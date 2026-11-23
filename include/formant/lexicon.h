#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace formant {

// One exact pronunciation override. Matching is ASCII case-insensitive and the
// IPA is parsed through the universal IPA mapping.
struct PronunciationEntry {
    std::string spelling;
    std::string ipa;
};

// One context-sensitive pronunciation choice. Rules are tried in file order;
// an empty before/after field means "any neighbouring word". Context matching
// is ASCII case-insensitive and ignores punctuation between words.
struct HomographRule {
    std::string spelling;
    std::string ipa;
    std::string before;
    std::string after;
};

// A user-supplied pronunciation layer. Entries are applied ahead of the
// language front end; homograph rules take precedence over exact entries.
struct PronunciationLexicon {
    std::vector<PronunciationEntry> words;
    std::vector<HomographRule> homographs;

    bool empty() const { return words.empty() && homographs.empty(); }
};

// Parse the line-oriented lexicon format documented in docs/lexicons.md.
// Throws std::invalid_argument with a line number for malformed input.
PronunciationLexicon parsePronunciationLexicon(std::string_view text);

} // namespace formant
