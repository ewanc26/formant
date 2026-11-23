#pragma once

// Internal fast mathematics for the render hot path (#107). Deliberately not
// part of the public API: the engine promises the same phoneme and voice
// behaviour, not a particular approximation of sine.
//
// The kernel replaces libm's std::sin in the per-harmonic and per-noise-partial
// loops, and the vibrato cents factor replaces std::pow. Both are pure,
// deterministic functions: the sine table is built once from a polynomial (not
// from the host libm) and is immutable thereafter.

namespace formant::detail {

// Approximates sin(radians) with a wrapped table and linear interpolation.
// Absolute error is below 1e-6 over the argument range the renderer uses; the
// committed THD+N test asserts a much stricter -90 dB budget for a rendered
// pure tone.
double sine(double radians);

// Approximates 2^(cents/1200), the vibrato cents-to-ratio factor, for the
// bounded depth range a voice may carry. Relative error is below 1e-8.
double centsToRatioFast(double cents);

// Approximates log2(x) for finite x > 0 with absolute error below 2e-7. Used
// by the formant-frequency blend so a glide is linear in pitch, not in Hz.
double log2Approx(double x);

// Approximates 2^x for |x| <= 60. Relative error is below 2e-7. The formant
// blend uses it with a bounded ratio, so no argument outside that range reaches
// the renderer.
double exp2Approx(double x);

} // namespace formant::detail
