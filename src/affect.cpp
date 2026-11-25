#include "formant/affect.h"

#include "formant/affect_data.h"
#include "formant/hash.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <locale>
#include <optional>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace formant {
namespace {

std::string lower(std::string value) {
    std::transform(
        value.begin(), value.end(), value.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}

[[noreturn]] void fail(std::size_t line, const std::string &message) {
    throw std::invalid_argument("affect line " + std::to_string(line) + ": " +
                                message);
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
        // Only the checksum directive itself is excluded. Comments and blank
        // lines are part of the versioned file, so editing them invalidates the
        // checksum just like editing a coefficient does.
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

std::string format(double value) {
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out << std::setprecision(17) << value;
    return out.str();
}

std::optional<std::string> findChecksum(std::string_view text) {
    std::optional<std::string> result;
    std::istringstream stream{std::string(text)};
    std::string line;
    std::size_t lineNumber = 0;
    while (std::getline(stream, line)) {
        ++lineNumber;
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        const std::size_t comment = line.find('#');
        if (comment != std::string::npos) {
            line.erase(comment);
        }
        const std::size_t first = line.find_first_not_of(" \t");
        if (first == std::string::npos) {
            continue;
        }
        std::istringstream fields(line.substr(first));
        std::string key;
        fields >> key;
        if (lower(key) != "checksum") {
            continue;
        }
        std::string value;
        std::string extra;
        if (!(fields >> value) || (fields >> extra)) {
            fail(lineNumber, "checksum needs one value");
        }
        if (result) {
            fail(lineNumber, "duplicate checksum directive");
        }
        result = lower(value);
    }
    return result;
}

double number(const std::string &text, std::size_t line,
              const std::string &what) {
    char *end = nullptr;
    const double value = std::strtod(text.c_str(), &end);
    if (end != text.c_str() + text.size() || !std::isfinite(value)) {
        fail(line, "invalid number '" + text + "' for " + what);
    }
    return value;
}

std::array<double, 3> coefficients(const std::string &a, const std::string &v,
                                   const std::string &d, std::size_t line,
                                   const std::string &what) {
    const std::array<double, 3> result = {number(a, line, what + " arousal"),
                                          number(v, line, what + " valence"),
                                          number(d, line, what + " dominance")};
    for (double value : result) {
        if (std::abs(value) > 4.0) {
            fail(line, what + " coefficient outside [-4, 4]");
        }
    }
    return result;
}

AffectAxes axes(const std::string &a, const std::string &v,
                const std::string &d, std::size_t line) {
    const AffectAxes result = {number(a, line, "arousal"),
                               number(v, line, "valence"),
                               number(d, line, "dominance")};
    if (std::abs(result.arousal) > kAffectAxisLimit ||
        std::abs(result.valence) > kAffectAxisLimit ||
        std::abs(result.dominance) > kAffectAxisLimit) {
        fail(line, "affect axes must be in [-1, 1]");
    }
    return result;
}

double dot(const std::array<double, 3> &coefficients, const AffectAxes &axes) {
    return coefficients[0] * axes.arousal + coefficients[1] * axes.valence +
           coefficients[2] * axes.dominance;
}

std::array<double, 3> *mappingField(AffectLeverMapping &mapping,
                                    const std::string &name) {
    if (name == "pitch") {
        return &mapping.pitch;
    }
    if (name == "range") {
        return &mapping.range;
    }
    if (name == "rate") {
        return &mapping.rate;
    }
    if (name == "intensity") {
        return &mapping.intensity;
    }
    if (name == "jitter") {
        return &mapping.jitter;
    }
    if (name == "shimmer") {
        return &mapping.shimmer;
    }
    if (name == "adduction") {
        return &mapping.adduction;
    }
    if (name == "effort") {
        return &mapping.effort;
    }
    return nullptr;
}

void parseSource(std::string_view text, AffectData &data,
                 std::set<std::string> &localStyles,
                 std::set<std::string> &localEmphasis) {
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
        if (name == "name" || name == "author" || name == "description" ||
            name == "license" || name == "version") {
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
                data.name = value;
            } else if (name == "author") {
                data.author = value;
            } else if (name == "description") {
                data.description = value;
            } else if (name == "license") {
                data.license = value;
            } else {
                data.version = value;
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
                parsed != kCurrentAffectSchema) {
                fail(line, "unsupported affect schema '" + value +
                               "' (expected " +
                               std::to_string(kCurrentAffectSchema) + ")");
            }
            data.schema = static_cast<int>(parsed);
        } else if (name == "checksum") {
            if (!isHexChecksum(value)) {
                fail(line, "checksum must be 64 hexadecimal digits");
            }
            data.checksum = lower(value);
        } else if (name == "range") {
            std::string maximum;
            if (!(fields >> maximum) || value != "-1" || maximum != "1") {
                fail(line, "range must be -1 1");
            }
        } else if (name == "map") {
            std::string a;
            std::string v;
            std::string d;
            if (!(fields >> a) || !(fields >> v) || !(fields >> d) ||
                value.empty()) {
                fail(line, "map needs a lever and three coefficients");
            }
            std::string field = lower(value);
            std::array<double, 3> *target = mappingField(data.mapping, field);
            if (!target) {
                fail(line, "unknown mapping lever '" + field + "'");
            }
            *target = coefficients(a, v, d, line, field);
        } else if (name == "style") {
            std::string a;
            std::string v;
            std::string d;
            if (!(fields >> a) || !(fields >> v) || !(fields >> d) ||
                value.empty()) {
                fail(line, "style needs a name and three axes");
            }
            if (!localStyles.insert(value).second) {
                fail(line, "duplicate style '" + value + "'");
            }
            AffectStyle style;
            style.name = value;
            style.axes = axes(a, v, d, line);
            data.styles[value] = style;
        } else if (name == "emphasis") {
            std::string pitch;
            std::string duration;
            std::string amplitude;
            if (!(fields >> pitch) || !(fields >> duration) ||
                !(fields >> amplitude) || value.empty()) {
                fail(line,
                     "emphasis needs a name and pitch duration amplitude");
            }
            const std::string level = lower(value);
            if (!localEmphasis.insert(level).second) {
                fail(line, "duplicate emphasis level '" + level + "'");
            }
            AffectEmphasis emphasis = {
                number(pitch, line, "emphasis pitch"),
                number(duration, line, "emphasis duration"),
                number(amplitude, line, "emphasis amplitude")};
            if (emphasis.pitch < 0.5 || emphasis.pitch > 2.0 ||
                emphasis.duration < 0.5 || emphasis.duration > 2.0 ||
                emphasis.amplitude < 0.5 || emphasis.amplitude > 2.0) {
                fail(line, "emphasis factors must be in [0.5, 2]");
            }
            data.emphasis[level] = emphasis;
        } else {
            fail(line, "unknown directive '" + key + "'");
        }
        std::string extra;
        if (fields >> extra) {
            fail(line, "unexpected extra value '" + extra + "'");
        }
    }
}

void validate(const AffectData &data) {
    if (data.schema != kCurrentAffectSchema) {
        throw std::invalid_argument("affect table requires 'schema " +
                                    std::to_string(kCurrentAffectSchema) + "'");
    }
    if (data.styles.find("neutral") == data.styles.end()) {
        throw std::invalid_argument("affect table requires a neutral style");
    }
    if (data.emphasis.empty()) {
        throw std::invalid_argument("affect table requires emphasis levels");
    }
}

double clamp(double value, double minimum, double maximum) {
    return std::min(maximum, std::max(minimum, value));
}

} // namespace

std::string_view builtinAffectData() { return detail::kBuiltinAffectData; }

std::string affectFileChecksum(std::string_view text) {
    return sha256Hex(checksumSource(text));
}

AffectData parseAffectData(std::string_view builtin, std::string_view user,
                           bool verifyChecksum) {
    AffectData result;
    std::set<std::string> styles;
    std::set<std::string> emphasis;
    parseSource(builtin, result, styles, emphasis);
    styles.clear();
    emphasis.clear();
    if (!user.empty()) {
        if (const std::optional<std::string> checksum = findChecksum(user)) {
            if (verifyChecksum && *checksum != affectFileChecksum(user)) {
                throw std::invalid_argument(
                    "affect checksum mismatch: expected " + *checksum +
                    ", computed " + affectFileChecksum(user));
            }
        }
        parseSource(user, result, styles, emphasis);
    }
    validate(result);
    return result;
}

AffectData loadAffectData(const std::string &path) {
    std::ifstream file(path);
    if (!file) {
        throw std::runtime_error("cannot open affect file: " + path);
    }
    std::ostringstream source;
    source << file.rdbuf();
    if (file.bad()) {
        throw std::runtime_error("cannot read affect file: " + path);
    }
    try {
        return parseAffectData(builtinAffectData(), source.str());
    } catch (const std::invalid_argument &error) {
        throw std::invalid_argument(path + ": " + error.what());
    }
}

std::string writeAffectData(const AffectData &data, std::string_view style) {
    if (!style.empty() &&
        data.styles.find(std::string(style)) == data.styles.end()) {
        throw std::invalid_argument("unknown affect style: " +
                                    std::string(style));
    }
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out << "# Formant affect data, generated by --dump-style.\n"
        << "schema " << kCurrentAffectSchema << "\n"
        << "name " << (data.name.empty() ? "formant-affect" : data.name) << "\n"
        << "author "
        << (data.author.empty() ? "Formant contributors" : data.author) << "\n"
        << "description "
        << (data.description.empty()
                ? "Continuous affect styles and emphasis levels"
                : data.description)
        << "\n"
        << "license "
        << (data.license.empty() ? "AGPL-3.0-or-later" : data.license) << "\n"
        << "version " << (data.version.empty() ? "1.0.0" : data.version) << "\n"
        << "range -1 1\n";
    const auto writeMapping =
        [&out](const char *name, const std::array<double, 3> &coefficients) {
            out << "map " << name << ' ' << format(coefficients[0]) << ' '
                << format(coefficients[1]) << ' ' << format(coefficients[2])
                << '\n';
        };
    writeMapping("pitch", data.mapping.pitch);
    writeMapping("range", data.mapping.range);
    writeMapping("rate", data.mapping.rate);
    writeMapping("intensity", data.mapping.intensity);
    writeMapping("jitter", data.mapping.jitter);
    writeMapping("shimmer", data.mapping.shimmer);
    writeMapping("adduction", data.mapping.adduction);
    writeMapping("effort", data.mapping.effort);
    for (const auto &entry : data.emphasis) {
        out << "emphasis " << entry.first << ' ' << format(entry.second.pitch)
            << ' ' << format(entry.second.duration) << ' '
            << format(entry.second.amplitude) << '\n';
    }
    for (const auto &entry : data.styles) {
        if (!style.empty() && entry.first != style) {
            continue;
        }
        out << "style " << entry.first << ' '
            << format(entry.second.axes.arousal) << ' '
            << format(entry.second.axes.valence) << ' '
            << format(entry.second.axes.dominance) << '\n';
    }
    out << "checksum " << affectFileChecksum(out.str()) << '\n';
    return out.str();
}

AffectProfile resolveAffectProfile(const AffectData &data,
                                   std::string_view styleName,
                                   std::optional<double> arousal,
                                   std::optional<double> valence,
                                   std::optional<double> dominance) {
    const std::string name =
        styleName.empty() ? "neutral" : std::string(styleName);
    const auto found = data.styles.find(name);
    if (found == data.styles.end()) {
        throw std::invalid_argument("unknown affect style: " + name);
    }
    AffectProfile result;
    result.name = found->second.name;
    result.axes = found->second.axes;
    if (arousal) {
        result.axes.arousal = *arousal;
    }
    if (valence) {
        result.axes.valence = *valence;
    }
    if (dominance) {
        result.axes.dominance = *dominance;
    }
    result.axes.arousal =
        clamp(result.axes.arousal, -kAffectAxisLimit, kAffectAxisLimit);
    result.axes.valence =
        clamp(result.axes.valence, -kAffectAxisLimit, kAffectAxisLimit);
    result.axes.dominance =
        clamp(result.axes.dominance, -kAffectAxisLimit, kAffectAxisLimit);
    const AffectLeverMapping &map = data.mapping;
    result.levers.pitch = clamp(1.0 + dot(map.pitch, result.axes), 0.5, 1.8);
    result.levers.range = clamp(1.0 + dot(map.range, result.axes), 0.5, 1.8);
    result.levers.rate = clamp(1.0 + dot(map.rate, result.axes), 0.5, 1.8);
    result.levers.intensity =
        clamp(1.0 + dot(map.intensity, result.axes), 0.5, 1.8);
    result.levers.jitterCents =
        clamp(std::abs(dot(map.jitter, result.axes)), 0.0, 50.0);
    result.levers.shimmer =
        clamp(std::abs(dot(map.shimmer, result.axes)), 0.0, 0.25);
    result.levers.adduction = clamp(dot(map.adduction, result.axes), -1.0, 1.0);
    result.levers.effort = clamp(dot(map.effort, result.axes), -1.0, 1.0);
    result.emphasis = data.emphasis;
    return result;
}

void applyAffectStyle(Voice &voice, const AffectProfile &profile) {
    const AffectLevers &levers = profile.levers;
    voice.pitch = clamp(voice.pitch * levers.pitch, 0.0, 2000.0);
    voice.declination = clamp(voice.declination * levers.range, 0.0, 1.0);
    voice.rate = clamp(voice.rate * levers.rate, 0.01, 5.0);
    voice.gain = clamp(voice.gain * levers.intensity, 0.001, 4.0);
    voice.jitterCents =
        clamp(voice.jitterCents + levers.jitterCents, 0.0, 50.0);
    voice.shimmerDepth = clamp(voice.shimmerDepth + levers.shimmer, 0.0, 0.25);

    if (levers.adduction >= 0.0) {
        voice.breathiness =
            clamp(voice.breathiness * (1.0 - 0.6 * levers.adduction), 0.0, 1.0);
        voice.harmonics =
            static_cast<int>(clamp(static_cast<double>(voice.harmonics) *
                                       (1.0 + 0.25 * levers.adduction),
                                   1.0, 512.0));
    } else {
        const double laxness = -levers.adduction;
        voice.breathiness =
            clamp(voice.breathiness + 0.8 * laxness * (1.0 - voice.breathiness),
                  0.0, 1.0);
        voice.harmonics = static_cast<int>(
            clamp(static_cast<double>(voice.harmonics) * (1.0 - 0.4 * laxness),
                  1.0, 512.0));
    }
    voice.formantScale =
        clamp(voice.formantScale * (1.0 + 0.1 * levers.effort), 0.25, 4.0);
    voice.attack = clamp(voice.attack * (1.0 - 0.15 * levers.effort), 0.0, 1.0);
    voice.release =
        clamp(voice.release * (1.0 + 0.15 * levers.effort), 0.0, 2.0);
}

} // namespace formant
