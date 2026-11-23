#include "formant/voice_file.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace formant {
namespace {

constexpr int kVoiceSchema = 1;

[[noreturn]] void fail(std::size_t line, const std::string &message) {
    throw std::invalid_argument("voice line " + std::to_string(line) + ": " +
                                message);
}

std::string lower(std::string value) {
    std::transform(
        value.begin(), value.end(), value.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}

// Seventeen significant digits round-trip every finite IEEE double exactly,
// which is what makes the generated preset definitions reproduce their audio.
std::string number(double value) {
    std::ostringstream out;
    out << std::setprecision(17) << value;
    return out.str();
}

} // namespace

std::vector<VoiceParameter> voiceParameters() {
    // min/max are the hard validation bounds; safe min/max are the documented
    // neighbourhood where each knob behaves predictably.
    return {
        {"pitch", 0.0, 2000.0, 60.0, 400.0, false,
         "base fundamental frequency in Hz; 0 disables voicing"},
        {"declination", 0.0, 1.0, 0.0, 0.3, false,
         "downward F0 drift across a phrase"},
        {"rate", 0.01, 5.0, 0.5, 2.0, false, "speaking-rate multiplier"},
        {"formant-scale", 0.25, 4.0, 0.7, 1.5, false,
         "vocal-tract length multiplier applied to every formant"},
        {"vibrato-rate", 0.0, 20.0, 3.0, 8.0, false,
         "vibrato speed in Hz; 0 disables vibrato"},
        {"vibrato-depth", 0.0, 200.0, 0.0, 40.0, false,
         "vibrato depth in cents"},
        {"breathiness", 0.0, 1.0, 0.0, 0.15, false,
         "amount of deterministic sine-noise under voiced sounds"},
        {"harmonics", 0, 512, 12, 80, true,
         "number of sine partials in the glottal source"},
        {"attack", 0.0, 1.0, 0.002, 0.05, false,
         "phoneme attack ramp in seconds"},
        {"release", 0.0, 2.0, 0.002, 0.1, false,
         "phoneme release ramp in seconds"},
        {"gain", 0.001, 4.0, 0.3, 1.0, false,
         "output level before normalisation"},
    };
}

void setVoiceParameter(Voice &voice, std::string_view name, double value) {
    for (const VoiceParameter &parameter : voiceParameters()) {
        if (parameter.name != name) {
            continue;
        }
        if (!std::isfinite(value) || value < parameter.minimum ||
            value > parameter.maximum ||
            (parameter.integer && value != std::trunc(value))) {
            throw std::invalid_argument("invalid value for " + parameter.name +
                                        ": " + number(value));
        }
        if (name == "pitch")
            voice.pitch = value;
        else if (name == "declination")
            voice.declination = value;
        else if (name == "rate")
            voice.rate = value;
        else if (name == "formant-scale")
            voice.formantScale = value;
        else if (name == "vibrato-rate")
            voice.vibratoRate = value;
        else if (name == "vibrato-depth")
            voice.vibratoDepth = value;
        else if (name == "breathiness")
            voice.breathiness = value;
        else if (name == "harmonics")
            voice.harmonics = static_cast<int>(value);
        else if (name == "attack")
            voice.attack = value;
        else if (name == "release")
            voice.release = value;
        else if (name == "gain")
            voice.gain = value;
        return;
    }
    throw std::invalid_argument("unknown voice parameter: " +
                                std::string(name));
}

double voiceParameter(const Voice &voice, std::string_view name) {
    if (name == "pitch")
        return voice.pitch;
    if (name == "declination")
        return voice.declination;
    if (name == "rate")
        return voice.rate;
    if (name == "formant-scale")
        return voice.formantScale;
    if (name == "vibrato-rate")
        return voice.vibratoRate;
    if (name == "vibrato-depth")
        return voice.vibratoDepth;
    if (name == "breathiness")
        return voice.breathiness;
    if (name == "harmonics")
        return voice.harmonics;
    if (name == "attack")
        return voice.attack;
    if (name == "release")
        return voice.release;
    if (name == "gain")
        return voice.gain;
    throw std::invalid_argument("unknown voice parameter: " +
                                std::string(name));
}

VoiceDefinition parseVoiceDefinition(std::string_view text) {
    VoiceDefinition result;
    std::istringstream stream{std::string(text)};
    std::string lineText;
    std::size_t line = 0;
    while (std::getline(stream, lineText)) {
        ++line;
        if (!lineText.empty() && lineText.back() == '\r') {
            lineText.pop_back();
        }
        const std::size_t comment = lineText.find('#');
        if (comment != std::string::npos) {
            lineText.erase(comment);
        }
        const std::size_t first = lineText.find_first_not_of(" \t");
        if (first == std::string::npos) {
            continue;
        }
        std::istringstream fields(lineText.substr(first));
        std::string key;
        fields >> key;
        const std::string name = lower(key);
        if (name == "name" || name == "base") {
            std::string value;
            std::getline(fields, value);
            const std::size_t start = value.find_first_not_of(" \t");
            if (start == std::string::npos) {
                fail(line, name + " needs a value");
            }
            value = value.substr(start);
            const std::size_t end = value.find_last_not_of(" \t");
            value.erase(end + 1);
            if (name == "name") {
                result.name = value;
            } else {
                result.base = value;
            }
            continue;
        }
        std::string value;
        if (!(fields >> value)) {
            fail(line, "directive " + key + " needs a value");
        }
        if (name == "schema") {
            char *end = nullptr;
            const long parsed = std::strtol(value.c_str(), &end, 10);
            if (end != value.c_str() + value.size() || parsed != kVoiceSchema) {
                fail(line, "unsupported voice schema '" + value +
                               "' (expected " + std::to_string(kVoiceSchema) +
                               ")");
            }
            result.schema = static_cast<int>(parsed);
        } else if (name == "taper-ceiling") {
            const std::string flag = lower(value);
            if (flag == "true" || flag == "1") {
                result.taperCeiling = true;
            } else if (flag == "false" || flag == "0") {
                result.taperCeiling = false;
            } else {
                fail(line, "taper-ceiling needs true or false");
            }
        } else {
            bool known = false;
            for (const VoiceParameter &parameter : voiceParameters()) {
                if (parameter.name == name) {
                    known = true;
                    break;
                }
            }
            if (!known) {
                fail(line, "unknown parameter '" + key + "'");
            }
            char *end = nullptr;
            const double parsed = std::strtod(value.c_str(), &end);
            if (end != value.c_str() + value.size() || !std::isfinite(parsed)) {
                fail(line, "invalid number '" + value + "' for " + name);
            }
            // Validate against the hard interval now, so a bad file is
            // rejected before any audio is written.
            Voice scratch;
            try {
                setVoiceParameter(scratch, name, parsed);
            } catch (const std::invalid_argument &error) {
                fail(line, error.what());
            }
            result.values[name] = parsed;
        }
        std::string extra;
        if (fields >> extra) {
            fail(line, "unexpected extra value '" + extra + "'");
        }
    }
    if (result.schema != kVoiceSchema) {
        throw std::invalid_argument("voice definition requires 'schema " +
                                    std::to_string(kVoiceSchema) + "'");
    }
    return result;
}

VoiceDefinition loadVoiceDefinition(const std::string &path) {
    std::ifstream file(path);
    if (!file) {
        throw std::runtime_error("cannot open voice file: " + path);
    }
    std::ostringstream source;
    source << file.rdbuf();
    if (file.bad()) {
        throw std::runtime_error("cannot read voice file: " + path);
    }
    try {
        return parseVoiceDefinition(source.str());
    } catch (const std::invalid_argument &error) {
        throw std::invalid_argument(path + ": " + error.what());
    }
}

Voice resolveVoice(const VoiceDefinition &definition) {
    if (definition.schema != kVoiceSchema) {
        throw std::invalid_argument(
            "unsupported voice schema " + std::to_string(definition.schema) +
            " (expected " + std::to_string(kVoiceSchema) + ")");
    }
    Voice result = voicePreset(definition.base);
    for (const auto &entry : definition.values) {
        setVoiceParameter(result, entry.first, entry.second);
    }
    if (definition.taperCeiling) {
        result.taperCeiling = *definition.taperCeiling;
    }
    if (!definition.name.empty()) {
        result.name = definition.name;
    }
    return result;
}

std::string writeVoiceDefinition(const Voice &voice) {
    std::ostringstream out;
    out << "# Formant voice definition, generated by --dump-voice.\n"
        << "schema " << kVoiceSchema << "\n"
        << "name " << voice.name << "\n";
    bool isPreset = false;
    for (const std::string &preset : voicePresetNames()) {
        if (preset == voice.name) {
            isPreset = true;
            break;
        }
    }
    out << "base " << (isPreset ? voice.name : "neutral") << "\n";
    for (const VoiceParameter &parameter : voiceParameters()) {
        out << parameter.name << " "
            << number(voiceParameter(voice, parameter.name)) << "\n";
    }
    out << "taper-ceiling " << (voice.taperCeiling ? "true" : "false") << "\n";
    return out.str();
}

} // namespace formant
