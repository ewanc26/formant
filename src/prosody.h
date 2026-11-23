#pragma once

// Shared description of the intended prosody and spectral envelope.
//
// Synthesizer::render() and the objective-metrics analyzers both consume these
// functions. Keeping one copy means a report can never drift away from the
// parameters that actually produced the samples.

#include "formant/phoneme.h"
#include "formant/voice.h"

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

inline FormantBand blendBand(const FormantBand &from, const FormantBand &to,
                             double mix) {
    return {from.frequency + (to.frequency - from.frequency) * mix,
            from.amplitude + (to.amplitude - from.amplitude) * mix,
            from.bandwidth + (to.bandwidth - from.bandwidth) * mix};
}

inline SpectralZero blendZero(const SpectralZero &from, const SpectralZero &to,
                              double mix) {
    return {from.frequency + (to.frequency - from.frequency) * mix,
            from.bandwidth + (to.bandwidth - from.bandwidth) * mix,
            from.depth + (to.depth - from.depth) * mix};
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
    return std::min(total, 2.0);
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
    total = std::min(total, 2.0);

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
// Same crossfade as bandsAt(), but accepts a precomputed transition length so
// the renderer can hoist transitionSamples() out of its per-sample loop.
inline std::array<FormantBand, 4>
bandsAtWithTransition(const Phoneme &phoneme,
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
    for (size_t band = 0; band < bands.size(); ++band) {
        bands[band] = blendBand(from[band], phoneme.bands[band], mix);
        bands[band] = blendBand(bands[band], target[band], progress);
    }
    return bands;
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
            voice.vibratoDepth * std::sin(2.0 * kPi * voice.vibratoRate * time);
        fundamental *= std::pow(2.0, cents / 1200.0);
    }
    return std::max(0.0, fundamental);
}

} // namespace detail
} // namespace formant
