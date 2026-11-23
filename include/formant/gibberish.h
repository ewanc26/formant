#pragma once

// Deterministic phonotactic word generation. This module is a *text* layer:
// it produces IPA transcripts and Phoneme sequences for the ordinary renderer.
// Its seeded PRNG is never used by synthesis, so the engine remains sine-only.

#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include "formant/language.h"
#include "formant/phoneme.h"

namespace formant {

// A small, explicit xorshift32 source. There is no global state and no clock:
// identical seeds produce identical draws on every platform. Not for audio.
class SeededRng {
  public:
    explicit SeededRng(uint32_t seed);

    uint32_t next();
    // Uniform in [0, bound). Uses rejection to avoid modulo bias.
    uint32_t below(uint32_t bound);
    bool chance(double probability);

  private:
    uint32_t state_;
};

// An Awkwords-compatible pattern grammar. Definitions map a capital letter
// (A-Z) to another pattern; `main` is the word pattern. C, V and N have
// built-in defaults when a grammar does not redefine them.
struct PatternGrammar {
    std::vector<std::pair<char, std::string>> definitions;
    std::string main;
};

// The atoms a category letter expands to. Each atom is a valid IPA symbol (or
// short IPA sequence), so a generated transcript can be fed back to parseIpa().
class PatternInventory {
  public:
    void set(char letter, std::vector<std::string> atoms);
    const std::vector<std::string> &get(char letter) const;
    bool empty(char letter) const { return get(letter).empty(); }

  private:
    std::vector<std::string> categories_[26];
};

// Derived phoneme transition statistics for statistical word generation. Tables
// contain aggregate counts, not source dictionary entries. See
// data/gibberish/PROVENANCE.md.
struct PhonotacticNgramTable {
    std::string languageTag;
    std::string source;
    std::string license;
    std::vector<std::pair<std::string, uint32_t>> lengths;
    std::vector<std::pair<std::string, uint32_t>> starts;
    std::vector<std::pair<std::string, uint32_t>> ends;
    std::vector<std::pair<std::string, uint32_t>> unigrams;
    std::unordered_map<std::string,
                       std::vector<std::pair<std::string, uint32_t>>>
        bigrams;
    std::unordered_map<std::string,
                       std::vector<std::pair<std::string, uint32_t>>>
        trigrams;

    bool empty() const {
        return starts.empty() || unigrams.empty() || lengths.empty();
    }
};

enum class GibberishStyle { Pattern, Statistical, Combined };

struct GibberishSettings {
    uint32_t seed = 1;
    int count = 1;
    // Seeded statement/question/list contours and phrase-final lengthening.
    bool prosody = true;
    GibberishStyle style = GibberishStyle::Pattern;
    // Language/style tag for statistical tables, e.g. "en-us".
    std::string styleTag = "en-us";
    // Optional path to a custom .ngram table; when empty, built-in tables are
    // used for known tags.
    std::string styleTablePath;
};

struct GeneratedWord {
    std::string ipa;
    std::vector<Phoneme> phonemes;
};

struct GibberishResult {
    std::vector<GeneratedWord> words;
    // All words joined into one utterance with short inter-word pauses and the
    // seeded contours already applied. Feed this to Synthesizer::render().
    std::vector<Phoneme> phonemes;
    // One IPA transcript per line, suitable for --transcript or --ipa.
    std::string transcript;
};

// Parse a grammar source. Lines of the form `C=p/t/k` define subpatterns;
// all other non-empty lines form the main pattern (joined with spaces).
// Throws std::invalid_argument for malformed patterns, unknown category
// letters, weights outside 1..128, or excessive nesting.
PatternGrammar parsePatternGrammar(std::string_view source);

// Built-in C/V/N inventories, and one derived from a conlang definition's
// `class` directives (falling back to classifying its phoneme inventory).
PatternInventory defaultPatternInventory();
PatternInventory patternInventory(const LanguageDefinition &language);

// Inline documentation of the supported grammar.
std::string patternGrammarHelp();

// Load/inspect statistical n-gram tables. The committed table format is a plain
// text, line-oriented data file with provenance metadata in its header.
PhonotacticNgramTable parsePhonotacticNgramTable(std::string_view source);
PhonotacticNgramTable loadPhonotacticNgramTable(const std::string &path);
PhonotacticNgramTable builtinPhonotacticNgramTable(std::string_view tag);
std::vector<std::string> availableGibberishStyles();

// Generate `settings.count` words. Pattern mode uses the grammar; statistical
// mode samples phoneme sequences from n-gram tables constrained to the active
// profile inventory; combined mode uses the grammar but resolves category atoms
// through weighted statistical categories when possible. Throws
// std::invalid_argument on bad input and std::runtime_error when constraints
// make generation impossible.
GibberishResult generateGibberish(const PatternGrammar &grammar,
                                  const PatternInventory &inventory,
                                  const GibberishSettings &settings,
                                  Language language = Language::English);

} // namespace formant
