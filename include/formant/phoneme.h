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
};

// The built-in phoneme inventory, in a stable order.
const std::vector<Phoneme> &phonemeTable();

// Case-sensitive lookup of a phoneme by its short name ("ee", "th", "_").
const Phoneme *findPhoneme(std::string_view name);

// Convert text to phonemes using a small lexicon plus English letter rules.
// Unknown words fall back to deterministic grapheme-to-phoneme conversion.
std::vector<Phoneme> phonemize(std::string_view text);

// Parse a bracketed phoneme string such as "[h ee l ow]" used by the CLI.
// Commas and question marks inside the brackets become pauses/rises.
std::vector<Phoneme> parsePhonemes(std::string_view spec);

// Human-readable names, useful for --list-phonemes and debugging.
std::vector<std::string> phonemeNames();

} // namespace formant
