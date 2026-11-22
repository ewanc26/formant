#include "formant/synthesizer.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <initializer_list>
#include <stdexcept>

namespace formant {
namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kHarmonicCeiling = 0.49; // fraction of the sample rate
// Hard cap on voiced partials. The Nyquist break normally stops the harmonic
// loop much earlier, but a near-zero fundamental never reaches it; without a
// cap such a voice would iterate billions of times per sample. Beyond this
// count the 1/h weighting makes each partial inaudible.
constexpr int kMaxHarmonics = 4096;

struct Segment {
    const Phoneme *phoneme = nullptr;
    int start = 0;
    int end = 0;
    std::array<FormantBand, 4> from{};
    bool wasVoiced = false;
    double wasNoise = 0.0;
};

struct NoisePartial {
    double frequency = 0.0;
    double phase = 0.0;
};

FormantBand zeroBand() { return {0.0, 0.0, 1.0}; }

std::array<FormantBand, 4> zeroBands() {
    return {zeroBand(), zeroBand(), zeroBand(), zeroBand()};
}

double clamp(double value, double low, double high) {
    return std::max(low, std::min(high, value));
}

double smooth(double t) {
    t = clamp(t, 0.0, 1.0);
    return t * t * (3.0 - 2.0 * t);
}

// Four sine-shaped resonances, each with a Lorentzian response. This is the
// only "filter" in the engine: no IIR filters, noise buffers or samples.
double resonance(double frequency, const std::array<FormantBand, 4> &bands,
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

std::vector<NoisePartial> makeNoisePartials(int sampleRate) {
    std::vector<NoisePartial> partials;
    partials.reserve(12);

    uint32_t state = 0x9e3779b9u;
    auto next = [&state]() {
        state = state * 1664525u + 1013904223u;
        return state;
    };

    const double ceiling = sampleRate * 0.45;
    for (int i = 0; i < 12; ++i) {
        const double spread = static_cast<double>(next() % 9000);
        NoisePartial partial;
        partial.frequency = std::min(1200.0 + spread, ceiling);
        partial.phase =
            (static_cast<double>(next() % 10000) / 10000.0) * 2.0 * kPi;
        partials.push_back(partial);
    }
    return partials;
}

FormantBand blendBand(const FormantBand &from, const FormantBand &to,
                      double mix) {
    return {
        from.frequency + (to.frequency - from.frequency) * mix,
        from.amplitude + (to.amplitude - from.amplitude) * mix,
        from.bandwidth + (to.bandwidth - from.bandwidth) * mix,
    };
}

} // namespace

Synthesizer::Synthesizer(Voice voice) : voice_(std::move(voice)) {}

std::vector<float> Synthesizer::render(const std::vector<Phoneme> &phonemes,
                                       int sampleRate) const {
    if (sampleRate < 8000) {
        throw std::invalid_argument("sample rate must be at least 8000 Hz");
    }
    if (phonemes.empty()) {
        return {};
    }

    const double rate = std::max(0.1, voice_.rate);
    const bool rising =
        std::any_of(phonemes.begin(), phonemes.end(),
                    [](const Phoneme &p) { return p.pitchRise; });

    std::vector<Segment> segments;
    segments.reserve(phonemes.size());
    int cursor = 0;
    std::array<FormantBand, 4> previousBands = zeroBands();
    bool previousVoiced = false;
    double previousNoise = 0.0;

    for (const Phoneme &phoneme : phonemes) {
        const int length = std::max(
            1, static_cast<int>(std::llround(phoneme.duration / rate *
                                             static_cast<double>(sampleRate))));
        Segment segment;
        segment.phoneme = &phoneme;
        segment.start = cursor;
        segment.end = cursor + length;
        segment.from = previousBands;
        segment.wasVoiced = previousVoiced;
        segment.wasNoise = previousNoise;
        segments.push_back(segment);

        cursor = segment.end;
        previousBands = phoneme.bands;
        previousVoiced = phoneme.voiced;
        previousNoise = phoneme.kind == PhonemeKind::Silence
                            ? 0.0
                            : (phoneme.voiced ? voice_.breathiness : 1.0);
    }

    const int totalSamples = cursor;
    std::vector<float> output(static_cast<size_t>(totalSamples), 0.0f);
    const std::vector<NoisePartial> partials = makeNoisePartials(sampleRate);
    const double transition =
        std::max(1.0, std::round(0.035 * sampleRate / rate));

    double phase = 0.0;
    for (const Segment &segment : segments) {
        const Phoneme &phoneme = *segment.phoneme;
        if (phoneme.kind == PhonemeKind::Silence) {
            continue;
        }

        const int length = segment.end - segment.start;
        const double attack = std::min(
            std::max(1.0, std::round((phoneme.kind == PhonemeKind::Plosive
                                          ? 0.003
                                          : voice_.attack) *
                                     sampleRate / rate)),
            length * 0.5);
        const double release = std::min(
            std::max(1.0, std::round((phoneme.kind == PhonemeKind::Plosive
                                          ? 0.004
                                          : voice_.release) *
                                     sampleRate / rate)),
            length * 0.5);

        const double voicedTarget = phoneme.voiced ? 1.0 : 0.0;
        const double noiseTarget = phoneme.voiced ? voice_.breathiness : 1.0;
        const double riseStart = static_cast<double>(
            totalSamples - std::min(totalSamples, sampleRate / 2));

        for (int s = segment.start; s < segment.end; ++s) {
            const double elapsed = static_cast<double>(s - segment.start);
            const double mix = smooth(elapsed / transition);
            const double vowelProgress =
                smooth(elapsed / static_cast<double>(length));
            const std::array<FormantBand, 4> &targetBands =
                phoneme.hasEndBands ? phoneme.endBands : phoneme.bands;
            std::array<FormantBand, 4> bands{};
            for (size_t b = 0; b < bands.size(); ++b) {
                bands[b] = blendBand(segment.from[b], phoneme.bands[b], mix);
                bands[b] = blendBand(bands[b], targetBands[b], vowelProgress);
            }

            const double voicedMix =
                segment.wasVoiced + (voicedTarget - segment.wasVoiced) * mix;
            const double noiseMix =
                segment.wasNoise + (noiseTarget - segment.wasNoise) * mix;
            const double envelope =
                std::min({1.0, elapsed / attack,
                          static_cast<double>(segment.end - s) / release});

            const double progress =
                static_cast<double>(s) / static_cast<double>(totalSamples);
            double fundamental =
                voice_.pitch * (1.0 - voice_.declination * progress) *
                (phoneme.pitchStart + (phoneme.pitchEnd - phoneme.pitchStart) *
                                          (static_cast<double>(elapsed) /
                                           static_cast<double>(length)));
            if (rising && s >= riseStart) {
                const double riseProgress =
                    static_cast<double>(s - riseStart) /
                    std::max(1.0,
                             static_cast<double>(totalSamples - riseStart));
                fundamental *= 1.0 + 0.35 * smooth(riseProgress);
            }
            if (voice_.vibratoRate > 0.0 && voice_.vibratoDepth > 0.0) {
                const double time =
                    static_cast<double>(s) / static_cast<double>(sampleRate);
                const double cents =
                    voice_.vibratoDepth *
                    std::sin(2.0 * kPi * voice_.vibratoRate * time);
                fundamental *= std::pow(2.0, cents / 1200.0);
            }
            fundamental = std::max(0.0, fundamental);
            phase = std::fmod(phase + 2.0 * kPi * fundamental /
                                          static_cast<double>(sampleRate),
                              2.0 * kPi);

            double voiced = 0.0;
            if (fundamental > 0.0 && voice_.harmonics > 0) {
                // The ceiling bounds the useful partials, but a very small
                // fundamental can keep every partial below it. Cap the count
                // so a pathological voice cannot render without bound.
                const int limit = std::min(voice_.harmonics, kMaxHarmonics);
                for (int h = 1; h <= limit; ++h) {
                    const double frequency =
                        fundamental * static_cast<double>(h);
                    if (frequency > sampleRate * kHarmonicCeiling) {
                        break;
                    }
                    voiced += resonance(frequency, bands, voice_.formantScale) /
                              static_cast<double>(h) *
                              std::sin(static_cast<double>(h) * phase);
                }
            }

            double noise = 0.0;
            if (noiseMix > 0.0) {
                const double time =
                    static_cast<double>(s) / static_cast<double>(sampleRate);
                double sum = 0.0;
                for (const NoisePartial &partial : partials) {
                    sum += resonance(partial.frequency, bands,
                                     voice_.formantScale) *
                           std::sin(2.0 * kPi * partial.frequency * time +
                                    partial.phase);
                }
                noise = 0.6 * sum / static_cast<double>(partials.size());
            }

            output[static_cast<size_t>(s)] =
                static_cast<float>(voice_.gain * envelope * phoneme.amplitude *
                                   (voicedMix * voiced + noiseMix * noise));
        }
    }

    float peak = 0.0f;
    for (float sample : output) {
        peak = std::max(peak, std::abs(sample));
    }
    if (peak > 0.0f) {
        const float normalise = 0.9f / peak;
        for (float &sample : output) {
            sample *= normalise;
        }
    }
    return output;
}

std::vector<float> synthesizeText(std::string_view text, const Voice &voice,
                                  int sampleRate) {
    return Synthesizer(voice).render(phonemize(text), sampleRate);
}

} // namespace formant
