#pragma once

#include <vector>

#include "formant/voice.h"
#include "formant/phoneme.h"

namespace formant {

// A pure value describing a render in progress. Default construction starts a
// fresh utterance. Pass the state returned by renderContinued() to the next
// call to append another utterance; no globals or wall-clock state are used.
//
// The accumulated phoneme sequence is the source of truth for the continuous
// context (declination, final rise, vibrato and formant crossfades), which is
// why a chain of calls produces byte-identical audio to one render of the
// concatenation. The remaining fields expose the exact carried values so an
// embedder can inspect or assert them.
struct RenderState {
    std::vector<Phoneme> phonemes; // every utterance rendered so far
    int sampleRate = 0;            // fixed for the whole chain
    double phase = 0.0;            // glottal phase at the end, radians [0, 2pi)
    double noiseTimeBase = 0.0; // absolute sine-noise time at the end, seconds
    int totalSamples = 0;       // combined utterance length in samples
    bool rising = false;        // true when the combined utterance rises
    double subPhase = 0.0;      // #140: F0/2 pulse phase, radians [0, 2pi)
};

// The normalized audio for all phonemes accumulated so far, plus the state to
// carry into the next call. `samples` always contains the whole chain, so the
// final call's buffer is the complete utterance.
struct RenderResult {
    std::vector<float> samples;
    RenderState state;
};

// Renders phonemes to mono 32-bit floating-point PCM. The output is
// deterministic: the same voice and phonemes always produce the same samples.
class Synthesizer {
  public:
    explicit Synthesizer(Voice voice);

    // sampleRate must be at least 8000. The returned buffer has exactly
    // round(total phoneme duration * sampleRate / rate) samples.
    std::vector<float> render(const std::vector<Phoneme> &phonemes,
                              int sampleRate = 44100) const;

    // Appends `phonemes` to `state` and renders the whole accumulated chain.
    // Each phoneme keeps its own attack/release and the scheduler's 35 ms
    // crossfade, so a splice receives exactly the same smooth treatment as
    // any interior phoneme boundary; no extra fade is inserted at the joint.
    // The glottal phase, sine-noise time base, formant inheritance and
    // utterance-wide prosody continue across calls, so the final result is
    // byte-identical to render() of the concatenated phoneme sequence.
    // `sampleRate` must match the state's rate once one has been set, and must
    // be at least 8000. The ceiling taper from issue #108 is stateless (a pure
    // function of frequency and rate), so it needs no carried field.
    RenderResult renderContinued(const std::vector<Phoneme> &phonemes,
                                 int sampleRate, RenderState state) const;

    const Voice &voice() const { return voice_; }

  private:
    Voice voice_;
};

// Convenience helper: phonemize and render in one call.
std::vector<float> synthesizeText(std::string_view text, const Voice &voice,
                                  int sampleRate = 44100);

// Language-aware convenience overload. The language only selects the text
// front end; rendering is identical for every language.
std::vector<float> synthesizeText(std::string_view text, Language language,
                                  const Voice &voice, int sampleRate = 44100);

} // namespace formant
