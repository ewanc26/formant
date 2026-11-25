#pragma once

#include <array>
#include <optional>
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

// A spectral zero (anti-formant): the mirror of a formant peak. `depth` is the
// fraction of the envelope removed at the centre, from 0 (no notch) to 1 (a
// total null). Nasal segments use two: a place-dependent low zero and a
// weaker upper zero.
struct SpectralZero {
    double frequency = 0.0;
    double bandwidth = 200.0;
    double depth = 0.0;

    bool operator==(const SpectralZero &) const = default;
};

// #123: optional per-segment note control for singing. `noteOffsetSeconds`
// locates this segment inside its musical note, so a note split into
// transition/sustain pieces still shares one vibrato envelope. When this field
// is set on a phoneme, the shared F0 oracle replaces the global voice vibrato
// and jitter with this note-local, delayed vibrato. Absent by default, so
// ordinary speech takes the exact same code path as before.
struct NoteF0Control {
    double noteOffsetSeconds = 0.0;
    double vibratoRateHz = 0.0;
    double vibratoExtentCents = 0.0;
    double vibratoDelaySeconds = 0.0;
    double vibratoFadeSeconds = 0.0;

    bool operator==(const NoteF0Control &) const = default;
};

// #101: optional per-segment frication descriptor. `partials` is the size of
// the deterministic unvoiced sine stack; the centre/bandwidth pair shapes its
// spectrum, evolving from the attack pair to the steady pair over the segment.
// `floor` keeps a flat broadband fraction outside the peak. Every field is data
// resolved from the versioned frication table, never from a random source.
struct FricationControl {
    int partials = 12;
    double centerHz = 0.0;
    double bandwidthHz = 0.0;
    double attackCenterHz = 0.0;
    double attackBandwidthHz = 0.0;
    double attackSeconds = 0.0;
    double floor = 0.0;

    bool operator==(const FricationControl &) const = default;
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
    // pitchStart/pitchEnd describe a linear F0 contour across the segment as
    // multipliers of the voice's base pitch.
    double amplitude = 1.0;
    double pitchStart = 1.0;
    double pitchEnd = 1.0;

    // Absolute F0 targets in Hz, used by prosody-direct override files. When
    // f0StartHz is set it replaces the voice-relative contour above with a
    // linear Hz ramp; the phrase-final rise and vibrato still apply on top.
    // Empty by default, so ordinary renders are byte-for-byte unchanged.
    std::optional<double> f0StartHz;
    std::optional<double> f0EndHz;

    // Optional anti-formants, kept last so existing positional aggregate
    // initialisers keep their meaning. Zero-depth by default, so every oral
    // phoneme and every caller-built Phoneme renders exactly as before.
    std::array<SpectralZero, 2> zeros{};

    // Optional provenance tag set by front ends (for example "nvv:laugh" or
    // "breath:auto"). Empty for ordinary speech, so equality and rendering
    // are unchanged. Front ends use it to express intent to later layers; the
    // synthesiser never reads it.
    std::string source;

    // Optional note-F0 control (#123). Added after provenance so every
    // positional aggregate initialiser keeps its original meaning.
    std::optional<NoteF0Control> noteF0;

    // #101: optional per-segment frication control. When set, the unvoiced sine
    // stack uses the authored partial count and a place-specific spectral
    // envelope that evolves from its attack shape to its steady shape. Absent
    // by default, so every ordinary render keeps the original fixed 12-partial
    // path.
    std::optional<FricationControl> frication;

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
