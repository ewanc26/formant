#pragma once

#include <array>
#include <string>
#include <string_view>
#include <vector>

namespace formant {

enum class PhonemeKind {
    Silence,
    Voiced,
    Nasal,
    Fricative,
    Plosive,
    Approximant
};

// One resonance of the vocal tract. All four bands are sine-wave energy
// peaks; there is no sampled audio or filtered noise anywhere in the engine.
struct FormantBand {
    double frequency = 0.0;
    double amplitude = 0.0;
    double bandwidth = 100.0;

    bool operator==(const FormantBand &) const = default;
};

struct Phoneme {
    std::string name;
    PhonemeKind kind = PhonemeKind::Silence;
    std::array<FormantBand, 4> bands{};
    double duration = 0.1; // seconds at rate 1.0
    bool voiced = false;
    bool pitchRise = false; // marks a question-like final contour

    // Diphthongs move from bands to endBands across the phoneme. For other
    // sounds endBands is unused and hasEndBands stays false.
    std::array<FormantBand, 4> endBands{};
    bool hasEndBands = false;

    // Prosody, primarily set by the IPA front end. 1.0 is neutral.
    // pitchStart/pitchEnd describe a linear F0 contour across the segment.
    double amplitude = 1.0;
    double pitchStart = 1.0;
    double pitchEnd = 1.0;

    bool operator==(const Phoneme &) const = default;
};

// The built-in phoneme inventory, in a stable order.
const std::vector<Phoneme> &phonemeTable();

// Case-sensitive lookup of a phoneme by its short name ("ee", "th", "_").
const Phoneme *findPhoneme(std::string_view name);

// Text front ends share one phoneme inventory and one synthesis path. The
// language tag selects only grapheme-to-phoneme conversion; IPA and X-SAMPA
// input bypass the text layer entirely.
enum class Language { English, Spanish };

// Stable BCP-47-style primary tags ("en", "es") and display names.
std::string_view languageTag(Language language);
std::string_view languageName(Language language);
std::vector<std::string> languageTags();

// Parse a language tag, accepting any region or script suffix ("es-MX",
// "en_US"). Throws std::invalid_argument for an unsupported language.
Language languageFromTag(std::string_view tag);

// Convert text to phonemes with the selected language front end. English is
// the default, so existing callers keep their behaviour. Unknown English
// words fall back to deterministic grapheme-to-phoneme conversion.
std::vector<Phoneme> phonemize(std::string_view text,
                               Language language = Language::English);

// Parse a bracketed phoneme string such as "[h ee l ow]" used by the CLI.
// Commas and question marks inside the brackets become pauses/rises.
std::vector<Phoneme> parsePhonemes(std::string_view spec);

// Human-readable names, useful for --list-phonemes and debugging.
std::vector<std::string> phonemeNames();

} // namespace formant
