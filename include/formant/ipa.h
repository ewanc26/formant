#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "formant/phoneme.h"
#include "formant/voice.h"

namespace formant {

// Parse a UTF-8 International Phonetic Alphabet transcription. Base letters,
// combining diacritics, tie-bar affricates, stress/length marks, tones and
// suprasegmentals are accepted; unknown symbols throw std::invalid_argument.
// The result uses the same Phoneme struct as the English text layer.
std::vector<Phoneme> parseIpa(std::string_view transcription);

// Parse X-SAMPA (7-bit ASCII) by translating it to IPA, then parsing that.
// This covers the standard X-SAMPA symbol, diacritic and SAMPROSA tables.
std::vector<Phoneme> parseXsampa(std::string_view transcription);

// Canonical IPA symbols recognised by parseIpa, in chart-like order. Useful
// for discovery and for checking coverage in tests.
std::vector<std::string> ipaSymbols();

// Render an IPA / X-SAMPA transcription directly.
std::vector<float> synthesizeIpa(std::string_view transcription,
                                 const Voice &voice, int sampleRate = 44100);
std::vector<float> synthesizeXsampa(std::string_view transcription,
                                    const Voice &voice, int sampleRate = 44100);

} // namespace formant
