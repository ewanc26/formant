// Accuracy, distortion and benchmark checks for the shared sine kernel
// (#107). The kernel is compared with std::sin over the argument range the
// renderer uses, a rendered pure tone is measured for THD+N against the
// documented -90 dB budget, and the vibrato ratio is checked against
// std::pow. The timing numbers are printed for docs/design.md; they are not
// asserted because they depend on the machine and compiler.

#include <chrono>
#include <cmath>
#include <iostream>
#include <string>

#include "../src/sine_kernel.h"

namespace {

int g_failures = 0;

#define CHECK(expr)                                                            \
    do {                                                                       \
        if (!(expr)) {                                                         \
            std::cerr << "FAILED: " << #expr                                   \
                      << " at formant_sine_kernel.cpp:" << __LINE__ << "\\n";   \
            ++g_failures;                                                      \
        }                                                                      \
    } while (0)

constexpr double kTwoPi = 6.28318530717958647692;
constexpr double kSampleRate = 44100.0;
constexpr double kToneHz = 440.0;

// Power of the residual after removing the best-fit fundamental, relative to
// the fundamental's power, in dB. This is a THD+N measurement for a generated
// pure tone: everything the kernel adds (harmonics and numerical noise) is
// counted.
double toneThdPlusNoiseDb() {
    const int samples = static_cast<int>(kSampleRate);
    const double omega = kTwoPi * kToneHz / kSampleRate;

    double sumCos = 0.0;
    double sumSin = 0.0;
    for (int n = 0; n < samples; ++n) {
        const double tone = formant::detail::sine(omega * n);
        sumCos += tone * std::cos(omega * n);
        sumSin += tone * std::sin(omega * n);
    }
    const double a = 2.0 * sumCos / samples;
    const double b = 2.0 * sumSin / samples;

    double residualPower = 0.0;
    for (int n = 0; n < samples; ++n) {
        const double fit =
            a * std::cos(omega * n) + b * std::sin(omega * n);
        const double residual = formant::detail::sine(omega * n) - fit;
        residualPower += residual * residual;
    }
    residualPower /= samples;

    // A unit-amplitude sine has power 1/2.
    return 10.0 * std::log10(residualPower / 0.5);
}

template <typename Fn>
double nanosecondsPerCall(Fn &&fn, int iterations) {
    volatile double sink = 0.0;
    double phase = 0.0;
    for (int i = 0; i < 1000; ++i) {
        sink = sink + fn(phase);
        phase += 0.01;
    }
    const auto start = std::chrono::steady_clock::now();
    for (int i = 0; i < iterations; ++i) {
        sink = sink + fn(phase);
        phase += 0.01;
    }
    const auto end = std::chrono::steady_clock::now();
    (void)sink;
    return std::chrono::duration<double, std::nano>(end - start).count() /
           static_cast<double>(iterations);
}

} // namespace

int main() {
    using formant::detail::centsToRatioFast;
    using formant::detail::sine;

    // Absolute accuracy against libm over [-1000, 1000] radians, which covers
    // every harmonic and noise argument in a normal render.
    double maxError = 0.0;
    double maxErrorAt = 0.0;
    for (int i = 0; i <= 2000000; ++i) {
        const double x = -1000.0 + static_cast<double>(i) * 0.001;
        const double error = std::abs(sine(x) - std::sin(x));
        if (error > maxError) {
            maxError = error;
            maxErrorAt = x;
        }
    }
    std::cout << "sine max absolute error " << maxError << " at " << maxErrorAt
              << " rad\n";
    CHECK(maxError < 1e-6);

    // At the extremes of the renderer's noise path the argument can be much
    // larger; range reduction must still stay inside the error budget.
    for (double x : {1.0e5, 1.0e6, 1.0e7}) {
        const double error = std::abs(sine(x) - std::sin(x));
        std::cout << "sine absolute error at " << x << " rad " << error << "\n";
        CHECK(error < 1e-7);
    }

    // A rendered 440 Hz tone must stay inside the documented THD+N budget.
    const double thdn = toneThdPlusNoiseDb();
    std::cout << "440 Hz THD+N " << thdn << " dB\n";
    CHECK(thdn <= -90.0);

    // The vibrato cents factor must agree with the exact power-of-two ratio
    // over the range voice files allow.
    double maxRatioError = 0.0;
    for (int cents = -200; cents <= 200; ++cents) {
        const double exact = std::pow(2.0, static_cast<double>(cents) / 1200.0);
        const double got = centsToRatioFast(static_cast<double>(cents));
        maxRatioError = std::max(maxRatioError, std::abs(got / exact - 1.0));
    }
    std::cout << "cents ratio max relative error " << maxRatioError << "\n";
    CHECK(maxRatioError < 1e-6);

    // Reference timing, printed for docs/design.md. Equal work per call keeps
    // the comparison honest: both sides evaluate one phase argument.
    // Take the best of five runs: the median is still sensitive to a single
    // scheduler interruption on a shared CI machine, while the minimum is the
    // closest estimate of the per-call cost.
    constexpr int kIterations = 2000000;
    double kernelNs = 1e9;
    double libmNs = 1e9;
    for (int run = 0; run < 5; ++run) {
        kernelNs = std::min(
            kernelNs, nanosecondsPerCall(
                          [](double phase) {
                              return formant::detail::sine(phase);
                          },
                          kIterations));
        libmNs = std::min(
            libmNs, nanosecondsPerCall(
                        [](double phase) { return std::sin(phase); },
                        kIterations));
    }
    std::cout << "timing kernel " << kernelNs << " ns/call, std::sin "
              << libmNs << " ns/call, speedup " << libmNs / kernelNs << "x\n";

    std::cout << "formant_sine_kernel " << (g_failures ? "FAILED" : "OK")
              << "\n";
    return g_failures ? 1 : 0;
}
