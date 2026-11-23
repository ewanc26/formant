#pragma once

#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "formant/voice.h"

namespace formant {

// One tunable synthesis parameter. The safe interval is the documented
// neighbourhood where the parameter behaves predictably; the hard interval is
// what the loader accepts. Everything here is data, so --list-voice-parameters
// and the tuning cookbook describe exactly the values the loader enforces.
struct VoiceParameter {
    std::string name;
    double minimum = 0.0;
    double maximum = 0.0;
    double safeMinimum = 0.0;
    double safeMaximum = 0.0;
    bool integer = false;
    std::string description;
};

// Stable, ordered table of every numeric Voice parameter.
std::vector<VoiceParameter> voiceParameters();

// A parsed voice definition. Unset parameters keep the base preset's value,
// so a file can change one knob without respecifying the whole voice. The
// metadata fields are populated by distributable schema 2 packages; schema 1
// files leave them empty.
struct VoiceDefinition {
    int schema = 0;
    std::string base = "neutral";
    std::string name;
    std::map<std::string, double> values;
    std::optional<bool> taperCeiling;
    std::string author;
    std::string description;
    std::string license;
    std::string version;
    std::optional<std::string> checksum;
};

// Current schema for distributable voice packages. Schema 1 files remain
// readable; schema 2 adds required attribution metadata and a checksum.
constexpr int kCurrentVoiceSchema = 2;

// Attributive data stored in a distributable voice package (schema 2). All
// fields are required when writing a package.
struct VoicePackageMetadata {
    std::string name;
    std::string author;
    std::string description;
    std::string license;
    std::string version;
};

// SHA-256 of the file with its checksum line removed (CRLF normalised to LF),
// as 64 lowercase hex digits. Applied to a complete package it recomputes the
// stored checksum, which is how --inspect-voice verifies integrity.
std::string voiceFileChecksum(std::string_view text);

// Parse/load the line-oriented format documented in docs/voice-files.md.
// Throws std::invalid_argument with a "voice line N: ..." prefix for malformed
// input and values outside the hard interval. With verifyChecksum false the
// package requirements and any checksum mismatch are left for the caller to
// report, which is what --inspect-voice uses to display status instead of
// failing on a file it was asked to describe.
VoiceDefinition parseVoiceDefinition(std::string_view text,
                                     bool verifyChecksum = true);
VoiceDefinition loadVoiceDefinition(const std::string &path);

// Layer a definition over its base preset. Every built-in preset resolves to
// itself, so a definition that sets all parameters round-trips byte-exactly.
Voice resolveVoice(const VoiceDefinition &definition);

// Set/read one parameter by its table name. Throws for an unknown name, a
// non-integral value for an integer parameter, or a value outside the hard
// interval.
void setVoiceParameter(Voice &voice, std::string_view name, double value);
double voiceParameter(const Voice &voice, std::string_view name);

// Serialise a voice as a complete definition file (base + every parameter),
// used by --dump-voice to make the built-in presets round-trippable data.
std::string writeVoiceDefinition(const Voice &voice);

// Serialise a voice as a distributable schema 2 package: full attribution,
// every parameter, and a trailing checksum line. All metadata fields must be
// non-empty. Used by --package-voice to prepare voices for sharing.
std::string writeVoicePackage(const Voice &voice,
                              const VoicePackageMetadata &metadata);

} // namespace formant
