#include "formant/accent.h"

#include "formant/hash.h"
#include "formant/ipa.h"
#include "formant/phoneme.h"
#include "formant/accent_data.h"

#include <algorithm>
#include <array>
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

std::string lower(std::string_view value) {
    std::string result(value);
    std::transform(
        result.begin(), result.end(), result.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return result;
}

std::string trim(std::string_view value) {
    const std::size_t begin = value.find_first_not_of(" \t");
    if (begin == std::string_view::npos) {
        return {};
    }
    const std::size_t end = value.find_last_not_of(" \t");
    return std::string(value.substr(begin, end - begin + 1));
}

[[noreturn]] void fail(std::size_t line, const std::string &message) {
    throw std::invalid_argument("accent line " + std::to_string(line) + ": " +
                                message);
}

bool isHexChecksum(const std::string &value) {
    return value.size() == 64 &&
           std::all_of(value.begin(), value.end(), [](unsigned char c) {
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

void verifyChecksumState(const AccentData &data, std::string_view source,
                         const std::string &label) {
    if (!data.checksum) {
        return;
    }
    if (!isHexChecksum(*data.checksum)) {
        throw std::invalid_argument("accent " + label +
                                    " checksum must be 64 hexadecimal digits");
    }
    const std::string computed = accentFileChecksum(source);
    if (*data.checksum != computed) {
        throw std::invalid_argument("accent " + label +
                                    " checksum mismatch: expected " +
                                    *data.checksum + ", computed " + computed);
    }
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

bool validName(std::string_view value) {
    if (value.empty()) {
        return false;
    }
    return std::all_of(value.begin(), value.end(), [](unsigned char c) {
        return std::isalnum(c) || c == '-' || c == '_';
    });
}

bool validContext(std::string_view value) {
    return value == "any" || value == "initial" || value == "final" ||
           value == "boundary" || value == "cluster" ||
           value == "boundary-cluster";
}

bool validRhythm(std::string_view value) {
    return value == "stress" || value == "syllable" || value == "mora" ||
           value == "unspecified";
}

// English short names and IPA names can both appear in a phoneme stream. Fold
// the common equivalents to one stable key so a profile written for text also
// matches IPA and X-SAMPA input.
std::string canonical(std::string_view name) {
    // Keys are the engine's English short names. IPA front ends keep their IPA
    // glyph as the segment name, so fold true equivalents onto those keys.
    // A tap (ɾ) and schwa (ə) stay distinct: both are separate phonemes that a
    // profile may need to target without also changing r or uh.
    static const std::array<std::pair<std::string_view, std::string_view>, 20>
        aliases{{
            {"θ", "th"},   {"ð", "dh"},  {"ʃ", "sh"},  {"ʒ", "zh"},
            {"tʃ", "ch"},  {"dʒ", "j"},  {"ŋ", "ng"},  {"ɹ", "r"},
            {"ɪ", "i"},    {"ɛ", "e"},   {"æ", "a"},   {"ɑ", "aa"},
            {"ɒ", "ah"},   {"ʌ", "uh"},  {"ɚ", "er"},  {"ɝ", "er"},
            {"ʊ", "oo"},   {"u", "oo"},  {"ɔ", "or"},  {"ɡ", "g"},
        }};
    const std::string key = lower(name);
    for (const auto &entry : aliases) {
        if (key == entry.first) {
            return std::string(entry.second);
        }
    }
    return key;
}

std::vector<Phoneme> parseSegmentSequence(const std::string &text,
                                          std::size_t line) {
    std::vector<Phoneme> result;
    std::size_t begin = 0;
    while (begin <= text.size()) {
        const std::size_t end = text.find('+', begin);
        const std::string token = trim(std::string_view(text).substr(
            begin, end == std::string::npos ? std::string::npos
                                            : end - begin));
        if (!token.empty()) {
            if (const Phoneme *found = findPhoneme(token)) {
                result.push_back(*found);
            } else {
                std::vector<Phoneme> parsed;
                try {
                    parsed = parseIpa(token);
                } catch (const std::exception &) {
                    fail(line, "unknown phoneme '" + token + "'");
                }
                if (parsed.empty()) {
                    fail(line, "empty phoneme '" + token + "'");
                }
                for (Phoneme &item : parsed) {
                    result.push_back(std::move(item));
                }
            }
        }
        if (end == std::string::npos) {
            break;
        }
        begin = end + 1;
    }
    if (result.empty()) {
        fail(line, "phoneme sequence needs at least one segment");
    }
    return result;
}

Phoneme singleSegment(const std::string &text, std::size_t line) {
    const std::vector<Phoneme> parsed = parseSegmentSequence(text, line);
    if (parsed.size() != 1) {
        fail(line, "expected one phoneme, got " +
                       std::to_string(parsed.size()) + " in '" + text + "'");
    }
    return parsed.front();
}

void parseSource(std::string_view text, AccentData &data,
                 std::set<std::string> &local) {
    std::istringstream stream{std::string(text)};
    std::string lineText;
    std::size_t line = 0;
    bool sawSchema = false;
    AccentProfile *current = nullptr;
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
            const std::string textValue = trim(value);
            if (textValue.empty()) {
                fail(line, directive + " needs a value");
            }
            if (directive == "name") {
                data.name = textValue;
            } else if (directive == "author") {
                data.author = textValue;
            } else if (directive == "description") {
                data.description = textValue;
            } else if (directive == "license") {
                data.license = textValue;
            } else {
                data.version = textValue;
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
                parsed != kCurrentAccentSchema) {
                fail(line, "unsupported accent schema '" + value +
                               "' (expected " +
                               std::to_string(kCurrentAccentSchema) + ")");
            }
            sawSchema = true;
            data.schema = static_cast<int>(parsed);
        } else if (directive == "checksum") {
            std::string extra;
            if (fields >> extra) {
                fail(line, "checksum takes one value");
            }
            // Keep the raw value here so `PLACEHOLDER` can be inspected and
            // reported; full validation happens when checksums are verified.
            data.checksum = lower(value);
        } else if (directive == "accent") {
            std::string source;
            std::string target;
            std::string extra;
            if (!(fields >> source) || !(fields >> target) || value.empty() ||
                !validName(value) || !validName(source) ||
                !validName(target)) {
                fail(line, "accent needs a name, source language and target "
                           "language");
            }
            if (fields >> extra) {
                fail(line, "unexpected extra value '" + extra + "'");
            }
            if (!local.insert(value).second) {
                fail(line, "duplicate accent '" + value + "'");
            }
            AccentProfile profile;
            profile.name = value;
            profile.sourceLanguage = source;
            profile.targetLanguage = target;
            data.profiles[value] = std::move(profile);
            current = &data.profiles[value];
        } else if (directive == "about") {
            std::string rest;
            std::getline(fields, rest);
            const auto found = data.profiles.find(value);
            if (found == data.profiles.end()) {
                fail(line, "about references unknown accent '" + value + "'");
            }
            const std::string textValue = trim(rest);
            if (textValue.empty()) {
                fail(line, "about needs a description");
            }
            found->second.description = textValue;
        } else if (directive == "prosody") {
            std::vector<std::string> args;
            std::string arg;
            while (fields >> arg) {
                args.push_back(arg);
            }
            if (value.empty() || args.size() != 4) {
                fail(line, "prosody needs a name, F0 mean, F0 range, duration "
                           "scale and amplitude scale");
            }
            const auto found = data.profiles.find(value);
            if (found == data.profiles.end()) {
                fail(line, "prosody references unknown accent '" + value +
                               "'");
            }
            AccentProfile *profile = &found->second;
            profile->prosody.pitchMean = number(args[0], line, "F0 mean");
            profile->prosody.pitchRange = number(args[1], line, "F0 range");
            profile->prosody.durationScale =
                number(args[2], line, "duration scale");
            profile->prosody.amplitudeScale =
                number(args[3], line, "amplitude scale");
        } else if (directive == "rhythm") {
            std::string cls;
            if (!(fields >> cls) || value.empty()) {
                fail(line, "rhythm needs a name and a convention");
            }
            const auto found = data.profiles.find(value);
            if (found == data.profiles.end()) {
                fail(line, "rhythm references unknown accent '" + value +
                               "'");
            }
            if (!validRhythm(cls)) {
                fail(line, "rhythm convention must be stress, syllable, mora "
                           "or unspecified");
            }
            found->second.rhythm = cls;
        } else if (directive == "map") {
            std::string source;
            std::string replacement;
            std::string strengthText = "1";
            std::string context = "any";
            if (!(fields >> source) || !(fields >> replacement)) {
                fail(line, "map needs a name, source and replacement");
            }
            if (fields >> strengthText) {
                if (fields >> context) {
                    std::string extra;
                    if (fields >> extra) {
                        fail(line, "unexpected extra value '" + extra + "'");
                    }
                }
            }
            const auto found = data.profiles.find(value);
            if (found == data.profiles.end()) {
                fail(line, "map references unknown accent '" + value + "'");
            }
            AccentMapping mapping;
            // Match keys are canonical segment names. Replacements keep their
            // authored token and are resolved from it again at apply time: a
            // bare `a` is the English short name (/æ/), while `/a/` forces the
            // IPA open front vowel. Storing the resolved name would collapse
            // the two, because both carry the segment name "a".
            singleSegment(replacement, line);
            mapping.source = canonical(singleSegment(source, line).name);
            mapping.replacement = trim(replacement);
            mapping.strength = number(strengthText, line, "mapping strength");
            mapping.context = lower(context);
            if (mapping.strength < 0.0 || mapping.strength > 1.0) {
                fail(line, "mapping strength must be in [0, 1]");
            }
            if (!validContext(mapping.context)) {
                fail(line, "unknown context '" + mapping.context + "'");
            }
            found->second.mappings.push_back(std::move(mapping));
        } else if (directive == "insert") {
            std::string position;
            std::string anchor;
            std::string replacement;
            std::string strengthText = "1";
            std::string context = "any";
            if (!(fields >> position) || !(fields >> anchor) ||
                !(fields >> replacement)) {
                fail(line, "insert needs a name, position, anchor and "
                           "replacement");
            }
            if (fields >> strengthText) {
                if (fields >> context) {
                    std::string extra;
                    if (fields >> extra) {
                        fail(line, "unexpected extra value '" + extra + "'");
                    }
                }
            }
            const auto found = data.profiles.find(value);
            if (found == data.profiles.end()) {
                fail(line, "insert references unknown accent '" + value +
                               "'");
            }
            AccentInsertion insertion;
            insertion.position = lower(position);
            insertion.anchor = canonical(singleSegment(anchor, line).name);
            singleSegment(replacement, line);
            insertion.replacement = trim(replacement);
            insertion.strength = number(strengthText, line, "insertion strength");
            insertion.context = lower(context);
            if (insertion.position != "before" &&
                insertion.position != "after") {
                fail(line, "insertion position must be before or after");
            }
            if (insertion.strength < 0.0 || insertion.strength > 1.0) {
                fail(line, "insertion strength must be in [0, 1]");
            }
            if (!validContext(insertion.context)) {
                fail(line, "unknown context '" + insertion.context + "'");
            }
            found->second.insertions.push_back(std::move(insertion));
        } else if (directive == "override") {
            std::string phoneme;
            std::string shiftText;
            std::string durationText;
            std::string pitchText;
            std::string amplitudeText;
            if (!(fields >> phoneme) || !(fields >> shiftText) ||
                !(fields >> durationText) || !(fields >> pitchText) ||
                !(fields >> amplitudeText)) {
                fail(line, "override needs a name, phoneme, formant shift "
                           "cents, duration, pitch and amplitude scales");
            }
            const auto found = data.profiles.find(value);
            if (found == data.profiles.end()) {
                fail(line, "override references unknown accent '" + value +
                               "'");
            }
            AccentOverride override;
            override.phoneme = canonical(singleSegment(phoneme, line).name);
            override.formantShiftCents = number(shiftText, line, "formant shift");
            override.durationScale = number(durationText, line, "duration scale");
            override.pitchScale = number(pitchText, line, "pitch scale");
            override.amplitudeScale =
                number(amplitudeText, line, "amplitude scale");
            for (const auto &entry : found->second.overrides) {
                if (entry.phoneme == override.phoneme) {
                    fail(line, "duplicate override for '" + override.phoneme +
                                   "'");
                }
            }
            found->second.overrides.push_back(std::move(override));
        } else {
            fail(line, "unknown directive '" + key + "'");
        }
        (void)current;
    }
    if (!sawSchema) {

        fail(line + 1, "accent table requires 'schema " +
                           std::to_string(kCurrentAccentSchema) + "'");
    }
}

void validate(const AccentData &data) {
    if (data.schema != kCurrentAccentSchema) {
        throw std::invalid_argument("accent table requires 'schema " +
                                    std::to_string(kCurrentAccentSchema) + "'");
    }
    if (data.profiles.empty()) {
        throw std::invalid_argument("accent table requires at least one profile");
    }
    for (const auto &entry : data.profiles) {
        const AccentProfile &profile = entry.second;
        if (profile.sourceLanguage.empty() || profile.targetLanguage.empty()) {
            throw std::invalid_argument("accent '" + entry.first +
                                        "' needs source and target languages");
        }
        const double mean = profile.prosody.pitchMean;
        const double range = profile.prosody.pitchRange;
        const double duration = profile.prosody.durationScale;
        const double amplitude = profile.prosody.amplitudeScale;
        if (!(mean >= 0.5 && mean <= 2.0) || !(range >= 0.0 && range <= 3.0) ||
            !(duration >= 0.5 && duration <= 2.0) ||
            !(amplitude >= 0.0 && amplitude <= 2.0)) {
            throw std::invalid_argument("accent '" + entry.first +
                                        "' has out-of-range prosody dials");
        }
    }
}

bool eligible(const Phoneme &phoneme) {
    if (phoneme.kind == PhonemeKind::Silence || !(phoneme.duration > 0.0) ||
        !std::isfinite(phoneme.duration)) {
        return false;
    }
    if (phoneme.source.rfind("breath:", 0) == 0 ||
        phoneme.source.rfind("nvv:", 0) == 0 ||
        phoneme.source.rfind("accent:", 0) == 0) {
        return false;
    }
    return !phoneme.f0StartHz && !phoneme.f0EndHz && !phoneme.noteF0;
}

bool vowel(const Phoneme &phoneme) {
    return phoneme.kind == PhonemeKind::Voiced && phoneme.voiced;
}

std::size_t firstSpeech(const std::vector<Phoneme> &phonemes) {
    for (std::size_t i = 0; i < phonemes.size(); ++i) {
        if (eligible(phonemes[i])) {
            return i;
        }
    }
    return phonemes.size();
}

std::size_t lastSpeech(const std::vector<Phoneme> &phonemes) {
    for (std::size_t i = phonemes.size(); i > 0; --i) {
        if (eligible(phonemes[i - 1])) {
            return i - 1;
        }
    }
    return phonemes.size();
}

bool contextMatches(const std::vector<Phoneme> &phonemes, std::size_t index,
                    std::string_view context) {
    if (context == "any") {
        return true;
    }
    if (context == "initial") {
        return index == firstSpeech(phonemes);
    }
    if (context == "final") {
        return index == lastSpeech(phonemes);
    }
    if (context == "boundary") {
        if (index == firstSpeech(phonemes)) {
            return true;
        }
        return index > 0 && phonemes[index - 1].kind == PhonemeKind::Silence;
    }
    if (context == "boundary-cluster") {
        // Word-initial consonant cluster, e.g. the s of "stop" but not of
        // "best": the Spanish-style prothetic vowel only appears there.
        return contextMatches(phonemes, index, "boundary") &&
               contextMatches(phonemes, index, "cluster");
    }
    if (context == "cluster") {
        // A word gap ends the cluster: never look past a pause into the next
        // word when deciding whether to insert a vowel.
        if (index + 1 >= phonemes.size() ||
            phonemes[index + 1].kind == PhonemeKind::Silence) {
            return false;
        }
        for (std::size_t i = index + 1; i < phonemes.size(); ++i) {
            if (!eligible(phonemes[i])) {
                continue;
            }
            return !vowel(phonemes[i]);
        }
        return false;
    }
    return false;
}

void copyProsody(const Phoneme &from, Phoneme &to) {
    to.duration = from.duration;
    to.amplitude = from.amplitude;
    to.pitchStart = from.pitchStart;
    to.pitchEnd = from.pitchEnd;
    to.pitchRise = from.pitchRise;
    to.f0StartHz = from.f0StartHz;
    to.f0EndHz = from.f0EndHz;
    to.source = from.source;
    to.noteF0 = from.noteF0;
    // Frication is data from a later layer; an accent substitution must not
    // discard it if applyAccent is called after frication by an embedder.
    to.frication = from.frication;
}

FormantBand blend(const FormantBand &a, const FormantBand &b, double strength) {
    return {a.frequency + (b.frequency - a.frequency) * strength,
            a.amplitude + (b.amplitude - a.amplitude) * strength,
            a.bandwidth + (b.bandwidth - a.bandwidth) * strength};
}

void shiftBands(std::array<FormantBand, 4> &bands, double cents) {
    const double factor = std::pow(2.0, cents / 1200.0);
    for (FormantBand &band : bands) {
        band.frequency *= factor;
        band.bandwidth *= factor;
    }
}

double clamp(double value, double minimum, double maximum) {
    return std::min(maximum, std::max(minimum, value));
}

} // namespace

std::string_view builtinAccentData() { return detail::kBuiltinAccentData; }

std::string accentFileChecksum(std::string_view text) {
    return sha256Hex(checksumSource(text));
}

AccentData parseAccentData(std::string_view builtin, std::string_view user,
                           bool verifyChecksum) {
    AccentData result;
    std::set<std::string> local;
    parseSource(builtin, result, local);
    if (verifyChecksum) {
        verifyChecksumState(result, builtin, "built-in table");
    }
    local.clear();
    if (!user.empty()) {
        // Checksums belong to one file, not to the merged table.
        result.checksum.reset();
        parseSource(user, result, local);
        if (verifyChecksum) {
            verifyChecksumState(result, user, "user file");
        }
    }
    validate(result);
    return result;
}

AccentData loadAccentData(const std::string &path) {
    std::ifstream file(path);
    if (!file) {
        throw std::runtime_error("cannot open accent file: " + path);
    }
    std::ostringstream source;
    source << file.rdbuf();
    if (file.bad()) {
        throw std::runtime_error("cannot read accent file: " + path);
    }
    try {
        return parseAccentData(builtinAccentData(), source.str());
    } catch (const std::invalid_argument &error) {
        throw std::invalid_argument(path + ": " + error.what());
    }
}

std::string writeAccentData(const AccentData &data, std::string_view name) {
    if (!name.empty() &&
        data.profiles.find(std::string(name)) == data.profiles.end()) {
        throw std::invalid_argument("unknown accent profile: " +
                                    std::string(name));
    }
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out << "# Formant accent data, generated by --dump-accent.\n"
        << "schema " << kCurrentAccentSchema << "\n"
        << "name " << (data.name.empty() ? "formant-accents" : data.name)
        << "\n"
        << "author "
        << (data.author.empty() ? "Formant contributors" : data.author) << "\n"
        << "description "
        << (data.description.empty()
                ? "Foreign-accent phoneme and prosody transfer profiles"
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
        const AccentProfile &profile = entry.second;
        out << "accent " << entry.first << ' ' << profile.sourceLanguage << ' '
            << profile.targetLanguage << '\n';
        if (!profile.description.empty()) {
            out << "about " << entry.first << ' ' << profile.description
                << '\n';
        }
        const AccentProsody &prosody = profile.prosody;
        out << "prosody " << entry.first << ' ' << format(prosody.pitchMean)
            << ' ' << format(prosody.pitchRange) << ' '
            << format(prosody.durationScale) << ' '
            << format(prosody.amplitudeScale) << '\n';
        if (!profile.rhythm.empty()) {
            out << "rhythm " << entry.first << ' ' << profile.rhythm << '\n';
        }
        for (const AccentMapping &mapping : profile.mappings) {
            out << "map " << entry.first << ' ' << mapping.source << ' '
                << mapping.replacement << ' ' << format(mapping.strength)
                << ' ' << mapping.context << '\n';
        }
        for (const AccentInsertion &insertion : profile.insertions) {
            out << "insert " << entry.first << ' ' << insertion.position << ' '
                << insertion.anchor << ' ' << insertion.replacement << ' '
                << format(insertion.strength) << ' ' << insertion.context
                << '\n';
        }
        for (const AccentOverride &override : profile.overrides) {
            out << "override " << entry.first << ' ' << override.phoneme << ' '
                << format(override.formantShiftCents) << ' '
                << format(override.durationScale) << ' '
                << format(override.pitchScale) << ' '
                << format(override.amplitudeScale) << '\n';
        }
    }
    out << "checksum " << accentFileChecksum(out.str()) << '\n';
    return out.str();
}

const AccentProfile *findAccentProfile(const AccentData &data,
                                       std::string_view name) {
    const auto found = data.profiles.find(std::string(name));
    return found == data.profiles.end() ? nullptr : &found->second;
}

std::vector<std::string> accentProfileNames(const AccentData &data) {
    std::vector<std::string> names;
    names.reserve(data.profiles.size());
    for (const auto &entry : data.profiles) {
        names.push_back(entry.first);
    }
    return names;
}

AccentProfile resolveAccentProfile(const AccentData &data,
                                   std::string_view name) {
    const AccentProfile *profile = findAccentProfile(data, name);
    if (profile == nullptr) {
        throw std::invalid_argument("unknown accent profile '" +
                                    std::string(name) +
                                    "' (use --list-accents)");
    }
    return *profile;
}

std::vector<Phoneme> applyAccent(std::vector<Phoneme> phonemes,
                                 const AccentProfile &profile) {
    if (phonemes.empty()) {
        return phonemes;
    }

    // Pass 1: systematic substitutions. The first matching rule wins so a
    // profile stays a simple ordered rule list. Timing and prosody belong to
    // the authored stream; only segment quality changes here.
    std::vector<Phoneme> mapped = phonemes;
    for (std::size_t i = 0; i < mapped.size(); ++i) {
        if (!eligible(mapped[i])) {
            continue;
        }
        const std::string name = canonical(mapped[i].name);
        for (const AccentMapping &mapping : profile.mappings) {
            if (mapping.source != name ||
                !contextMatches(phonemes, i, mapping.context) ||
                !(mapping.strength > 0.0)) {
                continue;
            }
            const Phoneme source = mapped[i];
            Phoneme replacement = singleSegment(mapping.replacement, 0);
            if (mapping.strength >= 1.0) {
                copyProsody(source, replacement);
                mapped[i] = std::move(replacement);
            } else {
                // A partial map can also fade a monophthong into a diphthong
                // or back. Use the source's steady bands as its missing glide
                // endpoint so the strength dial changes the whole segment.
                const bool needsEndBands =
                    source.hasEndBands || replacement.hasEndBands;
                for (std::size_t band = 0; band < 4; ++band) {
                    mapped[i].bands[band] =
                        blend(source.bands[band], replacement.bands[band],
                              mapping.strength);
                    if (!needsEndBands) {
                        continue;
                    }
                    const FormantBand fromEnd =
                        source.hasEndBands ? source.endBands[band]
                                           : source.bands[band];
                    const FormantBand toEnd =
                        replacement.hasEndBands ? replacement.endBands[band]
                                                : replacement.bands[band];
                    mapped[i].endBands[band] =
                        blend(fromEnd, toEnd, mapping.strength);
                }
                mapped[i].hasEndBands = needsEndBands;
            }
            break;
        }
    }

    // Pass 2: epenthesis. Inserted segments carry provenance so neural prosody
    // does not learn from segments the front end never produced.
    std::vector<Phoneme> result;
    result.reserve(mapped.size());
    for (std::size_t i = 0; i < mapped.size(); ++i) {
        for (const AccentInsertion &insertion : profile.insertions) {
            if (!eligible(mapped[i]) ||
                insertion.anchor != canonical(mapped[i].name) ||
                !(insertion.strength > 0.0) ||
                !contextMatches(mapped, i, insertion.context)) {
                continue;
            }
            if (insertion.position == "before") {
                Phoneme segment = singleSegment(insertion.replacement, 0);
                segment.duration *= insertion.strength;
                segment.amplitude = mapped[i].amplitude * insertion.strength;
                segment.pitchStart = mapped[i].pitchStart;
                segment.pitchEnd = mapped[i].pitchEnd;
                segment.source = "accent:" + profile.name;
                result.push_back(std::move(segment));
            }
        }
        result.push_back(mapped[i]);
        for (const AccentInsertion &insertion : profile.insertions) {
            if (!eligible(mapped[i]) ||
                insertion.anchor != canonical(mapped[i].name) ||
                !(insertion.strength > 0.0) ||
                !contextMatches(mapped, i, insertion.context)) {
                continue;
            }
            if (insertion.position == "after") {
                Phoneme segment = singleSegment(insertion.replacement, 0);
                segment.duration *= insertion.strength;
                segment.amplitude = mapped[i].amplitude * insertion.strength;
                segment.pitchStart = mapped[i].pitchStart;
                segment.pitchEnd = mapped[i].pitchEnd;
                segment.source = "accent:" + profile.name;
                result.push_back(std::move(segment));
            }
        }
    }

    // Pass 3: isolated per-phoneme realization overrides.
    for (Phoneme &phoneme : result) {
        if (!eligible(phoneme)) {
            continue;
        }
        const std::string name = canonical(phoneme.name);
        for (const AccentOverride &override : profile.overrides) {
            if (override.phoneme != name) {
                continue;
            }
            if (override.formantShiftCents != 0.0) {
                shiftBands(phoneme.bands, override.formantShiftCents);
                if (phoneme.hasEndBands) {
                    shiftBands(phoneme.endBands, override.formantShiftCents);
                }
            }
            phoneme.duration = clamp(phoneme.duration * override.durationScale,
                                     0.01, 2.0);
            phoneme.pitchStart =
                clamp(phoneme.pitchStart * override.pitchScale, 0.2, 3.0);
            phoneme.pitchEnd =
                clamp(phoneme.pitchEnd * override.pitchScale, 0.2, 3.0);
            phoneme.amplitude =
                clamp(phoneme.amplitude * override.amplitudeScale, 0.0, 2.0);
        }
    }

    // Pass 4: bounded prosody transfer. Exact intonation and prosody-direct
    // files still run later and win. Segments this profile inserted take the
    // same transfer as their anchor so an epenthetic vowel does not keep the
    // anchor's pre-transfer F0 and leave a pitch step.
    // Neutral dials skip the pass entirely: re-deriving start/end pitch from
    // centre and half-range is not bit-exact, and a profile that only maps
    // segments must leave every other field byte-identical.
    const AccentProsody &prosody = profile.prosody;
    if (prosody == AccentProsody{}) {
        return result;
    }
    const std::string inserted = "accent:" + profile.name;
    for (Phoneme &phoneme : result) {
        if (!eligible(phoneme) && phoneme.source != inserted) {
            continue;
        }
        const double center =
            (phoneme.pitchStart + phoneme.pitchEnd) * 0.5 * prosody.pitchMean;
        const double halfRange =
            (phoneme.pitchEnd - phoneme.pitchStart) * 0.5 *
            prosody.pitchRange;
        phoneme.pitchStart = clamp(center - halfRange, 0.2, 3.0);
        phoneme.pitchEnd = clamp(center + halfRange, 0.2, 3.0);
        phoneme.duration =
            clamp(phoneme.duration * prosody.durationScale, 0.01, 2.0);
        phoneme.amplitude =
            clamp(phoneme.amplitude * prosody.amplitudeScale, 0.0, 2.0);
    }
    return result;
}

} // namespace formant
