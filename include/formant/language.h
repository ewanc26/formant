#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "formant/phoneme.h"
#include "formant/lexicon.h"

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

// Language-level typology defaults declared in a conlang file. `rate` scales
// segment durations and `stress=fixed` assigns a default stress to the first
// vowel of each word. `tone` is recorded as metadata. `rhythm` is metadata by
// default; the optional PVI dials and strength below become active only when a
// caller opts in with `--rhythm auto` (or calls resolveRhythmProfile directly).
struct LanguageTypology {
    std::string tone = "unspecified";   // none | simple | complex
    std::string stress = "unspecified"; // none | fixed | free
    std::string rhythm = "unspecified"; // stress | syllable | mora
    std::optional<double> rate;         // positive duration multiplier
    std::optional<double> vocalicNPviMin;
    std::optional<double> vocalicNPviMax;
    std::optional<double> intervocalicRPviMin;
    std::optional<double> intervocalicRPviMax;
    std::optional<double> rhythmStrength;
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
    // Ordered rewrite rules, stored as their bodies (for example
    // "c > s / _ e"). Grapheme rules run before grapheme lookup; phoneme rules
    // run after lexicon/grapheme mapping and before realization overrides.
    std::unordered_map<std::string, std::vector<std::string>> classes;
    std::vector<std::string> graphemeRules;
    std::vector<std::string> phonemeRules;
    LanguageTypology typology;
};

// Parse the line-oriented format documented in docs/language-files.md.
// Throws std::invalid_argument with a line number for malformed input.
LanguageDefinition parseLanguageDefinition(std::string_view text);

// Convert text using a user-defined language. Whole words take precedence and
// skip the grapheme phase; otherwise ordered grapheme rules run before
// longest-match grapheme lookup. Ordered phoneme rules then run on the mapped
// sequence, and realization overrides and typology defaults are applied last.
// Unknown graphemes throw in strict mode; otherwise they are skipped. In
// lenient mode a phoneme outside the declared inventory is replaced by the
// nearest declared phoneme and reported in `warnings` (when non-null). Strict
// mode rejects it with the nearest candidate named in the error. When trace is
// non-null, each rewrite-rule application appends one stable debug line to it.
std::vector<Phoneme> phonemize(std::string_view text,
                               const LanguageDefinition &language,
                               std::vector<std::string> *trace = nullptr,
                               std::vector<std::string> *warnings = nullptr);

// Convert text with a user pronunciation layer. Homograph rules are tried in
// file order, then exact entries; unmatched words fall back to the selected
// language front end. With a language definition, exact user entries override
// its word lexicon while its grapheme and phoneme rules still apply to
// unmatched words.
std::vector<Phoneme> phonemize(std::string_view text, Language language,
                               const PronunciationLexicon &lexicon,
                               std::vector<std::string> *trace = nullptr);
std::vector<Phoneme> phonemize(std::string_view text,
                               const LanguageDefinition &language,
                               const PronunciationLexicon &lexicon,
                               std::vector<std::string> *trace = nullptr,
                               std::vector<std::string> *warnings = nullptr);

} // namespace formant
