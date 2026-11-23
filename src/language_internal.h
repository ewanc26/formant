#pragma once

#include <string_view>
#include <vector>

#include "formant/phoneme.h"

namespace formant {

// Language-specific text front ends. Each module converts its own orthography
// to the shared Phoneme sequence; the synthesiser never knows which language
// produced it. Adding a language means adding a module and a registry entry,
// not changing the acoustic engine.
std::vector<Phoneme> phonemizeEnglish(std::string_view text);
std::vector<Phoneme> phonemizeSpanish(std::string_view text);

} // namespace formant
