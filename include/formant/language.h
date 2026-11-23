#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "formant/phoneme.h"

namespace formant {

// Optional realization changes for one IPA phoneme. Unset fields keep the
// value produced by the universal IPA mapping.
struct PhonemeOverride {
    std::optional<double> durationScale;
    std::optional<double> amplitude;
    std::optional<double> pitchStart;
    std::optional<double> pitchEnd;
    std::optional<double> formantScale;
};

// A user-defined language over the shared IPA core. The maps are plain data:
// grapheme spelling -> IPA, word spelling -> IPA, and IPA symbol -> realization
// overrides. With strictInventory, every phoneme produced from text must also
// appear in phonemes.
struct LanguageDefinition {
    std::string name;
    std::string tag;
    bool strictInventory = false;
    std::unordered_map<std::string, PhonemeOverride> phonemes;
    std::unordered_map<std::string, std::string> graphemes;
    std::unordered_map<std::string, std::string> words;
};

// Parse the line-oriented format documented in docs/language-files.md.
// Throws std::invalid_argument with a line number for malformed input.
LanguageDefinition parseLanguageDefinition(std::string_view text);

// Convert text using a user-defined language. Graphemes are matched longest
// first, whole words take precedence over grapheme rules, and realization
// overrides are applied after IPA parsing. Unknown graphemes throw in strict
// mode; otherwise they are skipped.
std::vector<Phoneme> phonemize(std::string_view text,
                               const LanguageDefinition &language);

} // namespace formant
