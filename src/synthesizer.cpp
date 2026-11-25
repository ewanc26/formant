#include "formant/synthesizer.h"

#include "prosody.h"
#include "schedule.h"
#include "sine_kernel.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <initializer_list>
#include <limits>
#include <stdexcept>

namespace formant {
namespace {

struct Segment {
    const Phoneme *phoneme = nullptr;
    int start = 0;
    int end = 0;
    std::array<FormantBand, 4> from{};
    detail::BandBlendPlan blendPlan{};
    std::array<SpectralZero, 2> fromZeros{};
    bool wasVoiced = false;
    double wasNoise = 0.0;
};

struct NoisePartial {
    double frequency = 0.0;
    double phase = 0.0;
};

// The un-normalised waveform plus the continuous values that a continued
// render carries into the next call. Keeping this separate from normalisation
// means a chain can render the whole accumulated sequence before scaling, so
// its final buffer is byte-identical to a single render() of that sequence.
struct AudioDraft {
    std::vector<double> raw;
    double peak = 0.0;
    double phase = 0.0;
    int totalSamples = 0;
    bool rising = false;
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

void validate(const Voice &voice, int sampleRate) {
    if (sampleRate < 8000) {
        throw std::invalid_argument("sample rate must be at least 8000 Hz");
    }
    if (!std::isfinite(voice.rate) || voice.rate <= 0.0) {
        throw std::invalid_argument("rate must be positive and finite");
    }
}

AudioDraft renderDraft(const Voice &voice, const std::vector<Phoneme> &phonemes,
                       int sampleRate) {
    validate(voice, sampleRate);
    AudioDraft draft;
    if (phonemes.empty()) {
        return draft;
    }

    // The documented contract is round(total duration * sampleRate / rate)
    // samples. Never clamp the caller's rate: a slow rate must really produce
    // slower audio. The shared scheduler applies that cumulative-boundary
    // rule, so timing reports use exactly the spans rendered here.
    const double rate = voice.rate;
    draft.rising = detail::utteranceRises(phonemes);
    const std::vector<detail::ScheduledSpan> spans =
        detail::schedulePhonemes(phonemes, rate, sampleRate);

    std::vector<Segment> segments;
    segments.reserve(phonemes.size());
    std::array<FormantBand, 4> previousBands = detail::zeroBands();
    std::array<SpectralZero, 2> previousZeros{};
    bool previousVoiced = false;
    double previousNoise = 0.0;

    for (size_t i = 0; i < phonemes.size(); ++i) {
        const Phoneme &phoneme = phonemes[i];
        Segment segment;
        segment.phoneme = &phoneme;
        segment.start = spans[i].start;
        segment.end = spans[i].end;
        segment.from = previousBands;
        segment.blendPlan = detail::makeBandBlendPlan(phoneme, previousBands);
        segment.fromZeros = previousZeros;
        segment.wasVoiced = previousVoiced;
        segment.wasNoise = previousNoise;
        segments.push_back(segment);

        // A diphthong finishes at endBands, so the next segment must crossfade
        // from that final target, not from its initial bands.
        previousBands = phoneme.hasEndBands ? phoneme.endBands : phoneme.bands;
        previousZeros = phoneme.zeros;
        previousVoiced = phoneme.voiced;
        previousNoise = phoneme.kind == PhonemeKind::Silence
                            ? 0.0
                            : (phoneme.voiced ? voice.breathiness : 1.0);
    }

    draft.totalSamples = spans.back().end;
    // Accumulate in double precision. Converting each sample to float before
    // normalisation can overflow with a large gain, leaving an infinite peak
    // and NaN samples after the inf * 0 scaling step.
    draft.raw.assign(static_cast<size_t>(draft.totalSamples), 0.0);
    const std::vector<NoisePartial> partials = makeNoisePartials(sampleRate);

    for (const Segment &segment : segments) {
        const Phoneme &phoneme = *segment.phoneme;
        if (phoneme.kind == PhonemeKind::Silence) {
            continue;
        }

        const int length = segment.end - segment.start;
        const double attack = std::min(
            std::max(1.0, std::round((phoneme.kind == PhonemeKind::Plosive
                                          ? 0.003
                                          : voice.attack) *
                                     sampleRate / rate)),
            length * 0.5);
        const double release = std::min(
            std::max(1.0, std::round((phoneme.kind == PhonemeKind::Plosive
                                          ? 0.004
                                          : voice.release) *
                                     sampleRate / rate)),
            length * 0.5);

        const bool plosive = phoneme.kind == PhonemeKind::Plosive;
        const double voicedTarget = phoneme.voiced ? 1.0 : 0.0;
        const double noiseTarget = phoneme.voiced ? voice.breathiness : 1.0;
        // Both the segment crossfade and bandsAt() need the same transition
        // length. It depends only on the voice and sample rate, so computing it
        // once per segment avoids two round() calls per sample.
        const int crossfadeSamples =
            detail::transitionSamples(voice, sampleRate);

        for (int s = segment.start; s < segment.end; ++s) {
            const double elapsed = static_cast<double>(s - segment.start);
            const double mix =
                detail::smooth(elapsed / static_cast<double>(crossfadeSamples));
            const std::array<FormantBand, 4> bands = detail::bandsAtWithPlan(
                phoneme, segment.blendPlan, segment.from, s, segment.start,
                segment.end, crossfadeSamples);
            const std::array<SpectralZero, 2> zeros =
                detail::zerosAtWithTransition(phoneme, segment.fromZeros, s,
                                              segment.start, crossfadeSamples);
            const detail::FormantEnvelope formants =
                detail::makeFormantEnvelope(bands, zeros, voice.formantScale,
                                            voice.nasality);

            const double voicedMix =
                segment.wasVoiced + (voicedTarget - segment.wasVoiced) * mix;
            const double noiseMix =
                segment.wasNoise + (noiseTarget - segment.wasNoise) * mix;
            const double envelope = detail::amplitudeEnvelope(
                s - segment.start, length, attack, release, plosive);

            const double fundamental = detail::fundamentalAt(
                voice, phoneme, s, segment.start, segment.end,
                draft.totalSamples, sampleRate, draft.rising);
            draft.phase =
                std::fmod(draft.phase + 2.0 * detail::kPi * fundamental /
                                            static_cast<double>(sampleRate),
                          2.0 * detail::kPi);

            double voiced = 0.0;
            if (fundamental > 0.0 && voice.harmonics > 0) {
                // The ceiling bounds the useful partials, but a very small
                // fundamental can keep every partial below it. Cap the count
                // so a pathological voice cannot render without bound. Both
                // bounds fade their last few percent before the hard stop.
                const int limit =
                    std::min(voice.harmonics, detail::kMaxHarmonics);
                const bool capped = voice.harmonics >= detail::kMaxHarmonics;
                for (int h = 1; h <= limit; ++h) {
                    const double frequency =
                        fundamental * static_cast<double>(h);
                    if (frequency >= sampleRate * detail::kHarmonicCeiling) {
                        break;
                    }
                    double gain =
                        voice.taperCeiling
                            ? detail::ceilingGain(frequency, sampleRate)
                            : 1.0;
                    if (capped) {
                        gain *= voice.taperCeiling ? detail::capGain(h) : 1.0;
                    }
                    if (gain <= 0.0) {
                        continue;
                    }
                    voiced +=
                        detail::formantResponse(frequency, formants) /
                        static_cast<double>(h) * gain *
                        detail::sine(static_cast<double>(h) * draft.phase);
                }
            }

            double noise = 0.0;
            if (noiseMix > 0.0) {
                const double time =
                    static_cast<double>(s) / static_cast<double>(sampleRate);
                double sum = 0.0;
                for (const NoisePartial &partial : partials) {
                    sum +=
                        detail::formantResponse(partial.frequency, formants) *
                        detail::sine(2.0 * detail::kPi * partial.frequency *
                                         time +
                                     partial.phase);
                }
                noise = 0.6 * sum / static_cast<double>(partials.size());
            }

            double sample = voice.gain * envelope * phoneme.amplitude *
                            (voicedMix * voiced + noiseMix * noise);
            // #125: shimmer is a bounded, deterministic amplitude perturbation
            // from the affect layer. It shares the sine-only source model and
            // leaves the neutral path byte-for-byte unchanged.
            if (voice.shimmerDepth > 0.0) {
                const double time =
                    static_cast<double>(s) / static_cast<double>(sampleRate);
                const double modulation =
                    1.0 +
                    voice.shimmerDepth *
                        detail::sine(2.0 * detail::kPi * 7.0 * time + 0.23) *
                        detail::sine(2.0 * detail::kPi * 0.9 * time + 0.61);
                sample *= modulation;
            }
            draft.raw[static_cast<size_t>(s)] = sample;
        }
    }

    for (double sample : draft.raw) {
        if (!std::isfinite(sample)) {
            throw std::invalid_argument(
                "voice parameters produce non-finite output");
        }
        draft.peak = std::max(draft.peak, std::abs(sample));
    }
    return draft;
}

std::vector<float> normalize(const AudioDraft &draft) {
    std::vector<float> output(draft.raw.size(), 0.0f);
    if (draft.peak > 0.0) {
        // Divide each sample by the peak before scaling. Forming a reciprocal
        // first would overflow to infinity when the peak is subnormal, turning
        // finite samples into inf and zero samples into NaN. Since |raw| <=
        // peak, each ratio is within [-1, 1] and cannot overflow.
        for (size_t i = 0; i < draft.raw.size(); ++i) {
            output[i] = static_cast<float>((draft.raw[i] / draft.peak) * 0.9);
        }
    }
    return output;
}

} // namespace

Synthesizer::Synthesizer(Voice voice) : voice_(std::move(voice)) {}

std::vector<float> Synthesizer::render(const std::vector<Phoneme> &phonemes,
                                       int sampleRate) const {
    return normalize(renderDraft(voice_, phonemes, sampleRate));
}

RenderResult Synthesizer::renderContinued(const std::vector<Phoneme> &phonemes,
                                          int sampleRate,
                                          RenderState state) const {
    validate(voice_, sampleRate);
    if (state.sampleRate != 0 && state.sampleRate != sampleRate) {
        throw std::invalid_argument(
            "continued render sample rate must match the state");
    }

    RenderState next = std::move(state);
    next.sampleRate = sampleRate;
    next.phonemes.insert(next.phonemes.end(), phonemes.begin(), phonemes.end());

    const AudioDraft draft = renderDraft(voice_, next.phonemes, sampleRate);
    next.phase = draft.phase;
    next.noiseTimeBase = static_cast<double>(draft.totalSamples) /
                         static_cast<double>(sampleRate);
    next.totalSamples = draft.totalSamples;
    next.rising = draft.rising;
    return {normalize(draft), std::move(next)};
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
