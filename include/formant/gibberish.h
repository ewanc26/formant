#pragma once

// Deterministic phonotactic word generation. This module is a *text* layer:
// it produces IPA transcripts and Phoneme sequences for the ordinary renderer.
// Its seeded PRNG is never used by synthesis, so the engine remains sine-only.

#include <cstdint>
#include <string>
#include <string_view>
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

struct GibberishSettings {
    uint32_t seed = 1;
    int count = 1;
    // Seeded statement/question/list contours and phrase-final lengthening.
    bool prosody = true;
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

// Generate `settings.count` words. Throws std::invalid_argument on a bad
// grammar or inventory, and std::runtime_error if filters make every retry
// fail.
GibberishResult generateGibberish(const PatternGrammar &grammar,
                                  const PatternInventory &inventory,
                                  const GibberishSettings &settings,
                                  Language language = Language::English);

} // namespace formant
