#pragma once

// Data-defined frication quality (#101).
//
// The unvoiced part of the voice is a fixed stack of deterministic sine
// partials. This layer keeps that source model but makes the stack *data*: how
// many partials it holds, and a per-place spectral envelope that shapes them
// with a centre frequency and bandwidth. The envelope evolves from an attack
// shape to its steady shape over the segment, and the following phoneme's
// formants already blend in through the shared renderer crossfade.
//
// Everything here is opt-in. A render without --frication builds no control
// objects at all and stays byte-for-byte identical to previous releases.

#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "formant/phoneme.h"

namespace formant {

constexpr int kCurrentFricationSchema = 1;
constexpr int kMaxFricationPartials = 128;

// One place-of-articulation envelope. `partials` is the size of the
// deterministic sine stack, `centerHz`/`bandwidthHz` the steady spectral peak,
// and the `attack*` fields the peak at the start of the segment. `floor` is the
// flat broadband fraction that remains outside the peak.
struct FricationProfile {
    std::string name;
    int partials = 0;
    double centerHz = 0.0;
    double bandwidthHz = 0.0;
    double attackCenterHz = 0.0;
    double attackBandwidthHz = 0.0;

    bool operator==(const FricationProfile &) const = default;
};

// A complete, versioned profile table. A user file layers individual profiles
// over the shipped table, so a shared package can retune one sound without
// replacing the rest.
struct FricationData {
    int schema = 0;
    int defaultPartials = 48;
    double floor = 0.02;
    double attackSeconds = 0.012;
    std::map<std::string, FricationProfile> profiles;
    std::string name;
    std::string author;
    std::string description;
    std::string license;
    std::string version;
    std::optional<std::string> checksum;
};

// The shipped table, embedded at configure time from
// data/frication/frication_profiles.txt.
std::string_view builtinFricationData();

// SHA-256 with the checksum directive removed and CRLF normalised to LF.
std::string fricationFileChecksum(std::string_view text);

// Parses the documented line-oriented format. When `user` is supplied it layers
// over the built-in table and its checksum is verified. Throws
// std::invalid_argument with a "frication line N: ..." prefix on malformed
// input.
FricationData parseFricationData(std::string_view builtin,
                                 std::string_view user = {},
                                 bool verifyChecksum = true);

// Loads a standalone user table over the embedded defaults.
FricationData loadFricationData(const std::string &path);

// Serialises a versioned, checksummed table. With `profile` set only that entry
// is emitted, layered over the common defaults.
std::string writeFricationData(const FricationData &data,
                               std::string_view profile = {});

// Exact, case-sensitive lookup by phoneme short name (ASCII or UTF-8 IPA).
const FricationProfile *findFricationProfile(const FricationData &data,
                                             std::string_view phonemeName);

// Stable profile names, useful for --list-frication.
std::vector<std::string> fricationProfileNames(const FricationData &data);

// Attaches a resolved control to fricative and affricate segments. Segments
// without a matching profile are left untouched, so mixed input stays safe.
// Returns the number of segments that gained a control.
std::size_t applyFrication(std::vector<Phoneme> &phonemes,
                           const FricationData &data);

} // namespace formant
