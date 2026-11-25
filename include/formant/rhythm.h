#pragma once

// Opt-in rhythm-class duration data (#144).
//
// A rhythm profile is a small, versioned set of target bands for the
// Pairwise Variability Index (PVI) of vocalic and intervocalic intervals. It
// only changes phoneme durations on the same deterministic schedule used by
// rendering; it never touches phoneme identity, formants, F0 or the sine
// source model. Nothing here runs unless a caller selects a profile.
//
// The shipped table is data. Language files can declare the same numeric
// dials, so conlangs carry their rhythm calibration alongside their phonology.

#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "formant/language.h"
#include "formant/phoneme.h"

namespace formant {

constexpr int kCurrentRhythmSchema = 1;
constexpr int kMaxRhythmIterations = 12;

struct RhythmProfile {
    std::string name;
    std::string rhythmClass;
    double vocalicNPviMin = 0.0;
    double vocalicNPviMax = 0.0;
    double intervocalicRPviMin = 0.0;
    double intervocalicRPviMax = 0.0;
    double strength = 1.0;
    double minFactor = 0.6;
    double maxFactor = 1.8;
    std::string description;

    bool operator==(const RhythmProfile &) const = default;
};

struct RhythmData {
    int schema = 0;
    std::map<std::string, RhythmProfile> profiles;
    std::string name;
    std::string author;
    std::string description;
    std::string license;
    std::string version;
    std::optional<std::string> checksum;
};

std::string_view builtinRhythmData();

// SHA-256 with the checksum directive removed and CRLF normalised to LF.
std::string rhythmFileChecksum(std::string_view text);

// Parse the documented line-oriented format. A user table layers individual
// profiles over the shipped ones. Throws std::invalid_argument with a
// "rhythm line N: ..." prefix on bad input.
RhythmData parseRhythmData(std::string_view builtin, std::string_view user = {},
                           bool verifyChecksum = true);

RhythmData loadRhythmData(const std::string &path);

// Serialise a versioned, checksummed table. With `name` set only that profile
// is emitted, layered over the common metadata.
std::string writeRhythmData(const RhythmData &data, std::string_view name = {});

const RhythmProfile *findRhythmProfile(const RhythmData &data,
                                       std::string_view name);
std::vector<std::string> rhythmProfileNames(const RhythmData &data);

// Resolve `--rhythm NAME`. `auto` prefers a tag-specific built-in profile,
// then numeric dials in the language typology, then the generic class profile.
// Throws std::invalid_argument for an unknown or unresolvable selection.
RhythmProfile resolveRhythmProfile(const RhythmData &data,
                                   std::string_view name,
                                   std::string_view languageTag,
                                   const LanguageTypology &typology);

// Deterministic duration pass. Returns a copy; the input is never mutated.
// Silence, breath/NVV events, absolute-F0 and note-F0 segments are exempt.
std::vector<Phoneme> applyRhythm(std::vector<Phoneme> phonemes,
                                 const RhythmProfile &profile);

} // namespace formant
