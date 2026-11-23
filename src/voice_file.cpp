#include "formant/voice_file.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <utility>
#include <vector>

namespace formant {
namespace {

constexpr int kLegacyVoiceSchema = 1;

std::string lower(std::string value);

// SHA-256 round constants, taken from FIPS 180-4. A small self-contained
// implementation keeps voice packages dependency-free and reproducible.
constexpr uint32_t kSha256K[64] = {
    0x428a2f98U, 0x71374491U, 0xb5c0fbcfU, 0xe9b5dba5U, 0x3956c25bU,
    0x59f111f1U, 0x923f82a4U, 0xab1c5ed5U, 0xd807aa98U, 0x12835b01U,
    0x243185beU, 0x550c7dc3U, 0x72be5d74U, 0x80deb1feU, 0x9bdc06a7U,
    0xc19bf174U, 0xe49b69c1U, 0xefbe4786U, 0x0fc19dc6U, 0x240ca1ccU,
    0x2de92c6fU, 0x4a7484aaU, 0x5cb0a9dcU, 0x76f988daU, 0x983e5152U,
    0xa831c66dU, 0xb00327c8U, 0xbf597fc7U, 0xc6e00bf3U, 0xd5a79147U,
    0x06ca6351U, 0x14292967U, 0x27b70a85U, 0x2e1b2138U, 0x4d2c6dfcU,
    0x53380d13U, 0x650a7354U, 0x766a0abbU, 0x81c2c92eU, 0x92722c85U,
    0xa2bfe8a1U, 0xa81a664bU, 0xc24b8b70U, 0xc76c51a3U, 0xd192e819U,
    0xd6990624U, 0xf40e3585U, 0x106aa070U, 0x19a4c116U, 0x1e376c08U,
    0x2748774cU, 0x34b0bcb5U, 0x391c0cb3U, 0x4ed8aa4aU, 0x5b9cca4fU,
    0x682e6ff3U, 0x748f82eeU, 0x78a5636fU, 0x84c87814U, 0x8cc70208U,
    0x90befffaU, 0xa4506cebU, 0xbef9a3f7U, 0xc67178f2U};

uint32_t rotateRight(uint32_t value, unsigned bits) {
    return (value >> bits) | (value << (32 - bits));
}

std::string sha256Hex(std::string_view data) {
    uint32_t state[8] = {0x6a09e667U, 0xbb67ae85U, 0x3c6ef372U, 0xa54ff53aU,
                         0x510e527fU, 0x9b05688cU, 0x1f83d9abU, 0x5be0cd19U};
    std::vector<unsigned char> bytes(data.begin(), data.end());
    const uint64_t bitLength = static_cast<uint64_t>(bytes.size()) * 8U;
    bytes.push_back(0x80U);
    while (bytes.size() % 64 != 56) {
        bytes.push_back(0U);
    }
    for (int shift = 56; shift >= 0; shift -= 8) {
        bytes.push_back(static_cast<unsigned char>(bitLength >> shift));
    }
    for (std::size_t offset = 0; offset < bytes.size(); offset += 64) {
        uint32_t words[64] = {};
        for (int i = 0; i < 16; ++i) {
            words[i] =
                (static_cast<uint32_t>(bytes[offset + i * 4]) << 24) |
                (static_cast<uint32_t>(bytes[offset + i * 4 + 1]) << 16) |
                (static_cast<uint32_t>(bytes[offset + i * 4 + 2]) << 8) |
                static_cast<uint32_t>(bytes[offset + i * 4 + 3]);
        }
        for (int i = 16; i < 64; ++i) {
            const uint32_t s0 = rotateRight(words[i - 15], 7) ^
                                rotateRight(words[i - 15], 18) ^
                                (words[i - 15] >> 3);
            const uint32_t s1 = rotateRight(words[i - 2], 17) ^
                                rotateRight(words[i - 2], 19) ^
                                (words[i - 2] >> 10);
            words[i] = words[i - 16] + s0 + words[i - 7] + s1;
        }
        uint32_t a = state[0];
        uint32_t b = state[1];
        uint32_t c = state[2];
        uint32_t d = state[3];
        uint32_t e = state[4];
        uint32_t f = state[5];
        uint32_t g = state[6];
        uint32_t h = state[7];
        for (int i = 0; i < 64; ++i) {
            const uint32_t s1 =
                rotateRight(e, 6) ^ rotateRight(e, 11) ^ rotateRight(e, 25);
            const uint32_t choose = (e & f) ^ (~e & g);
            const uint32_t temp1 = h + s1 + choose + kSha256K[i] + words[i];
            const uint32_t s0 =
                rotateRight(a, 2) ^ rotateRight(a, 13) ^ rotateRight(a, 22);
            const uint32_t majority = (a & b) ^ (a & c) ^ (b & c);
            const uint32_t temp2 = s0 + majority;
            h = g;
            g = f;
            f = e;
            e = d + temp1;
            d = c;
            c = b;
            b = a;
            a = temp1 + temp2;
        }
        state[0] += a;
        state[1] += b;
        state[2] += c;
        state[3] += d;
        state[4] += e;
        state[5] += f;
        state[6] += g;
        state[7] += h;
    }
    std::ostringstream out;
    out << std::hex << std::setfill('0');
    for (uint32_t word : state) {
        out << std::setw(8) << word;
    }
    return out.str();
}

// The checksum covers every byte of the file except the checksum directive
// itself, with CRLF normalised to LF. Comments and blank lines are included
// deliberately: changing anything in a published voice invalidates its
// checksum. Lines are emitted with a trailing LF so hand-edited files and
// generated packages hash identically.
std::string checksumSource(std::string_view text) {
    std::string normalized;
    normalized.reserve(text.size());
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (text[i] == '\r' && i + 1 < text.size() && text[i + 1] == '\n') {
            continue;
        }
        normalized.push_back(text[i]);
    }
    std::istringstream stream(normalized);
    std::string line;
    std::string source;
    while (std::getline(stream, line)) {
        const std::size_t first = line.find_first_not_of(" \t");
        if (first != std::string::npos) {
            std::istringstream fields(line.substr(first));
            std::string key;
            fields >> key;
            if (lower(key) == "checksum") {
                continue;
            }
        }
        source += line;
        source += '\n';
    }
    return source;
}

bool isHexChecksum(const std::string &value) {
    if (value.size() != 64) {
        return false;
    }
    return std::all_of(value.begin(), value.end(), [](unsigned char c) {
        return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') ||
               (c >= 'A' && c <= 'F');
    });
}

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

std::string voiceFileChecksum(std::string_view text);

VoiceDefinition parseVoiceDefinition(std::string_view text,
                                     bool verifyChecksum) {
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
        if (name == "name" || name == "base" || name == "author" ||
            name == "description" || name == "license" || name == "version") {
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
            } else if (name == "base") {
                result.base = value;
            } else if (name == "author") {
                result.author = value;
            } else if (name == "description") {
                result.description = value;
            } else if (name == "license") {
                result.license = value;
            } else {
                result.version = value;
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
            if (end != value.c_str() + value.size() ||
                (parsed != kLegacyVoiceSchema &&
                 parsed != kCurrentVoiceSchema)) {
                fail(line, "unsupported voice schema '" + value +
                               "' (expected " +
                               std::to_string(kLegacyVoiceSchema) + " or " +
                               std::to_string(kCurrentVoiceSchema) + ")");
            }
            result.schema = static_cast<int>(parsed);
        } else if (name == "checksum") {
            if (!isHexChecksum(value)) {
                fail(line, "checksum must be 64 hexadecimal digits");
            }
            result.checksum = lower(value);
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
    if (result.schema != kLegacyVoiceSchema &&
        result.schema != kCurrentVoiceSchema) {
        throw std::invalid_argument("voice definition requires 'schema " +
                                    std::to_string(kLegacyVoiceSchema) +
                                    "' or 'schema " +
                                    std::to_string(kCurrentVoiceSchema) + "'");
    }
    // Inspection parses with verification disabled so it can display whatever
    // metadata is present. Loading a voice still enforces every requirement.
    if (result.schema == kCurrentVoiceSchema && verifyChecksum) {
        if (result.name.empty() || result.author.empty() ||
            result.description.empty() || result.license.empty() ||
            result.version.empty()) {
            throw std::invalid_argument(
                "voice schema 2 requires name, author, description, license "
                "and version");
        }
        if (!result.checksum) {
            throw std::invalid_argument("voice schema 2 requires a checksum");
        }
    }
    if (verifyChecksum && result.checksum) {
        const std::string computed = voiceFileChecksum(text);
        if (*result.checksum != computed) {
            throw std::invalid_argument("voice checksum mismatch: expected " +
                                        *result.checksum + ", computed " +
                                        computed);
        }
    }
    return result;
}

std::string voiceFileChecksum(std::string_view text) {
    return sha256Hex(checksumSource(text));
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
    if (definition.schema != kLegacyVoiceSchema &&
        definition.schema != kCurrentVoiceSchema) {
        throw std::invalid_argument(
            "unsupported voice schema " + std::to_string(definition.schema) +
            " (expected " + std::to_string(kLegacyVoiceSchema) + " or " +
            std::to_string(kCurrentVoiceSchema) + ")");
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
        << "schema " << kLegacyVoiceSchema << "\n"
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

std::string writeVoicePackage(const Voice &voice,
                              const VoicePackageMetadata &metadata) {
    if (metadata.name.empty() || metadata.author.empty() ||
        metadata.description.empty() || metadata.license.empty() ||
        metadata.version.empty()) {
        throw std::invalid_argument(
            "a voice package requires name, author, description, license and "
            "version");
    }
    std::ostringstream out;
    out << "# Formant voice package, generated by --package-voice.\n"
        << "schema " << kCurrentVoiceSchema << "\n"
        << "name " << metadata.name << "\n"
        << "author " << metadata.author << "\n"
        << "description " << metadata.description << "\n"
        << "license " << metadata.license << "\n"
        << "version " << metadata.version << "\n";
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
    out << "checksum " << voiceFileChecksum(out.str()) << "\n";
    return out.str();
}

} // namespace formant
