#include "sine_kernel.h"

#include <array>
#include <cmath>
#include <cstddef>

namespace formant::detail {
namespace {

constexpr double kTwoPi = 6.28318530717958647692;
constexpr double kTwoOverPi = 0.63661977236758134308;
constexpr double kHalfPi = 1.57079632679489661923;
constexpr double kLn2Over1200 = 0.00057762265046662111;
constexpr double kLn2 = 0.69314718055994530942;
constexpr double kLog2e = 1.44269504088896340736;
constexpr std::size_t kTableSize = 4096;
constexpr double kIndexScale =
    static_cast<double>(kTableSize) / kTwoPi;

// Taylor series to x^9. The reduced argument is at most pi/4, where the first
// omitted term is about 4e-13. Used to build the table, and nowhere else: a
// libm std::sin would make table entries depend on the host's libm and so
// make rendered bytes differ between supported platforms.
inline double sinPoly(double x) {
    const double x2 = x * x;
    return x * (1.0 +
                x2 * (-1.0 / 6.0 +
                      x2 * (1.0 / 120.0 +
                            x2 * (-1.0 / 5040.0 + x2 * (1.0 / 362880.0)))));
}

// Taylor series to x^10. The first omitted term at pi/4 is about 3e-12.
inline double cosPoly(double x) {
    const double x2 = x * x;
    return 1.0 +
           x2 * (-0.5 +
                 x2 * (1.0 / 24.0 +
                       x2 * (-1.0 / 720.0 +
                             x2 * (1.0 / 40320.0 + x2 * (-1.0 / 3628800.0)))));
}

double referenceSine(double radians) {
    const double scaled = radians * kTwoOverPi;
    const long long quadrant =
        static_cast<long long>(scaled + (scaled >= 0.0 ? 0.5 : -0.5));
    const double reduced = (scaled - static_cast<double>(quadrant)) * kHalfPi;

    switch (static_cast<unsigned long long>(quadrant) & 3ULL) {
    case 0:
        return sinPoly(reduced);
    case 1:
        return cosPoly(reduced);
    case 2:
        return -sinPoly(reduced);
    default:
        return -cosPoly(reduced);
    }
}

struct SineTable {
    std::array<double, kTableSize + 1> value{};
};

const SineTable &sineTable() {
    // Immutable after construction and initialised deterministically from the
    // polynomial, so every platform builds the same entries. Function-local
    // static keeps the one-time cost out of static-initialisation order and
    // out of every other translation unit.
    static const SineTable table = [] {
        SineTable result;
        for (std::size_t i = 0; i <= kTableSize; ++i) {
            result.value[i] = referenceSine(
                kTwoPi * static_cast<double>(i) / static_cast<double>(kTableSize));
        }
        return result;
    }();
    return table;
}

} // namespace

double sine(double radians) {
    // The table covers exactly one turn with a power-of-two entry count, so a
    // wrap is a mask on the integer part of the scaled position; the signed
    // fraction stays in (-1, 1) and interpolation on the wrapped segment is
    // still the two nearest entries. This keeps large noise arguments as
    // accurate as the reduced argument's rounding error without a division.
    const double position = radians * kIndexScale;
    // Floor, not truncation: for a negative position the fraction must be in
    // [0, 1) so the interpolation uses the segment the position actually falls
    // in, not the one after it extrapolated backwards.
    const long long whole =
        static_cast<long long>(std::floor(position));
    const std::size_t index =
        static_cast<std::size_t>(whole) & (kTableSize - 1);
    const double fraction = position - static_cast<double>(whole);
    const auto &values = sineTable().value;
    return values[index] + (values[index + 1] - values[index]) * fraction;
}

double centsToRatioFast(double cents) {
    // 2^(cents/1200) = exp(cents * ln(2) / 1200). Voice files bound the depth
    // to 200 cents, so |x| <= 0.116 and the degree-6 series is accurate to
    // better than 4e-9 relative.
    const double x = cents * kLn2Over1200;
    return 1.0 +
           x * (1.0 +
                x * (0.5 +
                     x * (1.0 / 6.0 +
                          x * (1.0 / 24.0 +
                               x * (1.0 / 120.0 + x * (1.0 / 720.0))))));
}

double log2Approx(double x) {
    // Range-reduce with frexp, then use the atanh series for ln(mantissa).
    // frexp is exact, and with the mantissa in [0.5, 1) the series argument is
    // at most 1/3, where the first omitted term is below 1e-7 after scaling by
    // log2(e). No libm logarithm is called, so the blend is platform-stable.
    int exponent = 0;
    const double mantissa = std::frexp(x, &exponent);
    const double z = (mantissa - 1.0) / (mantissa + 1.0);
    const double z2 = z * z;
    const double series =
        2.0 * z *
        (1.0 + z2 * (1.0 / 3.0 +
                     z2 * (1.0 / 5.0 +
                           z2 * (1.0 / 7.0 +
                                 z2 * (1.0 / 9.0 +
                                       z2 * (1.0 / 11.0))))));
    return static_cast<double>(exponent) + series * kLog2e;
}

double exp2Approx(double x) {
    // Split the integer and fractional powers. The degree-8 Taylor series for
    // exp(f * ln 2) on f in [0, 1) is accurate to about 1e-7 relative, and
    // ldexp applies the integer power exactly.
    if (x < -60.0) {
        x = -60.0;
    } else if (x > 60.0) {
        x = 60.0;
    }
    const double whole = std::floor(x);
    const double t = (x - whole) * kLn2;
    const double polynomial =
        1.0 +
        t * (1.0 +
             t * (0.5 +
                  t * (1.0 / 6.0 +
                       t * (1.0 / 24.0 +
                            t * (1.0 / 120.0 +
                                 t * (1.0 / 720.0 +
                                      t * (1.0 / 5040.0 +
                                           t * (1.0 / 40320.0))))))));
    return std::ldexp(polynomial, static_cast<int>(whole));
}

} // namespace formant::detail
