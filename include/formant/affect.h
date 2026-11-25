#pragma once

// Data-defined affect and style layer (#125).
//
// A style is a point in the three-dimensional pleasure/arousal/dominance space.
// The mapping from those axes to the synthesis levers is plain, versioned data:
// no second signal model and no randomness. Everything here is opt-in; a render
// without --style or an --arousal/--valence/--dominance override is unchanged.

#include <array>
#include <map>
#include <optional>
#include <string>
#include <string_view>

#include "formant/voice.h"

namespace formant {

// Continuous dimensions, each in [-1, 1]. Zero is the neutral point.
struct AffectAxes {
    double arousal = 0.0;
    double valence = 0.0;
    double dominance = 0.0;

    bool operator==(const AffectAxes &) const = default;
};

constexpr int kCurrentAffectSchema = 1;
constexpr double kAffectAxisLimit = 1.0;

// Coefficients for one PAD axis, in order: arousal, valence, dominance.
// Scale levers evaluate as 1 + a * A + v * V + d * D; perturbation levers
// evaluate as a * A + v * V + d * D and are clamped when applied.
struct AffectLeverMapping {
    std::array<double, 3> pitch{0.12, 0.02, 0.06};
    std::array<double, 3> range{0.16, -0.02, 0.04};
    std::array<double, 3> rate{0.22, 0.0, 0.05};
    std::array<double, 3> intensity{0.14, 0.08, 0.06};
    std::array<double, 3> jitter{0.45, -0.12, 0.05};
    std::array<double, 3> shimmer{0.055, -0.02, 0.01};
    std::array<double, 3> adduction{-0.05, 0.02, 0.85};
    std::array<double, 3> effort{0.06, 0.02, 0.06};
};

// The resolved low-level levers. Scale fields are multipliers; jitterCents and
// shimmer are additive disturbances with neutral zero. Adduction/effort are
// signed and map onto breathiness, harmonics and the vocal-tract scale.
struct AffectLevers {
    double pitch = 1.0;
    double range = 1.0;
    double rate = 1.0;
    double intensity = 1.0;
    double jitterCents = 0.0;
    double shimmer = 0.0;
    double adduction = 0.0;
    double effort = 0.0;
};

// Word-level emphasis levels. All three fields multiply the existing Phoneme
// prosody fields on the emphasized word only.
struct AffectEmphasis {
    double pitch = 1.22;
    double duration = 1.30;
    double amplitude = 1.28;
};

struct AffectStyle {
    std::string name;
    AffectAxes axes;
};

// A complete, versioned style table. User files layer over the shipped table
// field by field, so a shared package can define one new style or replace the
// entire mapping without rebuilding the binary.
struct AffectData {
    int schema = 0;
    AffectLeverMapping mapping;
    std::map<std::string, AffectStyle> styles;
    std::map<std::string, AffectEmphasis> emphasis;
    std::string name;
    std::string author;
    std::string description;
    std::string license;
    std::string version;
    std::optional<std::string> checksum;
};

// A resolved style ready for rendering. Coordinates and levers are exposed so
// a harness can verify that midpoints interpolate between named presets.
struct AffectProfile {
    std::string name = "neutral";
    AffectAxes axes;
    AffectLevers levers;
    std::map<std::string, AffectEmphasis> emphasis;
};

// The shipped table, embedded at build time from data/affect/affect_styles.txt.
std::string_view builtinAffectData();

// SHA-256 with checksum directives removed and CRLF normalised to LF.
std::string affectFileChecksum(std::string_view text);

// Parses the line-oriented format documented in docs/affect-styles.md. When
// `user` is supplied it overlays the built-in table. Throws
// std::invalid_argument with an "affect line N: ..." prefix on malformed input
// and a checksum mismatch.
AffectData parseAffectData(std::string_view builtin, std::string_view user = {},
                           bool verifyChecksum = true);

// Loads a standalone user table. Built-in defaults fill in any missing mapping.
AffectData loadAffectData(const std::string &path);

// Serialises a versioned, checksummed table for sharing. With `style` set only
// that preset is emitted, layered over the common mapping and emphasis levels.
std::string writeAffectData(const AffectData &data,
                            std::string_view style = {});

// Resolves a named style and optional coordinate overrides. An empty name uses
// "neutral", so --arousal on its own has a deterministic starting point.
AffectProfile
resolveAffectProfile(const AffectData &data, std::string_view style,
                     std::optional<double> arousal = std::nullopt,
                     std::optional<double> valence = std::nullopt,
                     std::optional<double> dominance = std::nullopt);

// Applies a resolved profile to a voice in place. Values are clamped to the
// documented hard intervals, so extreme settings stay finite and artefact-free.
void applyAffectStyle(Voice &voice, const AffectProfile &profile);

} // namespace formant
