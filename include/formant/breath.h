#pragma once

// Data-defined breath-group planning for long-form reading (#143).
//
// A breath group is a run of speech between respiratory events. The policy is
// plain data: clause boundaries, word/duration limits, depth scaling and F0
// declination are all table values, never synthesis code. Audible breaths are
// ordinary NVV phoneme sequences, so this layer adds no second signal model.
//
// Everything here is opt-in. With BreathMode::Off the input vector is copied
// unchanged, which keeps default renders byte-identical.

#include <cstddef>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "formant/phoneme.h"

namespace formant {

enum class BreathMode {
    Off,    // no planning: the exact front-end output is preserved
    Silent, // plan groups, insert pauses and declination, no audible breath
    Auto    // plan groups and insert scaled NVV breaths
};

// One resolved policy. Values are multipliers/ratios, not milliseconds, so a
// custom table can be dropped in without rebuilding synthesis code.
struct BreathGroupPolicy {
    std::size_t minWords = 4;
    std::size_t maxWords = 12;
    double maxSeconds = 5.0;
    // A group with fewer than shortWords words takes the first depth tier;
    // mediumWords and above takes the third.
    std::size_t shortWords = 4;
    std::size_t mediumWords = 8;
    // Per-group F0 target at the final voiced segment and the reset applied
    // to the first voiced segment after a breath.
    double declination = 0.965;
    double reset = 1.045;
    // Depth tiers: short < medium < long. Duration scales by these factors;
    // amplitude follows the documented 0.75 + 0.25 * depth law.
    double depthShort = 0.82;
    double depthMedium = 1.00;
    double depthLong = 1.18;
    // Pause used by Silent mode before depth scaling.
    double pauseSeconds = 0.08;
    std::vector<std::string> strongBoundaries{".", "!", "?", ";", ":"};
    std::vector<std::string> weakBoundaries{","};
    std::string eventShort = "breath-in";
    std::string eventMedium = "breath-in";
    std::string eventLong = "breath-in";
};

// A partial section. Optional fields let a style override one number without
// resetting the language values beneath it.
struct BreathOverlay {
    std::optional<std::size_t> minWords;
    std::optional<std::size_t> maxWords;
    std::optional<double> maxSeconds;
    std::optional<std::size_t> shortWords;
    std::optional<std::size_t> mediumWords;
    std::optional<double> declination;
    std::optional<double> reset;
    std::optional<double> depthShort;
    std::optional<double> depthMedium;
    std::optional<double> depthLong;
    std::optional<double> pauseSeconds;
    std::optional<std::vector<std::string>> strongBoundaries;
    std::optional<std::vector<std::string>> weakBoundaries;
    std::optional<std::string> eventShort;
    std::optional<std::string> eventMedium;
    std::optional<std::string> eventLong;
};

// Parsed tables. The default policy is always complete; language/style overlays
// are retained so resolveBreathPolicy() can layer a style over a language.
// The resolved maps are also exposed for listings and manual overrides.
struct BreathPolicySet {
    BreathGroupPolicy defaultPolicy;
    std::map<std::string, BreathGroupPolicy> languages;
    std::map<std::string, BreathGroupPolicy> styles;
    std::map<std::string, BreathOverlay> languageOverlays;
    std::map<std::string, BreathOverlay> styleOverlays;
};

// Parses the line-oriented format documented in docs/breath-groups.md. When
// `user` is supplied its sections override the built-in table field by field.
// Throws std::invalid_argument with "breath line N: ..." on malformed input.
BreathPolicySet parseBreathData(std::string_view builtin,
                                std::string_view user = {});
BreathPolicySet loadBreathData(const std::string &path);

// Accepts "off", "silent" and "auto". Throws with the offending value quoted.
BreathMode breathModeFromName(std::string_view name);

// Resolves a language tag ("en", "es", "" for a custom front end) and a named
// style. Unknown tags/styles simply use the lower layers.
BreathGroupPolicy resolveBreathPolicy(const BreathPolicySet &set,
                                      std::string_view languageTag,
                                      std::string_view style);

// The shipped table, embedded at build time from data/breath/breath_groups.txt.
std::string_view builtinBreathData();

// Supplies the segments for an event name. The CLI passes NvvLibrary::get; a
// unit test can pass a deterministic lambda instead.
using BreathEventLoader =
    std::function<std::vector<Phoneme>(std::string_view name)>;

struct BreathPlanOptions {
    BreathMode mode = BreathMode::Auto;
    std::string languageTag = "en";
    std::string style = "neutral";
};

// One measured group in the returned phoneme vector. `begin`/`end` are
// half-open phoneme indices and the seconds fields are cumulative score time,
// so a harness can align an F0 track with a group without guessing.
struct BreathGroup {
    std::size_t begin = 0;
    std::size_t end = 0;
    double startSeconds = 0.0;
    double endSeconds = 0.0;
    std::size_t words = 0;
    double seconds = 0.0;
    int depthTier = 1; // 0 short, 1 medium, 2 long
    double depthScale = 1.0;
    bool reset = false; // true when a respiratory event preceded this group
};

struct BreathPlan {
    std::vector<Phoneme> phonemes;
    std::vector<BreathGroup> groups;
    std::size_t insertedEvents = 0; // auto breaths or silent pauses
};

// Plans breath groups and returns the new phoneme vector. Explicit author
// breaths (tagged "nvv:breath-*" by NvvLibrary, or "breath:*" by a caller) are
// never duplicated. Absolute prosody-direct targets are left untouched, and
// CLI intonation/prosody files run afterwards so explicit contours still win.
BreathPlan planBreathGroups(const std::vector<Phoneme> &phonemes,
                            const BreathGroupPolicy &policy,
                            const BreathEventLoader &eventLoader,
                            const BreathPlanOptions &options = {});

} // namespace formant
