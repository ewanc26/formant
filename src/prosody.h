#pragma once

// Shared description of the intended prosody and spectral envelope.
//
// Synthesizer::render() and the objective-metrics analyzers both consume these
// functions. Keeping one copy means a report can never drift away from the
// parameters that actually produced the samples.

#include "formant/phoneme.h"
#include "formant/voice.h"
#include "sine_kernel.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace formant {
namespace detail {

inline constexpr double kPi = 3.14159265358979323846;

// Voiced partials stop at 0.49 * sampleRate. The safety cap on the partial
// count is shared by the renderer and the analyzer target tracks.
inline constexpr double kHarmonicCeiling = 0.49;
inline constexpr int kMaxHarmonics = 4096;

// Raised-cosine fade over the top 4% of the usable band. A partial loses all
// amplitude before the hard Nyquist break removes it, so a gliding F0 cannot
// pop a harmonic in or out. Below the window the gain is exactly 1, which
// keeps ordinary renders byte-identical.
inline constexpr double kCeilingTaper = 0.04;

inline double ceilingGain(double frequency, int sampleRate) {
    const double ceiling = kHarmonicCeiling * static_cast<double>(sampleRate);
    const double start = ceiling * (1.0 - kCeilingTaper);
    if (frequency <= start) {
        return 1.0;
    }
    if (frequency >= ceiling) {
        return 0.0;
    }
    const double t = (frequency - start) / (ceiling - start);
    return 0.5 * (1.0 + std::cos(kPi * t));
}

// The count cap is a second bound for subsonic voices that never reach
// Nyquist. Fade the same way so the cap is inaudible too; it only engages
// when the caller asked for more partials than the hard limit.
inline constexpr double kCapTaper = 0.04;

inline double capGain(int harmonic) {
    const double start = static_cast<double>(kMaxHarmonics) * (1.0 - kCapTaper);
    if (harmonic <= start) {
        return 1.0;
    }
    const double t = (static_cast<double>(harmonic) - start) /
                     (static_cast<double>(kMaxHarmonics) - start);
    if (t >= 1.0) {
        return 0.0;
    }
    return 0.5 * (1.0 + std::cos(kPi * t));
}

inline double clamp01(double value) {
    return std::max(0.0, std::min(1.0, value));
}

inline double smooth(double t) {
    t = clamp01(t);
    return t * t * (3.0 - 2.0 * t);
}

// Shared amplitude envelope for one segment. Non-plosive ramps use the same
// smoothstep as the crossfade, so the segment joins the sustain with matching
// value and zero slope. Plosives keep their short linear 3/4 ms bursts: that
// abruptness is the sound, and the exception is documented in design.md.
inline double amplitudeEnvelope(int offset, int length, double attack,
                                double release, bool plosive) {
    const double elapsed = static_cast<double>(offset);
    const double remaining = static_cast<double>(length - offset);
    if (plosive) {
        return std::min({1.0, elapsed / attack, remaining / release});
    }
    // Most of a segment is sustain; skip the polynomial on both sides when
    // the ramp has already reached 1. The comparisons return exactly the same
    // values as smooth() at and beyond the endpoints.
    const double attackGain =
        elapsed >= attack ? 1.0 : smooth(elapsed / attack);
    const double releaseGain =
        remaining >= release ? 1.0 : smooth(remaining / release);
    return std::min({1.0, attackGain, releaseGain});
}

inline bool utteranceRises(const std::vector<Phoneme> &phonemes) {
    for (auto it = phonemes.rbegin(); it != phonemes.rend(); ++it) {
        if (it->pitchRise) {
            return true;
        }
        if (it->kind != PhonemeKind::Silence) {
            return false;
        }
    }
    return false;
}

inline FormantBand zeroBand() { return {0.0, 0.0, 1.0}; }

inline std::array<FormantBand, 4> zeroBands() {
    return {zeroBand(), zeroBand(), zeroBand(), zeroBand()};
}

inline double blendFrequency(double from, double to, double mix) {
    // Formant spacing is perceived on a log-frequency scale, so frequencies
    // glide geometrically while amplitudes and bandwidths stay linear. A zero
    // frequency (the initial inherited band) has no logarithm; those fades
    // remain linear so the first segment still starts from silence correctly.
    if (mix <= 0.0 || from <= 0.0 || to <= 0.0 || from == to) {
        return from + (to - from) * mix;
    }
    if (mix >= 1.0) {
        return to;
    }
    return from * exp2Approx((log2Approx(to) - log2Approx(from)) * mix);
}

inline FormantBand blendBand(const FormantBand &from, const FormantBand &to,
                             double mix) {
    return {blendFrequency(from.frequency, to.frequency, mix),
            from.amplitude + (to.amplitude - from.amplitude) * mix,
            from.bandwidth + (to.bandwidth - from.bandwidth) * mix};
}

inline SpectralZero blendZero(const SpectralZero &from, const SpectralZero &to,
                              double mix) {
    return {from.frequency + (to.frequency - from.frequency) * mix,
            from.bandwidth + (to.bandwidth - from.bandwidth) * mix,
            from.depth + (to.depth - from.depth) * mix};
}

// The resonance total used to be hard-clamped with std::min(total, 2.0),
// which leaves a C0 corner in the amplitude weighting. The rational knee is
// exactly linear below 2.0 and has slope 1 at the corner, then saturates
// smoothly toward 2.25 instead of stopping dead.
inline constexpr double kResonanceClamp = 2.0;
inline constexpr double kResonanceKneeHeadroom = 0.25;

inline double softResonanceLimit(double total) {
    if (total <= kResonanceClamp) {
        return total;
    }
    const double over = total - kResonanceClamp;
    return kResonanceClamp +
           kResonanceKneeHeadroom * over / (kResonanceKneeHeadroom + over);
}

// Four sine-shaped resonances, each with a Lorentzian response. This is the
// only "filter" in the engine: no IIR filters, noise buffers or samples.
inline double formantResponse(double frequency,
                              const std::array<FormantBand, 4> &bands,
                              double scale) {
    double total = 0.0;
    for (const FormantBand &band : bands) {
        if (band.amplitude <= 0.0 || band.frequency <= 0.0) {
            continue;
        }
        const double centre = band.frequency * scale;
        const double width = band.bandwidth * std::max(0.7, scale);
        const double distance = (frequency - centre) / width;
        total += band.amplitude / (1.0 + distance * distance);
    }
    return softResonanceLimit(total);
}

// The synthesizer evaluates the same four resonances for every harmonic and
// every noise partial of a sample. Precomputing the scale-dependent centre and
// width once per sample hoists sixteen multiplications out of those loops. The
// response keeps the original operation order, so every result is bit-for-bit
// identical to formantResponse(frequency, bands, scale).
struct FormantEnvelope {
    std::array<double, 4> centre{};
    std::array<double, 4> width{};
    std::array<double, 4> amplitude{};
    std::array<bool, 4> active{};

    // Two table-driven anti-formants plus one voice-wide nasality notch.
    std::array<double, 3> zeroCentre{};
    std::array<double, 3> zeroWidth{};
    std::array<double, 3> zeroDepth{};
    std::array<bool, 3> zeroActive{};
};

inline FormantEnvelope
makeFormantEnvelope(const std::array<FormantBand, 4> &bands,
                    const std::array<SpectralZero, 2> &zeros, double scale,
                    double nasality = 0.0) {
    FormantEnvelope envelope;
    const double minimumScale = std::max(0.7, scale);
    for (std::size_t i = 0; i < bands.size(); ++i) {
        envelope.centre[i] = bands[i].frequency * scale;
        envelope.width[i] = bands[i].bandwidth * minimumScale;
        envelope.amplitude[i] = bands[i].amplitude;
        envelope.active[i] =
            bands[i].amplitude > 0.0 && bands[i].frequency > 0.0;
    }
    for (std::size_t i = 0; i < zeros.size(); ++i) {
        envelope.zeroCentre[i] = zeros[i].frequency * scale;
        envelope.zeroWidth[i] = zeros[i].bandwidth * minimumScale;
        envelope.zeroDepth[i] = std::clamp(zeros[i].depth, 0.0, 0.95);
        envelope.zeroActive[i] = envelope.zeroDepth[i] > 0.0 &&
                                 zeros[i].frequency > 0.0 &&
                                 zeros[i].bandwidth > 0.0;
    }
    // A pervasive nasal twang is one extra fixed zero, independent of the
    // phoneme's own articulatory zeros.
    envelope.zeroCentre[2] = 1000.0 * scale;
    envelope.zeroWidth[2] = 300.0 * minimumScale;
    envelope.zeroDepth[2] = std::clamp(0.45 * nasality, 0.0, 0.95);
    envelope.zeroActive[2] = envelope.zeroDepth[2] > 0.0;
    return envelope;
}

inline double formantResponse(double frequency,
                              const FormantEnvelope &envelope) {
    double total = 0.0;
    for (std::size_t i = 0; i < envelope.centre.size(); ++i) {
        if (!envelope.active[i]) {
            continue;
        }
        const double distance =
            (frequency - envelope.centre[i]) / envelope.width[i];
        total += envelope.amplitude[i] / (1.0 + distance * distance);
    }
    total = softResonanceLimit(total);

    // Anti-formants are the mirror of the resonances: a Lorentzian notch
    // subtracts a fraction of the envelope at its centre. With no active zero
    // the original value is returned untouched, keeping oral phonemes
    // bit-for-bit identical to previous releases.
    bool notched = false;
    double notch = 1.0;
    for (std::size_t i = 0; i < envelope.zeroCentre.size(); ++i) {
        if (!envelope.zeroActive[i]) {
            continue;
        }
        const double distance =
            (frequency - envelope.zeroCentre[i]) / envelope.zeroWidth[i];
        notch *= 1.0 - envelope.zeroDepth[i] / (1.0 + distance * distance);
        notched = true;
    }
    return notched ? total * notch : total;
}

inline int transitionSamples(const Voice &voice, int sampleRate) {
    return std::max(
        1, static_cast<int>(std::round(0.035 * sampleRate / voice.rate)));
}

// Intended formant bands at one sample of a segment. `from` is the band set
// inherited from the previous segment; a diphthong additionally glides from
// `bands` to `endBands` over its own span.
//
// A crossfade has three fixed log-frequency anchors (previous band, phoneme
// band, final target). Computing their logarithms once per segment removes
// two log2 calls per band from every render sample; the per-sample work is one
// lerp in log space and one exp2. `bandsAtWithTransition()` below builds the
// same plan on the fly, so metrics and audio still call identical arithmetic.
struct BandBlendPlan {
    std::array<double, 4> fromLog{};
    std::array<double, 4> phonemeLog{};
    std::array<double, 4> targetLog{};
    std::array<bool, 4> logarithmic{};
};

inline BandBlendPlan makeBandBlendPlan(const Phoneme &phoneme,
                                       const std::array<FormantBand, 4> &from) {
    const std::array<FormantBand, 4> &target =
        phoneme.hasEndBands ? phoneme.endBands : phoneme.bands;
    BandBlendPlan plan;
    for (std::size_t band = 0; band < plan.fromLog.size(); ++band) {
        plan.logarithmic[band] = from[band].frequency > 0.0 &&
                                 phoneme.bands[band].frequency > 0.0 &&
                                 target[band].frequency > 0.0;
        if (plan.logarithmic[band]) {
            plan.fromLog[band] = log2Approx(from[band].frequency);
            plan.phonemeLog[band] = log2Approx(phoneme.bands[band].frequency);
            plan.targetLog[band] = log2Approx(target[band].frequency);
        }
    }
    return plan;
}

inline std::array<FormantBand, 4>
bandsAtWithPlan(const Phoneme &phoneme, const BandBlendPlan &plan,
                const std::array<FormantBand, 4> &from, int sample,
                int segmentStart, int segmentEnd, int transition) {
    const double elapsed = static_cast<double>(sample - segmentStart);
    const double length =
        static_cast<double>(std::max(1, segmentEnd - segmentStart));
    const double mix = smooth(elapsed / static_cast<double>(transition));
    const double progress = smooth(elapsed / length);
    const std::array<FormantBand, 4> &target =
        phoneme.hasEndBands ? phoneme.endBands : phoneme.bands;

    std::array<FormantBand, 4> bands{};
    for (std::size_t band = 0; band < bands.size(); ++band) {
        double frequency;
        if (plan.logarithmic[band]) {
            if (plan.fromLog[band] == plan.phonemeLog[band] &&
                plan.phonemeLog[band] == plan.targetLog[band]) {
                // Nothing moves; keep the exact anchor instead of a log2 /
                // exp2 round trip. This also preserves steady-state renders.
                frequency = from[band].frequency;
            } else {
                const double first =
                    plan.fromLog[band] +
                    (plan.phonemeLog[band] - plan.fromLog[band]) * mix;
                frequency = exp2Approx(first + (plan.targetLog[band] - first) *
                                                   progress);
            }
        } else {
            // A zero inherited band cannot be logged, but the second stage
            // can: reproduce the old per-stage blend exactly rather than
            // forcing the whole fade to be linear.
            const double first =
                from[band].frequency +
                (phoneme.bands[band].frequency - from[band].frequency) * mix;
            frequency = blendFrequency(first, target[band].frequency, progress);
        }
        double amplitude =
            from[band].amplitude +
            (phoneme.bands[band].amplitude - from[band].amplitude) * mix;
        amplitude += (target[band].amplitude - amplitude) * progress;
        double bandwidth =
            from[band].bandwidth +
            (phoneme.bands[band].bandwidth - from[band].bandwidth) * mix;
        bandwidth += (target[band].bandwidth - bandwidth) * progress;
        bands[band] = {frequency, amplitude, bandwidth};
    }
    return bands;
}

inline std::array<FormantBand, 4>
bandsAtWithTransition(const Phoneme &phoneme,
                      const std::array<FormantBand, 4> &from, int sample,
                      int segmentStart, int segmentEnd, int transition) {
    return bandsAtWithPlan(phoneme, makeBandBlendPlan(phoneme, from), from,
                           sample, segmentStart, segmentEnd, transition);
}

inline std::array<FormantBand, 4>
bandsAt(const Phoneme &phoneme, const std::array<FormantBand, 4> &from,
        int sample, int segmentStart, int segmentEnd, const Voice &voice,
        int sampleRate) {
    return bandsAtWithTransition(phoneme, from, sample, segmentStart,
                                 segmentEnd,
                                 transitionSamples(voice, sampleRate));
}

// Anti-formants crossfade with the same 35 ms window as the formants, so a
// nasal consonant entering a vowel does not switch its notch on at full depth.
inline std::array<SpectralZero, 2>
zerosAtWithTransition(const Phoneme &phoneme,
                      const std::array<SpectralZero, 2> &from, int sample,
                      int segmentStart, int transition) {
    const double elapsed = static_cast<double>(sample - segmentStart);
    const double mix =
        smooth(elapsed / static_cast<double>(std::max(1, transition)));
    std::array<SpectralZero, 2> zeros{};
    for (std::size_t i = 0; i < zeros.size(); ++i) {
        zeros[i] = blendZero(from[i], phoneme.zeros[i], mix);
    }
    return zeros;
}

inline std::array<SpectralZero, 2>
zerosAt(const Phoneme &phoneme, const std::array<SpectralZero, 2> &from,
        int sample, int segmentStart, const Voice &voice, int sampleRate) {
    return zerosAtWithTransition(phoneme, from, sample, segmentStart,
                                 transitionSamples(voice, sampleRate));
}

// Intended fundamental at one sample. This includes declination, the
// phoneme's linear pitch contour, the utterance-final rise and vibrato.
inline double fundamentalAt(const Voice &voice, const Phoneme &phoneme,
                            int sample, int segmentStart, int segmentEnd,
                            int totalSamples, int sampleRate, bool rising) {
    const double elapsed = static_cast<double>(sample - segmentStart);
    const double length =
        static_cast<double>(std::max(1, segmentEnd - segmentStart));
    const double progress =
        static_cast<double>(sample) / static_cast<double>(totalSamples);

    // A prosody-direct target is an absolute Hz contour: it replaces the
    // voice-relative multiplier and declination for this segment only. The
    // optional branch is bypassed entirely for ordinary phonemes, so the
    // default path keeps its exact floating-point result.
    double fundamental;
    if (phoneme.f0StartHz.has_value()) {
        const double from = *phoneme.f0StartHz;
        const double to = phoneme.f0EndHz.value_or(from);
        fundamental = from + (to - from) * (elapsed / length);
    } else {
        fundamental =
            voice.pitch * (1.0 - voice.declination * progress) *
            (phoneme.pitchStart +
             (phoneme.pitchEnd - phoneme.pitchStart) * (elapsed / length));
    }

    if (rising) {
        const int riseStart =
            totalSamples - std::min(totalSamples, sampleRate / 2);
        if (sample >= riseStart) {
            const double riseProgress =
                static_cast<double>(sample - riseStart) /
                std::max(1.0, static_cast<double>(totalSamples - riseStart));
            fundamental *= 1.0 + 0.35 * smooth(riseProgress);
        }
    }

    if (voice.vibratoRate > 0.0 && voice.vibratoDepth > 0.0) {
        const double time =
            static_cast<double>(sample) / static_cast<double>(sampleRate);
        const double cents =
            voice.vibratoDepth * sine(2.0 * kPi * voice.vibratoRate * time);
        fundamental *= centsToRatioFast(cents);
    }

    // #125: jitter is a bounded, deterministic pitch perturbation. Two
    // incommensurate sines give an irregular but reproducible contour without a
    // PRNG or any clock state. The branch is skipped entirely at the neutral
    // value, so the default voice follows the exact path it always has.
    if (voice.jitterCents > 0.0) {
        const double time =
            static_cast<double>(sample) / static_cast<double>(sampleRate);
        const double cents = voice.jitterCents *
                             sine(2.0 * kPi * 11.0 * time + 0.37) *
                             sine(2.0 * kPi * 0.73 * time + 1.13);
        fundamental *= centsToRatioFast(cents);
    }
    return std::max(0.0, fundamental);
}

} // namespace detail
} // namespace formant
