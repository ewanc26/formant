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
// so a file can change one knob without respecifying the whole voice.
struct VoiceDefinition {
    int schema = 0;
    std::string base = "neutral";
    std::string name;
    std::map<std::string, double> values;
    std::optional<bool> taperCeiling;
};

// Parse/load the line-oriented format documented in docs/voice-files.md.
// Throws std::invalid_argument with a "voice line N: ..." prefix for malformed
// input and values outside the hard interval.
VoiceDefinition parseVoiceDefinition(std::string_view text);
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

} // namespace formant
