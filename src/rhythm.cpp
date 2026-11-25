#include "formant/rhythm.h"

#include "formant/hash.h"
#include "formant/metrics.h"
#include "formant/rhythm_data.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iterator>
#include <locale>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
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
    throw std::invalid_argument("rhythm line " + std::to_string(line) + ": " +
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

bool validClass(const std::string &value) {
    return value == "stress" || value == "syllable" || value == "mora" ||
           value == "unspecified";
}

void parseSource(std::string_view text, RhythmData &data,
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
                parsed != kCurrentRhythmSchema) {
                fail(line, "unsupported rhythm schema '" + value +
                               "' (expected " +
                               std::to_string(kCurrentRhythmSchema) + ")");
            }
            data.schema = static_cast<int>(parsed);
        } else if (directive == "checksum") {
            std::string extra;
            if ((fields >> extra) || !isHexChecksum(value)) {
                fail(line, "checksum must be 64 hexadecimal digits");
            }
            data.checksum = lower(value);
        } else if (directive == "rhythm") {
            std::string cls;
            std::string vMinText;
            std::string vMaxText;
            std::string iMinText;
            std::string iMaxText;
            std::string strengthText;
            std::string minFactorText;
            std::string maxFactorText;
            if (!(fields >> cls) || !(fields >> vMinText) ||
                !(fields >> vMaxText) || !(fields >> iMinText) ||
                !(fields >> iMaxText) || !(fields >> strengthText) ||
                !(fields >> minFactorText) || !(fields >> maxFactorText) ||
                value.empty()) {
                fail(line,
                     "rhythm needs a name, class, vocalic nPVI band, "
                     "intervocalic rPVI band, strength and duration factors");
            }
            if (!local.insert(value).second) {
                fail(line, "duplicate rhythm '" + value + "'");
            }
            RhythmProfile profile;
            profile.name = value;
            profile.rhythmClass = cls;
            profile.vocalicNPviMin = number(vMinText, line, "vocalic nPVI min");
            profile.vocalicNPviMax = number(vMaxText, line, "vocalic nPVI max");
            profile.intervocalicRPviMin =
                number(iMinText, line, "intervocalic rPVI min");
            profile.intervocalicRPviMax =
                number(iMaxText, line, "intervocalic rPVI max");
            profile.strength = number(strengthText, line, "strength");
            profile.minFactor = number(minFactorText, line, "min factor");
            profile.maxFactor = number(maxFactorText, line, "max factor");
            if (!validClass(profile.rhythmClass)) {
                fail(line, "rhythm class must be stress, syllable, mora or "
                           "unspecified");
            }
            if (!(profile.vocalicNPviMin > 0.0) ||
                !(profile.vocalicNPviMax >= profile.vocalicNPviMin) ||
                profile.vocalicNPviMax > 200.0) {
                fail(line, "vocalic nPVI band must be positive and ordered");
            }
            if (profile.intervocalicRPviMin < 0.0 ||
                profile.intervocalicRPviMax < profile.intervocalicRPviMin ||
                profile.intervocalicRPviMax > 500.0) {
                fail(line,
                     "intervocalic rPVI band must be non-negative and ordered");
            }
            if (profile.strength < 0.0 || profile.strength > 1.0) {
                fail(line, "strength must be in [0, 1]");
            }
            if (!(profile.minFactor > 0.0) || profile.minFactor > 1.0 ||
                profile.maxFactor < 1.0 || profile.maxFactor > 4.0) {
                fail(line, "min factor must be in (0, 1] and max factor in "
                           "[1, 4]");
            }
            data.profiles[value] = profile;
        } else if (directive == "about") {
            const auto found = data.profiles.find(value);
            if (found == data.profiles.end()) {
                fail(line, "about references unknown rhythm '" + value + "'");
            }
            std::string rest;
            std::getline(fields, rest);
            const std::size_t start = rest.find_first_not_of(" \t");
            if (start == std::string::npos) {
                fail(line, "about needs a name and a value");
            }
            found->second.description = rest.substr(start);
        } else {
            fail(line, "unknown directive '" + key + "'");
        }
        std::string extra;
        if (fields >> extra && directive != "about") {
            fail(line, "unexpected extra value '" + extra + "'");
        }
    }
}

void validate(const RhythmData &data) {
    if (data.schema != kCurrentRhythmSchema) {
        throw std::invalid_argument("rhythm table requires 'schema " +
                                    std::to_string(kCurrentRhythmSchema) + "'");
    }
    if (data.profiles.empty()) {
        throw std::invalid_argument(
            "rhythm table requires at least one profile");
    }
}

struct DurationInterval {
    std::vector<std::size_t> members;
    double duration = 0.0;
};

bool rhythmEligible(const Phoneme &phoneme) {
    if (phoneme.kind == PhonemeKind::Silence || !(phoneme.duration > 0.0) ||
        !std::isfinite(phoneme.duration)) {
        return false;
    }
    if (phoneme.source.rfind("breath:", 0) == 0 ||
        phoneme.source.rfind("nvv:", 0) == 0) {
        return false;
    }
    return !phoneme.noteF0 && !phoneme.f0StartHz && !phoneme.f0EndHz;
}

bool vocalic(const Phoneme &phoneme) {
    return phoneme.kind == PhonemeKind::Voiced && phoneme.voiced;
}

std::vector<DurationInterval>
collectIntervals(const std::vector<Phoneme> &phonemes, bool wantVocalic) {
    std::vector<DurationInterval> result;
    DurationInterval current;
    bool active = false;
    bool sawVocalic = false;
    const auto flush = [&]() {
        if (active && !current.members.empty()) {
            result.push_back(current);
        }
        current = {};
        active = false;
    };

    for (std::size_t i = 0; i < phonemes.size(); ++i) {
        const Phoneme &phoneme = phonemes[i];
        if (!rhythmEligible(phoneme)) {
            flush();
            sawVocalic = false;
            continue;
        }
        const bool isVocalic = vocalic(phoneme);
        if (isVocalic == wantVocalic && (wantVocalic || sawVocalic)) {
            if (!active) {
                active = true;
            }
            current.members.push_back(i);
            current.duration += phoneme.duration;
            if (isVocalic) {
                sawVocalic = true;
            }
        } else {
            flush();
            if (isVocalic) {
                sawVocalic = true;
            }
        }
    }
    if (!wantVocalic) {
        // A trailing consonant span is not intervocalic.
        active = false;
    }
    flush();
    return result;
}

double intervalMetric(const std::vector<DurationInterval> &intervals,
                      bool isVocalic) {
    if (intervals.size() < 2) {
        return 0.0;
    }
    double sum = 0.0;
    for (std::size_t i = 1; i < intervals.size(); ++i) {
        const double first = intervals[i - 1].duration;
        const double second = intervals[i].duration;
        const double difference = std::abs(first - second);
        sum += isVocalic ? 200.0 * difference / (first + second)
                         : difference * 1000.0;
    }
    return sum / static_cast<double>(intervals.size() - 1);
}

void recompute(std::vector<DurationInterval> &intervals,
               const std::vector<Phoneme> &phonemes) {
    for (DurationInterval &interval : intervals) {
        double total = 0.0;
        for (std::size_t index : interval.members) {
            total += phonemes[index].duration;
        }
        interval.duration = total;
    }
}

void scaleInterval(const std::vector<Phoneme> &original,
                   std::vector<Phoneme> &result,
                   const DurationInterval &interval, double scale,
                   double minFactor, double maxFactor) {
    const double target =
        interval.duration * std::clamp(scale, minFactor, maxFactor);
    std::vector<double> desired;
    desired.reserve(interval.members.size());
    for (std::size_t index : interval.members) {
        const double base = original[index].duration;
        desired.push_back(
            std::clamp(base * scale, base * minFactor, base * maxFactor));
    }
    for (int pass = 0; pass < 4; ++pass) {
        double sum = 0.0;
        for (double value : desired) {
            sum += value;
        }
        if (!(sum > 0.0)) {
            return;
        }
        const double correction = target / sum;
        for (std::size_t i = 0; i < desired.size(); ++i) {
            const double base = original[interval.members[i]].duration;
            desired[i] = std::clamp(desired[i] * correction, base * minFactor,
                                    base * maxFactor);
        }
    }
    for (std::size_t i = 0; i < desired.size(); ++i) {
        result[interval.members[i]].duration = desired[i];
    }
}

void adjustAxis(std::vector<Phoneme> &result,
                const std::vector<Phoneme> &original,
                std::vector<DurationInterval> intervals, double minimum,
                double maximum, double strength, double minFactor,
                double maxFactor, bool isVocalic) {
    if (intervals.size() < 2 || !(strength > 0.0)) {
        return;
    }
    for (int iteration = 0; iteration < kMaxRhythmIterations; ++iteration) {
        const double current = intervalMetric(intervals, isVocalic);
        if (!std::isfinite(current) ||
            (current >= minimum && current <= maximum)) {
            return;
        }
        if (!(current > 0.0)) {
            // All already-equal intervals have zero PVI. A tiny alternating
            // contrast gives the deterministic gain below something to shape.
            for (std::size_t i = 0; i < intervals.size(); ++i) {
                const double seed = i % 2 == 0 ? 1.02 : 0.98;
                scaleInterval(original, result, intervals[i], seed, minFactor,
                              maxFactor);
            }
            recompute(intervals, result);
            continue;
        }
        const double target = current < minimum ? minimum : maximum;
        const double gain =
            std::clamp(1.0 + strength * (target / current - 1.0), 0.5, 2.0);
        double mean = 0.0;
        for (const DurationInterval &interval : intervals) {
            mean += interval.duration;
        }
        mean /= static_cast<double>(intervals.size());
        for (DurationInterval &interval : intervals) {
            double desired = mean + gain * (interval.duration - mean);
            if (!(desired > 0.0)) {
                desired = interval.duration * gain;
            }
            scaleInterval(original, result, interval,
                          desired / interval.duration, minFactor, maxFactor);
        }
        recompute(intervals, result);
    }
}

} // namespace

std::string_view builtinRhythmData() { return detail::kBuiltinRhythmData; }

std::string rhythmFileChecksum(std::string_view text) {
    return sha256Hex(checksumSource(text));
}

RhythmData parseRhythmData(std::string_view builtin, std::string_view user,
                           bool verifyChecksum) {
    RhythmData result;
    std::set<std::string> local;
    parseSource(builtin, result, local);
    if (verifyChecksum && result.checksum &&
        *result.checksum != rhythmFileChecksum(builtin)) {
        throw std::invalid_argument("rhythm checksum mismatch: expected " +
                                    *result.checksum + ", computed " +
                                    rhythmFileChecksum(builtin));
    }
    local.clear();
    if (!user.empty()) {
        // Checksums belong to one file, not to the merged table. Clear the
        // built-in digest so an unsigned overlay is reported honestly and a
        // bad overlay digest is checked against the overlay text.
        result.checksum.reset();
        parseSource(user, result, local);
        if (verifyChecksum && result.checksum &&
            *result.checksum != rhythmFileChecksum(user)) {
            throw std::invalid_argument("rhythm checksum mismatch: expected " +
                                        *result.checksum + ", computed " +
                                        rhythmFileChecksum(user));
        }
    }
    validate(result);
    return result;
}

RhythmData loadRhythmData(const std::string &path) {
    std::ifstream file(path);
    if (!file) {
        throw std::runtime_error("cannot open rhythm file: " + path);
    }
    std::ostringstream source;
    source << file.rdbuf();
    if (file.bad()) {
        throw std::runtime_error("cannot read rhythm file: " + path);
    }
    try {
        return parseRhythmData(builtinRhythmData(), source.str());
    } catch (const std::invalid_argument &error) {
        throw std::invalid_argument(path + ": " + error.what());
    }
}

std::string writeRhythmData(const RhythmData &data, std::string_view name) {
    if (!name.empty() &&
        data.profiles.find(std::string(name)) == data.profiles.end()) {
        throw std::invalid_argument("unknown rhythm profile: " +
                                    std::string(name));
    }
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out << "# Formant rhythm data, generated by --dump-rhythm.\n"
        << "schema " << kCurrentRhythmSchema << "\n"
        << "name " << (data.name.empty() ? "formant-rhythm" : data.name) << "\n"
        << "author "
        << (data.author.empty() ? "Formant contributors" : data.author) << "\n"
        << "description "
        << (data.description.empty() ? "Rhythm-class PVI targets"
                                     : data.description)
        << "\n"
        << "license "
        << (data.license.empty() ? "AGPL-3.0-or-later" : data.license) << "\n"
        << "version " << (data.version.empty() ? "1.0.0" : data.version)
        << "\n";
    for (const auto &entry : data.profiles) {
        if (!name.empty() && entry.first != name) {
            continue;
        }
        const RhythmProfile &item = entry.second;
        out << "rhythm " << entry.first << ' ' << item.rhythmClass << ' '
            << format(item.vocalicNPviMin) << ' ' << format(item.vocalicNPviMax)
            << ' ' << format(item.intervocalicRPviMin) << ' '
            << format(item.intervocalicRPviMax) << ' ' << format(item.strength)
            << ' ' << format(item.minFactor) << ' ' << format(item.maxFactor)
            << '\n';
        if (!item.description.empty()) {
            out << "about " << entry.first << ' ' << item.description << '\n';
        }
    }
    out << "checksum " << rhythmFileChecksum(out.str()) << '\n';
    return out.str();
}

const RhythmProfile *findRhythmProfile(const RhythmData &data,
                                       std::string_view name) {
    const auto found = data.profiles.find(std::string(name));
    return found == data.profiles.end() ? nullptr : &found->second;
}

std::vector<std::string> rhythmProfileNames(const RhythmData &data) {
    std::vector<std::string> names;
    names.reserve(data.profiles.size());
    for (const auto &entry : data.profiles) {
        names.push_back(entry.first);
    }
    return names;
}

RhythmProfile resolveRhythmProfile(const RhythmData &data,
                                   std::string_view name,
                                   std::string_view languageTag,
                                   const LanguageTypology &typology) {
    if (name != "auto") {
        const RhythmProfile *profile = findRhythmProfile(data, name);
        if (profile == nullptr) {
            throw std::invalid_argument("unknown rhythm profile '" +
                                        std::string(name) +
                                        "' (use --list-rhythms)");
        }
        return *profile;
    }
    if (typology.vocalicNPviMin && typology.vocalicNPviMax &&
        typology.intervocalicRPviMin && typology.intervocalicRPviMax) {
        RhythmProfile profile;
        profile.name =
            languageTag.empty() ? typology.rhythm : std::string(languageTag);
        profile.rhythmClass = typology.rhythm;
        profile.vocalicNPviMin = *typology.vocalicNPviMin;
        profile.vocalicNPviMax = *typology.vocalicNPviMax;
        profile.intervocalicRPviMin = *typology.intervocalicRPviMin;
        profile.intervocalicRPviMax = *typology.intervocalicRPviMax;
        profile.strength = typology.rhythmStrength.value_or(1.0);
        profile.description = "Language-file rhythm calibration";
        return profile;
    }
    if (!languageTag.empty()) {
        if (const RhythmProfile *profile =
                findRhythmProfile(data, languageTag)) {
            return *profile;
        }
    }
    if (typology.rhythm != "unspecified") {
        if (const RhythmProfile *profile =
                findRhythmProfile(data, typology.rhythm)) {
            return *profile;
        }
    }
    throw std::invalid_argument(
        "no rhythm profile for language '" +
        std::string(languageTag.empty() ? "default" : languageTag) +
        "'; set rhythm dials in the language file or use --rhythm NAME");
}

std::vector<Phoneme> applyRhythm(std::vector<Phoneme> phonemes,
                                 const RhythmProfile &profile) {
    if (!(profile.strength > 0.0)) {
        return phonemes;
    }
    const std::vector<Phoneme> original = phonemes;
    adjustAxis(phonemes, original, collectIntervals(original, true),
               profile.vocalicNPviMin, profile.vocalicNPviMax, profile.strength,
               profile.minFactor, profile.maxFactor, true);
    adjustAxis(phonemes, original, collectIntervals(original, false),
               profile.intervocalicRPviMin, profile.intervocalicRPviMax,
               profile.strength, profile.minFactor, profile.maxFactor, false);
    return phonemes;
}

} // namespace formant
