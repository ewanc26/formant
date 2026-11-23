#pragma once

#include <vector>

#include "formant/voice.h"
#include "formant/phoneme.h"

namespace formant {

// Renders phonemes to mono 32-bit floating-point PCM. The output is
// deterministic: the same voice and phonemes always produce the same samples.
class Synthesizer {
  public:
    explicit Synthesizer(Voice voice);

    // sampleRate must be at least 8000. The returned buffer has exactly
    // round(total phoneme duration * sampleRate / rate) samples.
    std::vector<float> render(const std::vector<Phoneme> &phonemes,
                              int sampleRate = 44100) const;

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
