#include "formant/synthesizer.h"

#include "prosody.h"
#include "schedule.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <initializer_list>
#include <limits>
#include <stdexcept>

namespace formant {
namespace {

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
            (static_cast<double>(next() % 10000) / 10000.0) * 2.0 * detail::kPi;
        partials.push_back(partial);
    }
    return partials;
}

} // namespace

Synthesizer::Synthesizer(Voice voice) : voice_(std::move(voice)) {}

std::vector<float> Synthesizer::render(const std::vector<Phoneme> &phonemes,
                                       int sampleRate) const {
    if (sampleRate < 8000) {
        throw std::invalid_argument("sample rate must be at least 8000 Hz");
    }
    if (!std::isfinite(voice_.rate) || voice_.rate <= 0.0) {
        throw std::invalid_argument("rate must be positive and finite");
    }
    if (phonemes.empty()) {
        return {};
    }

    // The documented contract is round(total duration * sampleRate / rate)
    // samples. Never clamp the caller's rate: a slow rate must really produce
    // slower audio. The shared scheduler applies that cumulative-boundary
    // rule, so timing reports use exactly the spans rendered here.
    const double rate = voice_.rate;
    const bool rising = detail::utteranceRises(phonemes);
    const std::vector<detail::ScheduledSpan> spans =
        detail::schedulePhonemes(phonemes, rate, sampleRate);

    std::vector<Segment> segments;
    segments.reserve(phonemes.size());
    std::array<FormantBand, 4> previousBands = detail::zeroBands();
    bool previousVoiced = false;
    double previousNoise = 0.0;

    for (size_t i = 0; i < phonemes.size(); ++i) {
        const Phoneme &phoneme = phonemes[i];
        Segment segment;
        segment.phoneme = &phoneme;
        segment.start = spans[i].start;
        segment.end = spans[i].end;
        segment.from = previousBands;
        segment.wasVoiced = previousVoiced;
        segment.wasNoise = previousNoise;
        segments.push_back(segment);

        // A diphthong finishes at endBands, so the next segment must crossfade
        // from that final target, not from its initial bands.
        previousBands = phoneme.hasEndBands ? phoneme.endBands : phoneme.bands;
        previousVoiced = phoneme.voiced;
        previousNoise = phoneme.kind == PhonemeKind::Silence
                            ? 0.0
                            : (phoneme.voiced ? voice_.breathiness : 1.0);
    }

    const int totalSamples = spans.back().end;
    // Accumulate in double precision. Converting each sample to float before
    // normalisation can overflow with a large gain, leaving an infinite peak
    // and NaN samples after the inf * 0 scaling step.
    std::vector<double> raw(static_cast<size_t>(totalSamples), 0.0);
    const std::vector<NoisePartial> partials = makeNoisePartials(sampleRate);

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

        for (int s = segment.start; s < segment.end; ++s) {
            const double elapsed = static_cast<double>(s - segment.start);
            const double mix = detail::smooth(
                elapsed / static_cast<double>(
                              detail::transitionSamples(voice_, sampleRate)));
            const std::array<FormantBand, 4> bands =
                detail::bandsAt(phoneme, segment.from, s, segment.start,
                                segment.end, voice_, sampleRate);

            const double voicedMix =
                segment.wasVoiced + (voicedTarget - segment.wasVoiced) * mix;
            const double noiseMix =
                segment.wasNoise + (noiseTarget - segment.wasNoise) * mix;
            const double envelope =
                std::min({1.0, elapsed / attack,
                          static_cast<double>(segment.end - s) / release});

            const double fundamental = detail::fundamentalAt(
                voice_, phoneme, s, segment.start, segment.end, totalSamples,
                sampleRate, rising);
            phase = std::fmod(phase + 2.0 * detail::kPi * fundamental /
                                          static_cast<double>(sampleRate),
                              2.0 * detail::kPi);

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
                    voiced += detail::formantResponse(frequency, bands,
                                                      voice_.formantScale) /
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
                    sum +=
                        detail::formantResponse(partial.frequency, bands,
                                                voice_.formantScale) *
                        std::sin(2.0 * detail::kPi * partial.frequency * time +
                                 partial.phase);
                }
                noise = 0.6 * sum / static_cast<double>(partials.size());
            }

            raw[static_cast<size_t>(s)] =
                voice_.gain * envelope * phoneme.amplitude *
                (voicedMix * voiced + noiseMix * noise);
        }
    }

    double peak = 0.0;
    for (double sample : raw) {
        if (!std::isfinite(sample)) {
            throw std::invalid_argument(
                "voice parameters produce non-finite output");
        }
        peak = std::max(peak, std::abs(sample));
    }
    std::vector<float> output(raw.size(), 0.0f);
    if (peak > 0.0) {
        // Divide each sample by the peak before scaling. Forming a reciprocal
        // first would overflow to infinity when the peak is subnormal, turning
        // finite samples into inf and zero samples into NaN. Since |raw| <=
        // peak, each ratio is within [-1, 1] and cannot overflow.
        for (size_t i = 0; i < raw.size(); ++i) {
            output[i] = static_cast<float>((raw[i] / peak) * 0.9);
        }
    }
    return output;
}

std::vector<float> synthesizeText(std::string_view text, const Voice &voice,
                                  int sampleRate) {
    return Synthesizer(voice).render(phonemize(text), sampleRate);
}

std::vector<float> synthesizeText(std::string_view text, Language language,
                                  const Voice &voice, int sampleRate) {
    return Synthesizer(voice).render(phonemize(text, language), sampleRate);
}

} // namespace formant
