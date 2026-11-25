#include "formant/register.h"

#include "formant/hash.h"
#include "formant/register_data.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <locale>
#include <set>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace formant {
namespace {

std::string lower(std::string value) {
    std::transform(
        value.begin(), value.end(), value.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}

[[noreturn]] void fail(std::size_t line, const std::string &message) {
    throw std::invalid_argument("register line " + std::to_string(line) + ": " +
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

// Only the checksum directive is excluded. Comments and blank lines are part
// of the versioned file, so editing them invalidates the checksum too.
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

std::string format(double value) {
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out << std::setprecision(17) << value;
    return out.str();
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

int harmonic(const std::string &text, std::size_t line,
             const std::string &what) {
    char *end = nullptr;
    const long value = std::strtol(text.c_str(), &end, 10);
    if (end != text.c_str() + text.size() || value < 0 ||
        value > kMaxRegisterPartial) {
        fail(line, what + " must be an integer in [0, " +
                       std::to_string(kMaxRegisterPartial) + "]");
    }
    return static_cast<int>(value);
}

void parseSource(std::string_view text, RegisterData &data,
                 std::set<std::string> &local) {
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
        const std::string directive = lower(key);

        if (directive == "name" || directive == "author" ||
            directive == "description" || directive == "license" ||
            directive == "version") {
            std::string value;
            std::getline(fields, value);
            const std::size_t start = value.find_first_not_of(" \t");
            if (start == std::string::npos) {
                fail(line, directive + " needs a value");
            }
            value = value.substr(start);
            const std::size_t end = value.find_last_not_of(" \t");
            value.erase(end + 1);
            if (directive == "name") {
                data.name = value;
            } else if (directive == "author") {
                data.author = value;
            } else if (directive == "description") {
                data.description = value;
            } else if (directive == "license") {
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
        if (directive == "schema") {
            char *end = nullptr;
            const long parsed = std::strtol(value.c_str(), &end, 10);
            if (end != value.c_str() + value.size() ||
                parsed != kCurrentRegisterSchema) {
                fail(line, "unsupported register schema '" + value +
                               "' (expected " +
                               std::to_string(kCurrentRegisterSchema) + ")");
            }
            data.schema = static_cast<int>(parsed);
        } else if (directive == "checksum") {
            std::string extra;
            if ((fields >> extra) || !isHexChecksum(value)) {
                fail(line, "checksum must be 64 hexadecimal digits");
            }
            data.checksum = lower(value);
        } else if (directive == "register") {
            std::string partial;
            std::string gain;
            std::string bandwidth;
            std::string mix;
            std::string partials;
            if (!(fields >> partial) || !(fields >> gain) ||
                !(fields >> bandwidth) || !(fields >> mix) ||
                !(fields >> partials) || value.empty()) {
                fail(line, "register needs a name, partial, gain, bandwidth, "
                           "subharmonic mix and subharmonic partials");
            }
            if (!local.insert(value).second) {
                fail(line, "duplicate register '" + value + "'");
            }
            RegisterProfile profile;
            profile.name = value;
            profile.emphasisPartial =
                harmonic(partial, line, "emphasised partial");
            profile.emphasisGain = number(gain, line, "emphasis gain");
            profile.emphasisBandwidthHz =
                number(bandwidth, line, "emphasis bandwidth");
            profile.subharmonicMix = number(mix, line, "subharmonic mix");
            profile.subharmonicPartials =
                harmonic(partials, line, "subharmonic partials");
            if (profile.emphasisGain < 1.0 || profile.emphasisGain > 12.0) {
                fail(line, "emphasis gain must be in [1, 12]");
            }
            if (profile.emphasisBandwidthHz < 20.0 ||
                profile.emphasisBandwidthHz > 4000.0) {
                fail(line, "emphasis bandwidth must be in [20, 4000] Hz");
            }
            if (profile.subharmonicMix < 0.0 || profile.subharmonicMix > 1.0) {
                fail(line, "subharmonic mix must be in [0, 1]");
            }
            data.registers[value] = profile;
        } else if (directive == "voqs" || directive == "about") {
            const auto found = data.registers.find(value);
            if (found == data.registers.end()) {
                fail(line, directive + " references unknown register '" +
                               value + "'");
            }
            std::string rest;
            std::getline(fields, rest);
            const std::size_t start = rest.find_first_not_of(" \t");
            if (start == std::string::npos) {
                fail(line, directive + " needs a name and a value");
            }
            rest = rest.substr(start);
            if (directive == "voqs") {
                found->second.voqs = rest;
            } else {
                found->second.description = rest;
            }
        } else {
            fail(line, "unknown directive '" + key + "'");
        }
        std::string extra;
        if (fields >> extra && directive != "voqs" && directive != "about") {
            fail(line, "unexpected extra value '" + extra + "'");
        }
    }
}

void validate(const RegisterData &data) {
    if (data.schema != kCurrentRegisterSchema) {
        throw std::invalid_argument("register table requires 'schema " +
                                    std::to_string(kCurrentRegisterSchema) +
                                    "'");
    }
    if (data.registers.empty()) {
        throw std::invalid_argument(
            "register table requires at least one preset");
    }
    for (const auto &entry : data.registers) {
        if (entry.first.empty()) {
            throw std::invalid_argument("register preset needs a name");
        }
    }
}

} // namespace

std::string_view builtinRegisterData() { return detail::kBuiltinRegisterData; }

std::string registerFileChecksum(std::string_view text) {
    return sha256Hex(checksumSource(text));
}

RegisterData parseRegisterData(std::string_view builtin, std::string_view user,
                               bool verifyChecksum) {
    RegisterData result;
    std::set<std::string> local;
    parseSource(builtin, result, local);
    if (verifyChecksum && result.checksum &&
        *result.checksum != registerFileChecksum(builtin)) {
        throw std::invalid_argument("register checksum mismatch: expected " +
                                    *result.checksum + ", computed " +
                                    registerFileChecksum(builtin));
    }
    local.clear();
    if (!user.empty()) {
        parseSource(user, result, local);
        if (verifyChecksum && result.checksum &&
            *result.checksum != registerFileChecksum(user)) {
            throw std::invalid_argument(
                "register checksum mismatch: expected " + *result.checksum +
                ", computed " + registerFileChecksum(user));
        }
    }
    validate(result);
    return result;
}

RegisterData loadRegisterData(const std::string &path) {
    std::ifstream file(path);
    if (!file) {
        throw std::runtime_error("cannot open register file: " + path);
    }
    std::ostringstream source;
    source << file.rdbuf();
    if (file.bad()) {
        throw std::runtime_error("cannot read register file: " + path);
    }
    try {
        return parseRegisterData(builtinRegisterData(), source.str());
    } catch (const std::invalid_argument &error) {
        throw std::invalid_argument(path + ": " + error.what());
    }
}

std::string writeRegisterData(const RegisterData &data, std::string_view name) {
    if (!name.empty() &&
        data.registers.find(std::string(name)) == data.registers.end()) {
        throw std::invalid_argument("unknown register preset: " +
                                    std::string(name));
    }
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out << "# Formant register data, generated by --dump-register.\n"
        << "schema " << kCurrentRegisterSchema << "\n"
        << "name " << (data.name.empty() ? "formant-register" : data.name)
        << "\n"
        << "author "
        << (data.author.empty() ? "Formant contributors" : data.author) << "\n"
        << "description "
        << (data.description.empty()
                ? "Overtone and subharmonic register presets"
                : data.description)
        << "\n"
        << "license "
        << (data.license.empty() ? "AGPL-3.0-or-later" : data.license) << "\n"
        << "version " << (data.version.empty() ? "1.0.0" : data.version)
        << "\n";
    for (const auto &entry : data.registers) {
        if (!name.empty() && entry.first != name) {
            continue;
        }
        const RegisterProfile &item = entry.second;
        out << "register " << entry.first << ' ' << item.emphasisPartial << ' '
            << format(item.emphasisGain) << ' '
            << format(item.emphasisBandwidthHz) << ' '
            << format(item.subharmonicMix) << ' ' << item.subharmonicPartials
            << '\n';
        if (!item.voqs.empty()) {
            out << "voqs " << entry.first << ' ' << item.voqs << '\n';
        }
        if (!item.description.empty()) {
            out << "about " << entry.first << ' ' << item.description << '\n';
        }
    }
    out << "checksum " << registerFileChecksum(out.str()) << '\n';
    return out.str();
}

const RegisterProfile *findRegisterProfile(const RegisterData &data,
                                           std::string_view name) {
    const auto found = data.registers.find(std::string(name));
    return found == data.registers.end() ? nullptr : &found->second;
}

std::vector<std::string> registerProfileNames(const RegisterData &data) {
    std::vector<std::string> names;
    names.reserve(data.registers.size());
    for (const auto &entry : data.registers) {
        names.push_back(entry.first);
    }
    return names;
}

bool applyRegister(Voice &voice, const RegisterData &data,
                   std::string_view name) {
    const RegisterProfile *profile = findRegisterProfile(data, name);
    if (!profile) {
        return false;
    }
    voice.emphasisPartial = profile->emphasisPartial;
    voice.emphasisGain = profile->emphasisGain;
    voice.emphasisBandwidthHz = profile->emphasisBandwidthHz;
    voice.subharmonicMix = profile->subharmonicMix;
    voice.subharmonicPartials = profile->subharmonicPartials;
    return true;
}

} // namespace formant
