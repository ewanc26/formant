#pragma once

#include <array>
#include <cstddef>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "formant/phoneme.h"

namespace formant {

// A local F0 target for a tone label, expressed as multipliers of the voice's
// base pitch at the start and end of the target segment. Cents, semitones and
// per-language pitch ranges therefore keep working through the existing
// fundamentalAt() path; no second oscillator is introduced.
struct ToneShape {
    double start = 1.0;
    double end = 1.0;
};

// Every entry here is data, not code. Embedders may copy the default inventory
// and redefine labels, boundary tones, pause tiers or the downstep factor.
struct ToneInventory {
    std::map<std::string, ToneShape> accents;
    std::map<std::string, ToneShape> boundaries;
    // Pause duration in seconds for break indices 0..4. Index 0 means no pause.
    std::array<double, 5> breakTiers{0.0, 0.10, 0.25, 0.50, 0.80};
    // Each leading '!' on an accent label multiplies its shape by this factor.
    double downstep = 0.82;
};

// The built-in ToBI-style inventory. Returned by value so callers can edit it.
ToneInventory defaultToneInventory();

// Per-language inventories are data too: the shipping profiles start from the
// default table and adjust boundary shapes and downstep. Conlangs can supply
// an arbitrary ToneInventory through applyIntonation().
ToneInventory languageToneInventory(Language language);

// How an event selects a target. `Vowel` counts vowel/diphthong nuclei
// (PhonemeKind::Voiced) only. Voiced approximants such as `l` are not
// syllables, so they are deliberately excluded from these selectors.
enum class IntonationSelectorKind {
    Segment,   // absolute zero-based phoneme index
    Time,      // score time in seconds (sum of Phoneme::duration before rate)
    Vowel,     // zero-based vowel-nucleus index
    LastVowel, // final vowel nucleus
    AllVowels  // every vowel nucleus (used by list tunes)
};

struct IntonationSelector {
    IntonationSelectorKind kind = IntonationSelectorKind::Segment;
    std::size_t index = 0;
    double timeSeconds = 0.0;
};

enum class ToneEventKind { Accent, Boundary, Break };

// One symbolic event. `label` holds an accent or boundary label; `tier` holds
// a break index. `line` is the 1-based source line for instructive errors.
struct ToneEvent {
    ToneEventKind kind = ToneEventKind::Accent;
    IntonationSelector selector;
    std::string label;
    int tier = 0;
    std::size_t line = 0;
    // Leading '!' operators lower an accent by one downstep factor each.
    int downsteps = 0;
    // True when this event came from a named tune rather than the file. Tune
    // targets that do not exist in a short utterance are skipped, so one tune
    // can serve any length; explicit file events are strict instead.
    bool fromTune = false;
};

// A parsed annotation. A `tune NAME` line names a built-in default tune whose
// events run before the file's own events, so explicit lines override it.
struct IntonationFile {
    std::optional<std::string> tune;
    std::vector<ToneEvent> events;
    bool empty() const { return !tune.has_value() && events.empty(); }
};

// A named sequence of symbolic events, stored as pure data.
struct IntonationTune {
    std::string name;
    std::vector<ToneEvent> events;
};

// Built-in tune names for a shipping language profile. Every language ships
// statement, yesno, wh, list and exclamation tunes.
std::vector<std::string> intonationTuneNames(Language language);
const IntonationTune *findIntonationTune(Language language,
                                         std::string_view name);

// Parse/load the line-oriented annotation format. Lines may be blank or
// comments beginning with '#'. Directives:
//   tune NAME                 select a built-in default tune
//   accent SELECTOR LABEL     e.g. "accent vowel:1 L+H*"
//   boundary LABEL            e.g. "boundary L-H%" (applies to final vowel)
//   break SELECTOR N          break index 0..4 (inserts a pause tier)
// Selectors: "3", "@0.35s", "@350ms", "vowel:2", "vowel:last", "vowels".
// Throws std::invalid_argument with "intonation line N: ..." when malformed.
IntonationFile parseIntonationFile(std::string_view text);
IntonationFile loadIntonationFile(const std::string &path);

// Expand symbolic events into the phoneme contour in the parameter domain.
// Unselected segments are untouched, so untagged renders are byte-identical.
// Explicit prosody-direct contours applied afterwards win over these symbols
// because fundamentalAt() gives absolute Hz targets precedence.
void applyIntonation(std::vector<Phoneme> &phonemes, Language language,
                     const IntonationFile &file,
                     const ToneInventory &inventory = defaultToneInventory());

} // namespace formant
