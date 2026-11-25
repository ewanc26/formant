#pragma once

// Overtone and subharmonic register presets (#140).
//
// These are acoustic *simulations* of techniques from Mongolian and Tuvan
// throat-singing traditions, not samples or caricatures. A register is a small
// bundle of the engine's existing synthesis levers: a narrow resonance that
// emphasises one harmonic of the sine stack, and an optional second pulse at
// half the fundamental. Both live inside the additive, deterministic source
// model. Nothing here is active unless a caller opts in.
//
// The shipped table is data, not code. Presets can also be written as ordinary
// voice files because every register field is a public Voice parameter.

#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "formant/voice.h"

namespace formant {

constexpr int kCurrentRegisterSchema = 1;
constexpr int kMaxRegisterPartial = 64;

// One named register. `emphasisPartial == 0` disables the pinched resonance;
// `subharmonicMix == 0` disables the second pulse. Frequencies are Hz.
struct RegisterProfile {
    std::string name;
    int emphasisPartial = 0;
    double emphasisGain = 1.0;
    double emphasisBandwidthHz = 120.0;
    double subharmonicMix = 0.0;
    int subharmonicPartials = 0;
    std::string voqs;
    std::string description;

    bool operator==(const RegisterProfile &) const = default;
};

// A complete, versioned preset table. A user file layers individual registers
// over the shipped ones, so a shared table can retune one preset in isolation.
struct RegisterData {
    int schema = 0;
    std::map<std::string, RegisterProfile> registers;
    std::string name;
    std::string author;
    std::string description;
    std::string license;
    std::string version;
    std::optional<std::string> checksum;
};

// The shipped table, embedded at configure time from
// data/registers/register_presets.txt.
std::string_view builtinRegisterData();

// SHA-256 with the checksum directive removed and CRLF normalised to LF.
std::string registerFileChecksum(std::string_view text);

// Parse the documented line-oriented format. When `user` is supplied it layers
// over the built-in table and its checksum is verified. Throws
// std::invalid_argument with a "register line N: ..." prefix on bad input.
RegisterData parseRegisterData(std::string_view builtin,
                               std::string_view user = {},
                               bool verifyChecksum = true);

// Load a standalone user table over the embedded defaults.
RegisterData loadRegisterData(const std::string &path);

// Serialise a versioned, checksummed table. With `name` set only that entry is
// emitted, layered over the common defaults.
std::string writeRegisterData(const RegisterData &data,
                              std::string_view name = {});

// Exact, case-sensitive lookup, and the stable order used by --list-registers.
const RegisterProfile *findRegisterProfile(const RegisterData &data,
                                           std::string_view name);
std::vector<std::string> registerProfileNames(const RegisterData &data);

// Apply a named preset's numeric levers to a voice. Returns false for an
// unknown name and leaves the voice untouched. Register fields are plain Voice
// values with neutral defaults, so a resolved voice file can carry the same
// preset without any extra machinery.
bool applyRegister(Voice &voice, const RegisterData &data,
                   std::string_view name);

} // namespace formant
