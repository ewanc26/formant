// Formant-position verification across supported sample rates.
//
// The engine derives its Nyquist ceiling from the caller's sample rate. This
// harness renders one steady vowel through a high harmonic count at five rates
// and measures the spectrum with a Goertzel filter at the harmonic grid. It is
// deliberately dependency-free: no FFT library, no recorded reference audio and
// no tolerance based on a stored waveform. Each peak is compared with the
// phoneme's own formant table, so changing a vowel target changes the
// expectation automatically.

#include "formant/formant.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <string>
#include <utility>
#include <vector>

namespace {

int failures = 0;

#define CHECK(condition)                                                       \
    do {                                                                       \
        if (!(condition)) {                                                    \
            std::cerr << "FAIL " << __FILE__ << ":" << __LINE__ << ": "        \
                      << #condition << "\n";                                   \
            ++failures;                                                        \
        }                                                                      \
    } while (false)

constexpr double kPi = 3.14159265358979323846;

// Hann-windowed Goertzel magnitude, normalised so a full-scale sinusoid at the
// probe frequency reads about 0.5. Homogeneous scaling keeps peak-to-guard
// ratios meaningful even though render() normalises the whole buffer to 0.9.
double bandEnergy(const std::vector<float> &samples, double frequency,
                  int sampleRate) {
    if (samples.empty()) {
        return 0.0;
    }
    const double coefficient =
        2.0 * std::cos(2.0 * kPi * frequency / sampleRate);
    double previous = 0.0;
    double previous2 = 0.0;
    double windowSum = 0.0;
    for (size_t i = 0; i < samples.size(); ++i) {
        const double phase = 2.0 * kPi * static_cast<double>(i) /
                             static_cast<double>(samples.size() - 1);
        const double window = 0.5 - 0.5 * std::cos(phase);
        windowSum += window;
        const double value = static_cast<double>(samples[i]) * window;
        const double current = value + coefficient * previous - previous2;
        previous2 = previous;
        previous = current;
    }
    if (windowSum <= 0.0) {
        return 0.0;
    }
    const double power = previous * previous + previous2 * previous2 -
                         coefficient * previous * previous2;
    return std::sqrt(std::max(0.0, power)) / (0.5 * windowSum);
}

struct Peak {
    double frequency = 0.0;
    double energy = 0.0;
};

// Scans only the harmonic grid the additive source can actually produce.
Peak strongestHarmonic(const std::vector<float> &samples, int sampleRate,
                       double fundamental, int harmonics, double low,
                       double high) {
    Peak best;
    const int ceiling =
        std::min(harmonics,
                 static_cast<int>(std::floor(sampleRate * 0.49 / fundamental)));
    for (int h = 1; h <= ceiling; ++h) {
        const double frequency = fundamental * static_cast<double>(h);
        if (frequency < low || frequency > high) {
            continue;
        }
        const double energy = bandEnergy(samples, frequency, sampleRate);
        if (energy > best.energy) {
            best = {frequency, energy};
        }
    }
    return best;
}

double guardEnergy(const std::vector<float> &samples, int sampleRate,
                   double fundamental, int harmonics, double low, double high) {
    Peak peak = strongestHarmonic(samples, sampleRate, fundamental, harmonics,
                                  low, high);
    return peak.energy;
}

void verifyRate(int sampleRate, const formant::Phoneme &vowel,
                const formant::Voice &voice) {
    const std::vector<float> samples =
        formant::Synthesizer(voice).render({vowel}, sampleRate);
    CHECK(samples.size() == static_cast<size_t>(sampleRate));
    if (samples.size() != static_cast<size_t>(sampleRate)) {
        return;
    }

    double previousPeak = 0.0;
    for (size_t band = 0; band < 2; ++band) {
        const double expected = vowel.bands[band].frequency;
        const Peak peak =
            strongestHarmonic(samples, sampleRate, voice.pitch, voice.harmonics,
                              expected - 300.0, expected + 300.0);
        CHECK(peak.energy > 0.0);
        CHECK(std::abs(peak.frequency - expected) <= 250.0);

        // A real formant peak must dominate nearby off-formant harmonics.
        // This catches a spectrum that merely happens to contain a harmonic
        // inside the search window without any resonance shaping it.
        const double lowerGuard = guardEnergy(
            samples, sampleRate, voice.pitch, voice.harmonics,
            std::max(voice.pitch, expected - 600.0), expected - 400.0);
        const double upperGuard =
            guardEnergy(samples, sampleRate, voice.pitch, voice.harmonics,
                        expected + 400.0, expected + 600.0);
        const double guard = std::max(lowerGuard, upperGuard);
        CHECK(peak.energy > guard * 1.5);
        if (band == 1) {
            CHECK(peak.energy > previousPeak * 0.01);
        }
        previousPeak = peak.energy;

        std::cout << sampleRate << " Hz F" << (band + 1) << ": expected "
                  << expected << " Hz, peak " << peak.frequency << " Hz, "
                  << "energy " << peak.energy << ", guard " << guard << "\n";
    }
}

} // namespace

int main() {
    const formant::Phoneme *source = formant::findPhoneme("ee");
    if (source == nullptr) {
        std::cerr << "FAIL missing test vowel 'ee'\n";
        return 1;
    }
    formant::Phoneme vowel = *source;
    vowel.duration = 1.0;

    formant::Voice voice;
    voice.pitch = 100.0;
    voice.harmonics = 200; // reaches beyond Nyquist at 22050 Hz
    voice.vibratoRate = 0.0;
    voice.vibratoDepth = 0.0;
    voice.breathiness = 0.0;
    voice.declination = 0.0;
    voice.attack = 0.002;
    voice.release = 0.002;
    voice.gain = 1.0;

    for (int sampleRate : {22050, 44100, 48000, 96000, 192000}) {
        verifyRate(sampleRate, vowel, voice);
    }

    if (failures != 0) {
        std::cerr << failures << " formant verification check(s) failed\n";
        return 1;
    }
    std::cout << "formant verification passed at five sample rates\n";
    return 0;
}
