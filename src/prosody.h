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

inline int transitionSamples(const Voice &voice, int sampleRate) {
    return std::max(
        1, static_cast<int>(std::round(0.035 * sampleRate / voice.rate)));
}

// Intended formant bands at one sample of a segment. `from` is the band set
// inherited from the previous segment; a diphthong additionally glides from
// `bands` to `endBands` over its own span.
inline std::array<FormantBand, 4>
bandsAt(const Phoneme &phoneme, const std::array<FormantBand, 4> &from,
        int sample, int segmentStart, int segmentEnd, const Voice &voice,
        int sampleRate) {
    const double elapsed = static_cast<double>(sample - segmentStart);
    const double length =
        static_cast<double>(std::max(1, segmentEnd - segmentStart));
    const double mix = smooth(
        elapsed / static_cast<double>(transitionSamples(voice, sampleRate)));
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
