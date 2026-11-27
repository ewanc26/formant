#pragma once

// Opt-in foreign-accent profiles (#135).
//
// An accent profile is a small, versioned data layer that transforms an
// already-generated phoneme stream: systematic segment substitutions, optional
// epenthesis, per-phoneme realization overrides and bounded prosody dials. It
// runs after every G2P/text front end and before frication, breathing, rhythm,
// neural, intonation and prosody-direct layers. Nothing here runs unless a
// caller selects a profile, so default speech stays byte-identical.
//
// The shipped table is data. A user table layers individual profiles over the
// built-in ones without changing code. The renderer still sees ordinary
// Phoneme values, so coarticulation and the sine-only source model are shared
// with every other voice.

#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "formant/phoneme.h"

namespace formant {

constexpr int kCurrentAccentSchema = 1;

// Replace one segment with an IPA transcription. A strength of 1 is a full
// substitution, 0 is a no-op, and an intermediate value linearly blends the
// formants of the source with a single-segment replacement while keeping the
// authored duration and pitch contour. `context` is any, initial, final,
// boundary, cluster or boundary-cluster. "initial" and "final" mean the first
// and last non-silent segment of the utterance; "boundary" is word-initial;
// "cluster" means a consonant follows in the same word; "boundary-cluster" is
// both. `replacement` keeps the authored token (`a` is the English short name,
// `/a/` forces IPA) and is resolved again when the profile is applied.
struct AccentMapping {
    std::string source;
    std::string replacement;
    double strength = 1.0;
    std::string context = "any";

    bool operator==(const AccentMapping &) const = default;
};

// Insert an IPA sequence before or after an anchor segment. `context` uses the
// same vocabulary as AccentMapping. Inserted segments carry an
// "accent:NAME" provenance tag, so the neural layer does not apply twice to a
// segment that did not come from the front end.
struct AccentInsertion {
    std::string position = "before";
    std::string anchor;
    std::string replacement;
    double strength = 1.0;
    std::string context = "any";

    bool operator==(const AccentInsertion &) const = default;
};

// Per-phoneme realization adjustment, applied to matching segments after the
// substitution and insertion passes. `formantShiftCents` scales all four band
// centres and bandwidths; the other fields are multiplicative.
struct AccentOverride {
    std::string phoneme;
    double formantShiftCents = 0.0;
    double durationScale = 1.0;
    double pitchScale = 1.0;
    double amplitudeScale = 1.0;

    bool operator==(const AccentOverride &) const = default;
};

// Bounded utterance-wide transfer of F0 mean/range, rate and amplitude. Exact
// intonation and prosody-direct targets still win later in the pipeline.
struct AccentProsody {
    double pitchMean = 1.0;
    double pitchRange = 1.0;
    double durationScale = 1.0;
    double amplitudeScale = 1.0;

    bool operator==(const AccentProsody &) const = default;
};

struct AccentProfile {
    std::string name;
    std::string sourceLanguage;
    std::string targetLanguage;
    std::string description;
    std::string rhythm;
    AccentProsody prosody;
    std::vector<AccentMapping> mappings;
    std::vector<AccentInsertion> insertions;
    std::vector<AccentOverride> overrides;

    bool operator==(const AccentProfile &) const = default;
};

struct AccentData {
    int schema = 0;
    std::map<std::string, AccentProfile> profiles;
    std::string name;
    std::string author;
    std::string description;
    std::string license;
    std::string version;
    std::optional<std::string> checksum;
};

std::string_view builtinAccentData();

// SHA-256 with the checksum directive removed and CRLF normalised to LF.
std::string accentFileChecksum(std::string_view text);

// Parse the documented line-oriented format. A user table layers individual
// profiles over the shipped ones. Throws std::invalid_argument with an
// "accent line N: ..." prefix on bad input.
AccentData parseAccentData(std::string_view builtin, std::string_view user = {},
                           bool verifyChecksum = true);

AccentData loadAccentData(const std::string &path);

// Serialise a versioned, checksummed table. With `name` set only that profile
// is emitted, layered over the common metadata.
std::string writeAccentData(const AccentData &data, std::string_view name = {});

const AccentProfile *findAccentProfile(const AccentData &data,
                                       std::string_view name);
std::vector<std::string> accentProfileNames(const AccentData &data);

// Resolve `--accent NAME`. Throws std::invalid_argument for unknown names.
AccentProfile resolveAccentProfile(const AccentData &data,
                                   std::string_view name);

// Deterministic, value-in/value-out transform. Silence, breath/NVV events,
// absolute-F0 spans and note-F0 segments are exempt.
std::vector<Phoneme> applyAccent(std::vector<Phoneme> phonemes,
                                 const AccentProfile &profile);

} // namespace formant
