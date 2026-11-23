#pragma once

// Parameter-domain transposition: change the numbers that feed the additive
// engine rather than processing rendered audio. There is no phase vocoder, no
// post-processing pass and no hidden state, so a transposed voice stays a sum
// of sines and renders byte-identically for identical arguments.

#include "formant/voice.h"

#include <vector>

namespace formant {

// Converts an interval in cents to a frequency ratio (1200 cents = 2x).
// Throws std::invalid_argument for a non-finite interval.
double centsToRatio(double cents);

// Returns a copy of `voice` whose fundamental is scaled by `cents`. A voiced
// pitch of zero stays zero, so a whisper remains unvoiced. The formant targets
// and the noise stack are untouched.
Voice shiftPitch(const Voice &voice, double cents);

// Returns a copy whose formant targets and bandwidths are multiplied by
// `ratio`, leaving F0 unchanged. Ratios must be finite and positive.
Voice scaleFormants(const Voice &voice, double ratio);

// Physical interpretation of a formant ratio: a longer vocal tract lowers
// its resonances, so `tractScale > 1` divides the formant scale. This is the
// reciprocal of scaleFormants() and leaves F0 unchanged.
Voice withVocalTractLength(const Voice &voice, double tractScale);

// Pitch and formants in one pure step. Equivalent to shiftPitch() followed by
// scaleFormants(); provided so an embedder can describe a character variant
// with a single value object.
Voice transposeVoice(const Voice &voice, double cents, double formantRatio);

// Choir unison spread: one voice per cent offset, each a pure detune of the
// base voice with identical formants. Composes with any melodic layer that
// sets F0 per note, because every returned voice keeps its own F0 scale.
std::vector<Voice> detuneVoices(const Voice &voice,
                                const std::vector<double> &cents);

} // namespace formant
