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

    double fundamental =
        voice.pitch * (1.0 - voice.declination * progress) *
        (phoneme.pitchStart +
         (phoneme.pitchEnd - phoneme.pitchStart) * (elapsed / length));

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
