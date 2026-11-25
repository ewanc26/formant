#include "formant/frication.h"

#include "formant/frication_data.h"
#include "formant/hash.h"

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
    throw std::invalid_argument("frication line " + std::to_string(line) +
                                ": " + message);
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

int partialCount(const std::string &text, std::size_t line,
                 const std::string &what) {
    char *end = nullptr;
    const long value = std::strtol(text.c_str(), &end, 10);
    if (end != text.c_str() + text.size() || value < 12 ||
        value > kMaxFricationPartials) {
        fail(line, what + " must be an integer in [12, " +
                       std::to_string(kMaxFricationPartials) + "]");
    }
    return static_cast<int>(value);
}

double frequency(const std::string &text, std::size_t line,
                 const std::string &what) {
    const double value = number(text, line, what);
    if (value < 200.0 || value > 20000.0) {
        fail(line, what + " must be in [200, 20000] Hz");
    }
    return value;
}

double bandwidth(const std::string &text, std::size_t line,
                 const std::string &what) {
    const double value = number(text, line, what);
    if (value < 100.0 || value > 12000.0) {
        fail(line, what + " must be in [100, 12000] Hz");
    }
    return value;
}

void parseSource(std::string_view text, FricationData &data,
                 std::set<std::string> &localProfiles) {
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
                parsed != kCurrentFricationSchema) {
                fail(line, "unsupported frication schema '" + value +
                               "' (expected " +
                               std::to_string(kCurrentFricationSchema) + ")");
            }
            data.schema = static_cast<int>(parsed);
        } else if (directive == "checksum") {
            std::string extra;
            if ((fields >> extra) || !isHexChecksum(value)) {
                fail(line, "checksum must be 64 hexadecimal digits");
            }
            data.checksum = lower(value);
        } else if (directive == "defaults") {
            std::string floorText;
            std::string attackText;
            if (!(fields >> floorText) || !(fields >> attackText)) {
                fail(line, "defaults needs partials floor attack-seconds");
            }
            data.defaultPartials =
                partialCount(value, line, "default partials");
            const double floor = number(floorText, line, "broadband floor");
            if (floor < 0.0 || floor > 0.5) {
                fail(line, "broadband floor must be in [0, 0.5]");
            }
            const double attack =
                number(attackText, line, "envelope attack seconds");
            if (attack < 0.0 || attack > 1.0) {
                fail(line, "envelope attack must be in [0, 1] seconds");
            }
            data.floor = floor;
            data.attackSeconds = attack;
        } else if (directive == "profile") {
            std::string partials;
            std::string centre;
            std::string width;
            std::string attackCentre;
            std::string attackWidth;
            if (!(fields >> partials) || !(fields >> centre) ||
                !(fields >> width) || !(fields >> attackCentre) ||
                !(fields >> attackWidth) || value.empty()) {
                fail(line, "profile needs a key, partials, centre, bandwidth, "
                           "attack-centre and attack-bandwidth");
            }
            if (!localProfiles.insert(value).second) {
                fail(line, "duplicate profile '" + value + "'");
            }
            FricationProfile profile;
            profile.name = value;
            profile.partials = partialCount(partials, line, "partials");
            profile.centerHz = frequency(centre, line, "centre");
            profile.bandwidthHz = bandwidth(width, line, "bandwidth");
            profile.attackCenterHz =
                frequency(attackCentre, line, "attack centre");
            profile.attackBandwidthHz =
                bandwidth(attackWidth, line, "attack bandwidth");
            data.profiles[value] = profile;
        } else {
            fail(line, "unknown directive '" + key + "'");
        }
        std::string extra;
        if (fields >> extra) {
            fail(line, "unexpected extra value '" + extra + "'");
        }
    }
}

void validate(const FricationData &data) {
    if (data.schema != kCurrentFricationSchema) {
        throw std::invalid_argument("frication table requires 'schema " +
                                    std::to_string(kCurrentFricationSchema) +
                                    "'");
    }
    if (data.profiles.empty()) {
        throw std::invalid_argument(
            "frication table requires at least one profile");
    }
    if (data.defaultPartials < 12 ||
        data.defaultPartials > kMaxFricationPartials) {
        throw std::invalid_argument("frication default partials out of range");
    }
}

} // namespace

std::string_view builtinFricationData() {
    return detail::kBuiltinFricationData;
}

std::string fricationFileChecksum(std::string_view text) {
    return sha256Hex(checksumSource(text));
}

FricationData parseFricationData(std::string_view builtin,
                                 std::string_view user, bool verifyChecksum) {
    FricationData result;
    std::set<std::string> profiles;
    parseSource(builtin, result, profiles);
    if (verifyChecksum && result.checksum &&
        *result.checksum != fricationFileChecksum(builtin)) {
        throw std::invalid_argument("frication checksum mismatch: expected " +
                                    *result.checksum + ", computed " +
                                    fricationFileChecksum(builtin));
    }
    profiles.clear();
    if (!user.empty()) {
        parseSource(user, result, profiles);
        if (verifyChecksum && result.checksum &&
            *result.checksum != fricationFileChecksum(user)) {
            throw std::invalid_argument(
                "frication checksum mismatch: expected " + *result.checksum +
                ", computed " + fricationFileChecksum(user));
        }
    }
    validate(result);
    return result;
}

FricationData loadFricationData(const std::string &path) {
    std::ifstream file(path);
    if (!file) {
        throw std::runtime_error("cannot open frication file: " + path);
    }
    std::ostringstream source;
    source << file.rdbuf();
    if (file.bad()) {
        throw std::runtime_error("cannot read frication file: " + path);
    }
    try {
        return parseFricationData(builtinFricationData(), source.str());
    } catch (const std::invalid_argument &error) {
        throw std::invalid_argument(path + ": " + error.what());
    }
}

std::string writeFricationData(const FricationData &data,
                               std::string_view profile) {
    if (!profile.empty() &&
        data.profiles.find(std::string(profile)) == data.profiles.end()) {
        throw std::invalid_argument("unknown frication profile: " +
                                    std::string(profile));
    }
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out << "# Formant frication data, generated by --dump-frication.\n"
        << "schema " << kCurrentFricationSchema << "\n"
        << "name " << (data.name.empty() ? "formant-frication" : data.name)
        << "\n"
        << "author "
        << (data.author.empty() ? "Formant contributors" : data.author) << "\n"
        << "description "
        << (data.description.empty()
                ? "Per-place frication envelopes for the unvoiced sine stack"
                : data.description)
        << "\n"
        << "license "
        << (data.license.empty() ? "AGPL-3.0-or-later" : data.license) << "\n"
        << "version " << (data.version.empty() ? "1.0.0" : data.version) << "\n"
        << "defaults " << data.defaultPartials << ' ' << format(data.floor)
        << ' ' << format(data.attackSeconds) << '\n';
    for (const auto &entry : data.profiles) {
        if (!profile.empty() && entry.first != profile) {
            continue;
        }
        const FricationProfile &item = entry.second;
        out << "profile " << entry.first << ' ' << item.partials << ' '
            << format(item.centerHz) << ' ' << format(item.bandwidthHz) << ' '
            << format(item.attackCenterHz) << ' '
            << format(item.attackBandwidthHz) << '\n';
    }
    out << "checksum " << fricationFileChecksum(out.str()) << '\n';
    return out.str();
}

const FricationProfile *findFricationProfile(const FricationData &data,
                                             std::string_view phonemeName) {
    const auto found = data.profiles.find(std::string(phonemeName));
    return found == data.profiles.end() ? nullptr : &found->second;
}

std::vector<std::string> fricationProfileNames(const FricationData &data) {
    std::vector<std::string> names;
    names.reserve(data.profiles.size());
    for (const auto &entry : data.profiles) {
        names.push_back(entry.first);
    }
    return names;
}

std::size_t applyFrication(std::vector<Phoneme> &phonemes,
                           const FricationData &data) {
    std::size_t applied = 0;
    for (Phoneme &phoneme : phonemes) {
        if (phoneme.kind != PhonemeKind::Fricative &&
            phoneme.kind != PhonemeKind::Plosive) {
            continue;
        }
        const FricationProfile *profile =
            findFricationProfile(data, phoneme.name);
        if (!profile) {
            continue;
        }
        FricationControl control;
        control.partials = profile->partials;
        control.centerHz = profile->centerHz;
        control.bandwidthHz = profile->bandwidthHz;
        control.attackCenterHz = profile->attackCenterHz;
        control.attackBandwidthHz = profile->attackBandwidthHz;
        control.attackSeconds = data.attackSeconds;
        control.floor = data.floor;
        phoneme.frication = control;
        ++applied;
    }
    return applied;
}

} // namespace formant
