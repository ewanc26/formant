#include "formant/song.h"

#include "formant/hash.h"
#include "formant/song_data.h"
#include "sine_kernel.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <locale>
#include <numeric>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace formant {
namespace {

constexpr double kPi = 3.14159265358979323846;

std::string lower(std::string value) {
    std::transform(
        value.begin(), value.end(), value.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}

std::string upper(std::string value) {
    std::transform(
        value.begin(), value.end(), value.begin(),
        [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    return value;
}

std::string trimmed(std::string value) {
    const std::size_t first = value.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) {
        return {};
    }
    const std::size_t last = value.find_last_not_of(" \t\r\n");
    return value.substr(first, last - first + 1);
}

// A '#' only starts a comment at the beginning of a token. Scale degree names
// legitimately contain '#' ("C#"), so stripping the first '#' would silently
// rewrite data. This keeps C# intact while still allowing trailing comments.
std::size_t commentStart(const std::string &line) {
    for (std::size_t i = 0; i < line.size(); ++i) {
        if (line[i] != '#') {
            continue;
        }
        if (i == 0 || line[i - 1] == ' ' || line[i - 1] == '\t') {
            return i;
        }
    }
    return std::string::npos;
}

bool endsWith(const std::string &value, const std::string &suffix) {
    return value.size() >= suffix.size() &&
           value.compare(value.size() - suffix.size(), suffix.size(), suffix) ==
               0;
}

double clamp01(double value) { return std::max(0.0, std::min(1.0, value)); }

double smooth(double t) {
    t = clamp01(t);
    return t * t * (3.0 - 2.0 * t);
}

std::string format(double value) {
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out << std::setprecision(17) << value;
    return out.str();
}

[[noreturn]] void failSong(std::size_t line, const std::string &message) {
    throw std::invalid_argument("song line " + std::to_string(line) + ": " +
                                message);
}

[[noreturn]] void failScale(std::size_t line, const std::string &message) {
    throw std::invalid_argument("scale line " + std::to_string(line) + ": " +
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

std::string checksumSource(std::string_view text, std::string_view key) {
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
            std::string directive;
            fields >> directive;
            if (lower(directive) == key) {
                continue;
            }
        }
        source += line;
        source += '\n';
    }
    return source;
}

std::optional<std::string>
findChecksum(std::string_view text, std::string_view key, std::size_t line,
             void (*failure)(std::size_t, const std::string &)) {
    std::optional<std::string> result;
    std::istringstream stream{std::string(text)};
    std::string lineText;
    std::size_t lineNumber = 0;
    while (std::getline(stream, lineText)) {
        ++lineNumber;
        if (!lineText.empty() && lineText.back() == '\r') {
            lineText.pop_back();
        }
        const std::size_t comment = commentStart(lineText);
        if (comment != std::string::npos) {
            lineText.erase(comment);
        }
        const std::size_t first = lineText.find_first_not_of(" \t");
        if (first == std::string::npos) {
            continue;
        }
        std::istringstream fields(lineText.substr(first));
        std::string directive;
        fields >> directive;
        if (lower(directive) != key) {
            continue;
        }
        std::string value;
        std::string extra;
        if (!(fields >> value) || (fields >> extra)) {
            failure(lineNumber, std::string(key) + " checksum needs one value");
        }
        if (!isHexChecksum(value)) {
            failure(lineNumber, std::string(key) +
                                    " checksum must be 64 hexadecimal digits");
        }
        if (result) {
            failure(lineNumber, "duplicate checksum directive");
        }
        result = lower(value);
    }
    (void)line;
    return result;
}

double number(const std::string &text, std::size_t line,
              const std::string &what,
              void (*failure)(std::size_t, const std::string &)) {
    char *end = nullptr;
    const double value = std::strtod(text.c_str(), &end);
    if (end != text.c_str() + text.size() || !std::isfinite(value)) {
        failure(line, "invalid number '" + text + "' for " + what);
    }
    return value;
}

double seconds(const std::string &text, std::size_t line,
               const std::string &what,
               void (*failure)(std::size_t, const std::string &)) {
    std::string value = lower(text);
    double scale = 1.0;
    if (endsWith(value, "ms")) {
        value = value.substr(0, value.size() - 2);
        scale = 0.001;
    } else if (endsWith(value, "s")) {
        value = value.substr(0, value.size() - 1);
    } else if (endsWith(value, "c") &&
               what.find("cents") != std::string::npos) {
        value = value.substr(0, value.size() - 1);
    }
    const double parsed =
        number(value, line, what + " (in " + text + ")", failure);
    return parsed * scale;
}

std::vector<std::string> splitFields(const std::string &line) {
    std::istringstream fields(line);
    std::vector<std::string> result;
    std::string value;
    while (fields >> value) {
        result.push_back(value);
    }
    return result;
}

std::vector<std::string> splitPhonemes(const std::string &value,
                                       std::size_t line) {
    std::vector<std::string> result;
    std::size_t start = 0;
    while (start <= value.size()) {
        const std::size_t dash = value.find('-', start);
        const std::string name = value.substr(
            start, dash == std::string::npos ? dash : dash - start);
        if (name.empty()) {
            failSong(line, "empty phoneme name in note sequence '" + value +
                               "' (use '-' between names)");
        }
        if (findPhoneme(name) == nullptr) {
            failSong(line, "unknown phoneme '" + name +
                               "' (use built-in phoneme table names)");
        }
        result.push_back(name);
        if (dash == std::string::npos) {
            break;
        }
        start = dash + 1;
    }
    return result;
}

std::pair<std::string, double> stripCentsSuffix(const std::string &token,
                                                std::size_t line) {
    std::string core = token;
    double offset = 0.0;
    if (core.size() >= 2 && core.back() == 'c') {
        const std::size_t sign = core.find_last_of("+-", core.size() - 2);
        // "cents:-1200" carries its own sign; do not mistake it for an offset.
        const bool ownSign = lower(core).rfind("cents:", 0) == 0;
        if (sign != std::string::npos && sign > 0 && core[sign - 1] != ':' &&
            !ownSign) {
            offset = number(core.substr(sign, core.size() - sign - 1), line,
                            "pitch offset", failSong);
            core = core.substr(0, sign);
        }
    }
    return {core, offset};
}

double noteCentsFromA4(const std::string &token, const SongScale &scale,
                       std::size_t line) {
    const auto [core, offset] = stripCentsSuffix(token, line);
    const std::string value = lower(core);

    if (value.rfind("cents:", 0) == 0) {
        const double cents =
            number(value.substr(6), line, "cents pitch", failSong);
        if (std::abs(cents) > 120000.0) {
            failSong(line, "cents pitch outside [-120000, 120000]");
        }
        return cents + offset;
    }

    if (value.rfind("midi:", 0) == 0) {
        const double midi =
            number(value.substr(5), line, "MIDI note", failSong);
        if (midi != std::trunc(midi) || midi < 0.0 || midi > 127.0) {
            failSong(line, "MIDI note must be an integer in [0, 127]");
        }
        return 100.0 * (midi - 69.0) + offset;
    }

    if (value.rfind("degree:", 0) == 0) {
        std::string indexText = value.substr(7);
        int octave = 4;
        const std::size_t at = indexText.find('@');
        if (at != std::string::npos) {
            octave = static_cast<int>(number(indexText.substr(at + 1), line,
                                             "degree octave", failSong));
            indexText = indexText.substr(0, at);
            if (octave < 0 || octave > 8) {
                failSong(line, "degree octave must be in [0, 8]");
            }
        }
        const double index = number(indexText, line, "degree index", failSong);
        if (index != std::trunc(index) || index < 0.0 ||
            index >= static_cast<double>(scale.degrees.size())) {
            failSong(line, "degree index out of range for scale '" +
                               scale.name + "'");
        }
        const SongScaleDegree &degree =
            scale.degrees[static_cast<std::size_t>(index)];
        return degree.cents - scale.referenceCents +
               static_cast<double>(octave - 4) * scale.periodCents + offset;
    }

    if (!core.empty() && std::all_of(core.begin(), core.end(), [](char c) {
            return std::isdigit(static_cast<unsigned char>(c)) != 0;
        })) {
        const double midi = number(core, line, "MIDI note", failSong);
        if (midi < 0.0 || midi > 127.0) {
            failSong(line, "MIDI note must be in [0, 127]");
        }
        return 100.0 * (midi - 69.0) + offset;
    }

    if (core.empty() || core.size() > 32 ||
        std::isalpha(static_cast<unsigned char>(core[0])) == 0) {
        failSong(line, "invalid pitch '" + token +
                           "' (use C4, midi:69, 69, cents:1200 or degree:0)");
    }

    // Degree names are data and may be multi-character ("Aq", "B#q"), so the
    // octave is the trailing run of digits and the longest matching scale name
    // wins. This keeps enhancement fallbacks working without baking the
    // 12-tone spelling convention into the parser.
    std::size_t octaveStart = core.size();
    while (octaveStart > 0 && std::isdigit(static_cast<unsigned char>(
                                  core[octaveStart - 1])) != 0) {
        --octaveStart;
    }
    if (octaveStart == 0) {
        failSong(line, "pitch '" + token + "' has no note name");
    }
    const int octave =
        octaveStart == core.size()
            ? 4
            : static_cast<int>(number(core.substr(octaveStart), line,
                                      "pitch octave", failSong));
    if (octave < 0 || octave > 8) {
        failSong(line, "pitch octave must be in [0, 8]");
    }

    std::string name = upper(core.substr(0, octaveStart));
    auto findDegree =
        [&scale](const std::string &wanted) -> const SongScaleDegree * {
        const SongScaleDegree *match = nullptr;
        for (const SongScaleDegree &degree : scale.degrees) {
            if (upper(degree.name) != wanted) {
                continue;
            }
            if (match == nullptr || degree.name.size() > match->name.size()) {
                match = &degree;
            }
        }
        return match;
    };

    const SongScaleDegree *degree = findDegree(name);
    if (degree == nullptr && name.size() == 2 && name[1] == 'B') {
        // Enharmonic fallback: Db -> C#, Eb -> D#, and so on. A scale that
        // genuinely spells a flat degree still matches above.
        static const std::array<std::string, 7> naturals = {"C", "D", "E", "F",
                                                            "G", "A", "B"};
        static const std::array<std::string, 7> sharpOfFlat = {
            "B", "C#", "D#", "E", "F#", "G#", "A#"};
        const auto found = std::find(naturals.begin(), naturals.end(),
                                     std::string(1, name[0]));
        if (found != naturals.end()) {
            const std::size_t at =
                static_cast<std::size_t>(found - naturals.begin());
            name = sharpOfFlat[at];
            degree = findDegree(name);
        }
    }
    if (degree == nullptr) {
        failSong(line, "note name '" + name + "' is not defined by scale '" +
                           scale.name + "'");
    }
    return degree->cents - scale.referenceCents +
           static_cast<double>(octave - 4) * scale.periodCents + offset;
}

std::vector<std::string> dataLines(std::string_view text, std::size_t &line) {
    std::vector<std::string> result;
    std::istringstream stream{std::string(text)};
    std::string lineText;
    line = 0;
    while (std::getline(stream, lineText)) {
        ++line;
        if (!lineText.empty() && lineText.back() == '\r') {
            lineText.pop_back();
        }
        const std::size_t comment = commentStart(lineText);
        if (comment != std::string::npos) {
            lineText.erase(comment);
        }
        if (trimmed(lineText).empty()) {
            continue;
        }
        result.push_back(lineText);
    }
    return result;
}

} // namespace

double songVibratoCents(const NoteF0Control &control, double noteTimeSeconds) {
    if (!(control.vibratoRateHz > 0.0) || !(control.vibratoExtentCents > 0.0) ||
        !(control.vibratoFadeSeconds >= 0.0) ||
        noteTimeSeconds <= control.vibratoDelaySeconds) {
        return 0.0;
    }
    const double since = noteTimeSeconds - control.vibratoDelaySeconds;
    const double envelope = control.vibratoFadeSeconds > 0.0
                                ? smooth(since / control.vibratoFadeSeconds)
                                : 1.0;
    if (envelope <= 0.0) {
        return 0.0;
    }
    return control.vibratoExtentCents *
           detail::sine(2.0 * kPi * control.vibratoRateHz * since) * envelope;
}

std::string songChecksumSource(std::string_view text) {
    return checksumSource(text, "checksum");
}

std::string songFileChecksum(std::string_view text) {
    return sha256Hex(songChecksumSource(text));
}

std::string songScaleChecksumSource(std::string_view text) {
    return checksumSource(text, "checksum");
}

std::string songScaleChecksum(std::string_view text) {
    return sha256Hex(songScaleChecksumSource(text));
}

SongFile parseSongFile(std::string_view text, bool verifyChecksum) {
    SongFile result;
    std::size_t line = 0;
    const std::vector<std::string> lines = dataLines(text, line);
    // dataLines() resets line to the count; recover source line numbers by
    // walking the original text again below.
    (void)lines;
    std::istringstream stream{std::string(text)};
    std::string lineText;
    std::size_t lineNumber = 0;
    bool sawSchema = false;
    while (std::getline(stream, lineText)) {
        ++lineNumber;
        if (!lineText.empty() && lineText.back() == '\r') {
            lineText.pop_back();
        }
        const std::size_t comment = commentStart(lineText);
        if (comment != std::string::npos) {
            lineText.erase(comment);
        }
        const std::vector<std::string> fields = splitFields(lineText);
        if (fields.empty()) {
            continue;
        }
        const std::string directive = lower(fields[0]);
        if (directive == "schema") {
            if (fields.size() != 2) {
                failSong(lineNumber, "schema needs one value");
            }
            result.schema = static_cast<int>(
                number(fields[1], lineNumber, "song schema", failSong));
            if (result.schema != kCurrentSongSchema) {
                failSong(lineNumber, "unsupported song schema '" + fields[1] +
                                         "' (expected 1)");
            }
            sawSchema = true;
        } else if (directive == "name" || directive == "scale") {
            if (fields.size() < 2) {
                failSong(lineNumber, directive + " needs a value");
            }
            std::string value = fields[1];
            for (std::size_t i = 2; i < fields.size(); ++i) {
                value += " " + fields[i];
            }
            if (directive == "name") {
                result.name = value;
            } else {
                result.scale = value;
            }
        } else if (directive == "a4") {
            if (fields.size() != 2) {
                failSong(lineNumber, "a4 needs one value");
            }
            result.a4Hz =
                number(fields[1], lineNumber, "A4 reference", failSong);
            if (!(result.a4Hz >= 200.0 && result.a4Hz <= 1000.0)) {
                failSong(lineNumber, "A4 reference must be in [200, 1000] Hz");
            }
        } else if (directive == "note") {
            if (fields.size() < 4) {
                failSong(lineNumber,
                         "note needs PITCH DURATION PHONEMES [options]");
            }
            SongNote note;
            note.line = lineNumber;
            note.pitch = fields[1];
            note.durationSeconds =
                seconds(fields[2], lineNumber, "duration", failSong);
            if (!(note.durationSeconds > 0.0) || note.durationSeconds > 60.0) {
                failSong(lineNumber,
                         "note duration must be in (0, 60] seconds");
            }
            note.phonemes = fields[3];
            (void)splitPhonemes(note.phonemes, lineNumber);
            bool sawGlide = false;
            bool sawTransition = false;
            for (std::size_t i = 4; i < fields.size(); ++i) {
                const std::string option = lower(fields[i]);
                const std::size_t equals = option.find('=');
                if (equals == std::string::npos) {
                    failSong(lineNumber, "expected key=value option, got '" +
                                             fields[i] + "'");
                }
                const std::string key = option.substr(0, equals);
                const std::string value = fields[i].substr(equals + 1);
                if (key == "transition") {
                    if (value == "legato") {
                        note.transition = SongTransitionKind::Legato;
                        if (!sawGlide) {
                            note.glideSeconds = 0.012;
                        }
                    } else if (value == "portamento") {
                        note.transition = SongTransitionKind::Portamento;
                        if (!sawGlide) {
                            note.glideSeconds = 0.090;
                        }
                    } else {
                        failSong(lineNumber, "transition must be legato or "
                                             "portamento");
                    }
                    sawTransition = true;
                } else if (key == "glide") {
                    note.glideSeconds =
                        seconds(value, lineNumber, "glide", failSong);
                    sawGlide = true;
                } else if (key == "overshoot") {
                    note.overshootCents =
                        seconds(value, lineNumber, "overshoot cents", failSong);
                } else if (key == "preparation") {
                    note.preparationCents = seconds(
                        value, lineNumber, "preparation cents", failSong);
                } else if (key == "vibrato") {
                    std::vector<std::string> parts;
                    std::size_t start = 0;
                    while (true) {
                        const std::size_t colon = value.find(':', start);
                        parts.push_back(value.substr(
                            start, colon == std::string::npos ? colon
                                                              : colon - start));
                        if (colon == std::string::npos) {
                            break;
                        }
                        start = colon + 1;
                    }
                    if (parts.size() != 4 ||
                        std::any_of(parts.begin(), parts.end(),
                                    [](const std::string &part) {
                                        return part.empty();
                                    })) {
                        failSong(lineNumber,
                                 "vibrato needs rate:extent:delay:fade");
                    }
                    note.vibrato.rateHz =
                        number(parts[0], lineNumber, "vibrato rate", failSong);
                    note.vibrato.extentCents = seconds(
                        parts[1], lineNumber, "vibrato extent cents", failSong);
                    note.vibrato.delaySeconds = seconds(
                        parts[2], lineNumber, "vibrato delay", failSong);
                    note.vibrato.fadeSeconds =
                        seconds(parts[3], lineNumber, "vibrato fade", failSong);
                } else {
                    failSong(lineNumber, "unknown note option '" + key + "'");
                }
            }
            (void)sawTransition;
            if (!(note.glideSeconds >= 0.0 && note.glideSeconds <= 10.0)) {
                failSong(lineNumber, "glide must be in [0, 10] seconds");
            }
            if (std::abs(note.overshootCents) > 200.0 ||
                std::abs(note.preparationCents) > 200.0) {
                failSong(lineNumber, "overshoot and preparation must be within "
                                     "[-200, 200] cents");
            }
            if (note.vibrato.rateHz < 0.0 || note.vibrato.rateHz > 20.0 ||
                note.vibrato.extentCents < 0.0 ||
                note.vibrato.extentCents > 200.0 ||
                note.vibrato.delaySeconds < 0.0 ||
                note.vibrato.delaySeconds > 30.0 ||
                note.vibrato.fadeSeconds < 0.0 ||
                note.vibrato.fadeSeconds > 10.0) {
                failSong(lineNumber,
                         "vibrato parameters are outside their safe ranges");
            }
            result.notes.push_back(std::move(note));
        } else if (directive == "checksum") {
            if (fields.size() != 2 || !isHexChecksum(fields[1])) {
                failSong(lineNumber, "checksum must be 64 hexadecimal digits");
            }
            if (result.checksum) {
                failSong(lineNumber, "duplicate checksum directive");
            }
            result.checksum = lower(fields[1]);
        } else {
            failSong(lineNumber, "unknown directive '" + fields[0] + "'");
        }
    }
    if (!sawSchema) {
        failSong(1, "song is missing 'schema 1'");
    }
    if (result.notes.empty()) {
        failSong(lineNumber, "song has no notes");
    }
    const std::optional<std::string> checksum =
        findChecksum(text, "checksum", lineNumber, failSong);
    if (checksum && verifyChecksum && *checksum != songFileChecksum(text)) {
        failSong(lineNumber, "song checksum mismatch: expected " + *checksum +
                                 ", computed " + songFileChecksum(text));
    }
    if (checksum) {
        result.checksum = checksum;
    }
    return result;
}

SongFile loadSongFile(const std::string &path) {
    std::ifstream file(path);
    if (!file) {
        throw std::runtime_error("cannot open song file: " + path);
    }
    std::ostringstream source;
    source << file.rdbuf();
    if (file.bad()) {
        throw std::runtime_error("cannot read song file: " + path);
    }
    try {
        return parseSongFile(source.str());
    } catch (const std::invalid_argument &error) {
        throw std::invalid_argument(path + ": " + error.what());
    }
}

SongScale parseSongScale(std::string_view text, bool verifyChecksum) {
    SongScale result;
    std::istringstream stream{std::string(text)};
    std::string lineText;
    std::size_t lineNumber = 0;
    bool sawSchema = false;
    while (std::getline(stream, lineText)) {
        ++lineNumber;
        if (!lineText.empty() && lineText.back() == '\r') {
            lineText.pop_back();
        }
        const std::size_t comment = commentStart(lineText);
        if (comment != std::string::npos) {
            lineText.erase(comment);
        }
        const std::vector<std::string> fields = splitFields(lineText);
        if (fields.empty()) {
            continue;
        }
        const std::string directive = lower(fields[0]);
        if (directive == "schema") {
            if (fields.size() != 2) {
                failScale(lineNumber, "schema needs one value");
            }
            result.schema = static_cast<int>(
                number(fields[1], lineNumber, "scale schema", failScale));
            if (result.schema != kCurrentSongScaleSchema) {
                failScale(lineNumber, "unsupported scale schema '" + fields[1] +
                                          "' (expected 1)");
            }
            sawSchema = true;
        } else if (directive == "name") {
            if (fields.size() < 2) {
                failScale(lineNumber, "name needs a value");
            }
            std::string value = fields[1];
            for (std::size_t i = 2; i < fields.size(); ++i) {
                value += " " + fields[i];
            }
            result.name = value;
        } else if (directive == "period") {
            if (fields.size() != 2) {
                failScale(lineNumber, "period needs one value");
            }
            result.periodCents =
                number(fields[1], lineNumber, "scale period", failScale);
            if (!(result.periodCents >= 1.0 && result.periodCents <= 10000.0)) {
                failScale(lineNumber, "period must be in [1, 10000] cents");
            }
        } else if (directive == "reference") {
            if (fields.size() != 2) {
                failScale(lineNumber, "reference needs one value");
            }
            result.referenceCents =
                number(fields[1], lineNumber, "reference", failScale);
        } else if (directive == "degree") {
            if (fields.size() < 2 || fields.size() > 3) {
                failScale(lineNumber, "degree needs [NAME] CENTS");
            }
            SongScaleDegree degree;
            degree.line = lineNumber;
            if (fields.size() == 3) {
                degree.name = upper(fields[1]);
                degree.cents =
                    number(fields[2], lineNumber, "degree cents", failScale);
            } else {
                degree.cents =
                    number(fields[1], lineNumber, "degree cents", failScale);
            }
            if (!(degree.cents >= 0.0 && degree.cents < result.periodCents)) {
                failScale(lineNumber, "degree cents must be in [0, period)");
            }
            if (!result.degrees.empty() &&
                degree.cents <= result.degrees.back().cents) {
                failScale(lineNumber,
                          "degree cents must be strictly increasing");
            }
            if (!degree.name.empty()) {
                for (const SongScaleDegree &other : result.degrees) {
                    if (upper(other.name) == degree.name) {
                        failScale(lineNumber, "duplicate degree name '" +
                                                  degree.name + "'");
                    }
                }
            }
            result.degrees.push_back(std::move(degree));
        } else if (directive == "checksum") {
            if (fields.size() != 2 || !isHexChecksum(fields[1])) {
                failScale(lineNumber, "checksum must be 64 hexadecimal digits");
            }
            if (result.checksum) {
                failScale(lineNumber, "duplicate checksum directive");
            }
            result.checksum = lower(fields[1]);
        } else {
            failScale(lineNumber, "unknown directive '" + fields[0] + "'");
        }
    }
    if (!sawSchema) {
        failScale(1, "scale is missing 'schema 1'");
    }
    if (result.degrees.empty()) {
        failScale(lineNumber, "scale has no degrees");
    }
    if (!(result.referenceCents >= 0.0 &&
          result.referenceCents < result.periodCents)) {
        failScale(lineNumber, "reference must be in [0, period) cents");
    }
    const std::optional<std::string> checksum =
        findChecksum(text, "checksum", lineNumber, failScale);
    if (checksum && verifyChecksum && *checksum != songScaleChecksum(text)) {
        failScale(lineNumber, "scale checksum mismatch: expected " + *checksum +
                                  ", computed " + songScaleChecksum(text));
    }
    if (checksum) {
        result.checksum = checksum;
    }
    return result;
}

SongScale loadSongScale(const std::string &path) {
    std::ifstream file(path);
    if (!file) {
        throw std::runtime_error("cannot open scale file: " + path);
    }
    std::ostringstream source;
    source << file.rdbuf();
    if (file.bad()) {
        throw std::runtime_error("cannot read scale file: " + path);
    }
    try {
        return parseSongScale(source.str());
    } catch (const std::invalid_argument &error) {
        throw std::invalid_argument(path + ": " + error.what());
    }
}

std::string writeSongFile(const SongFile &song) {
    if (song.notes.empty()) {
        throw std::invalid_argument("cannot write a song with no notes");
    }
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out << "# formant song, generated by --dump-song\n"
        << "schema " << kCurrentSongSchema << "\n"
        << "name " << song.name << "\n"
        << "a4 " << format(song.a4Hz) << "\n"
        << "scale " << song.scale << "\n";
    for (const SongNote &note : song.notes) {
        out << "note " << note.pitch << ' ' << format(note.durationSeconds)
            << ' ' << note.phonemes;
        if (note.transition == SongTransitionKind::Portamento) {
            out << " transition=portamento";
        } else {
            out << " transition=legato";
        }
        if (note.glideSeconds > 0.0) {
            out << " glide=" << format(note.glideSeconds) << "s";
        }
        if (note.overshootCents != 0.0) {
            out << " overshoot=" << format(note.overshootCents) << "c";
        }
        if (note.preparationCents != 0.0) {
            out << " preparation=" << format(note.preparationCents) << "c";
        }
        if (note.vibrato.rateHz > 0.0 && note.vibrato.extentCents > 0.0) {
            out << " vibrato=" << format(note.vibrato.rateHz) << ':'
                << format(note.vibrato.extentCents)
                << "c:" << format(note.vibrato.delaySeconds)
                << "s:" << format(note.vibrato.fadeSeconds) << 's';
        }
        out << '\n';
    }
    out << "checksum " << songFileChecksum(out.str()) << '\n';
    return out.str();
}

std::string writeSongScale(const SongScale &scale) {
    if (scale.degrees.empty()) {
        throw std::invalid_argument("cannot write a scale with no degrees");
    }
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out << "# formant scale, generated by --dump-scale\n"
        << "schema " << kCurrentSongScaleSchema << "\n"
        << "name " << scale.name << "\n"
        << "period " << format(scale.periodCents) << "\n"
        << "reference " << format(scale.referenceCents) << "\n";
    for (const SongScaleDegree &degree : scale.degrees) {
        out << "degree";
        if (!degree.name.empty()) {
            out << ' ' << degree.name;
        }
        out << ' ' << format(degree.cents) << '\n';
    }
    out << "checksum " << songScaleChecksum(out.str()) << '\n';
    return out.str();
}

std::vector<std::string> builtinSongNames() { return {"twinkle"}; }

std::string_view builtinSongData(std::string_view name) {
    if (name == "twinkle") {
        return detail::kBuiltinTwinkleSong;
    }
    return {};
}

std::vector<std::string> builtinScaleNames() {
    return {"chromatic", "quarter"};
}

std::string_view builtinScaleData(std::string_view name) {
    if (name == "chromatic") {
        return detail::kBuiltinChromaticScale;
    }
    if (name == "quarter") {
        return detail::kBuiltinQuarterScale;
    }
    return {};
}

std::vector<Phoneme> phonemizeSong(const SongFile &song, const SongScale &scale,
                                   double a4OverrideHz) {
    if (song.notes.empty()) {
        throw std::invalid_argument("song has no notes to render");
    }
    const double a4 = a4OverrideHz > 0.0 ? a4OverrideHz : song.a4Hz;
    if (!(a4 >= 200.0 && a4 <= 1000.0)) {
        throw std::invalid_argument("A4 reference must be in [200, 1000] Hz");
    }

    std::vector<double> targets;
    targets.reserve(song.notes.size());
    for (const SongNote &note : song.notes) {
        const double cents = noteCentsFromA4(note.pitch, scale, note.line);
        targets.push_back(a4 * detail::exp2Approx(cents / 1200.0));
        if (!(targets.back() > 0.0) || targets.back() > 20000.0) {
            failSong(note.line, "resolved pitch is outside (0, 20000] Hz");
        }
    }

    struct Piece {
        double duration;
        double fromHz;
        double toHz;
    };

    std::vector<Phoneme> result;
    double previousEndHz = targets.front();
    for (std::size_t index = 0; index < song.notes.size(); ++index) {
        const SongNote &note = song.notes[index];
        const double target = targets[index];

        std::vector<Phoneme> base;
        double totalDuration = 0.0;
        for (const std::string &name :
             splitPhonemes(note.phonemes, note.line)) {
            const Phoneme *phoneme = findPhoneme(name);
            if (phoneme == nullptr) {
                failSong(note.line, "unknown phoneme '" + name + "'");
            }
            base.push_back(*phoneme);
            totalDuration += phoneme->duration;
        }
        if (!(totalDuration > 0.0)) {
            failSong(note.line, "note phoneme sequence has no duration");
        }
        for (Phoneme &phoneme : base) {
            phoneme.duration =
                note.durationSeconds * phoneme.duration / totalDuration;
        }

        std::vector<Piece> contour;
        if (index > 0) {
            const double glide =
                std::min(note.glideSeconds, note.durationSeconds * 0.45);
            if (glide > 0.0) {
                double peak = target;
                if (note.overshootCents != 0.0) {
                    peak = target *
                           detail::exp2Approx(note.overshootCents / 1200.0);
                }
                contour.push_back({glide, previousEndHz, peak});
                if (note.overshootCents != 0.0) {
                    const double settle =
                        std::min(0.08, note.durationSeconds * 0.20);
                    if (settle > 0.0) {
                        contour.push_back({settle, peak, target});
                    }
                }
            }
        }
        double used = std::accumulate(contour.begin(), contour.end(), 0.0,
                                      [](double sum, const Piece &piece) {
                                          return sum + piece.duration;
                                      });
        if (used < note.durationSeconds) {
            contour.push_back({note.durationSeconds - used, target, target});
        }

        if (index + 1 < song.notes.size() && note.preparationCents != 0.0 &&
            !contour.empty()) {
            const double preparation =
                std::min(0.08, note.durationSeconds * 0.15);
            if (preparation > 0.0) {
                double remaining = preparation;
                while (remaining > 0.0 && !contour.empty()) {
                    Piece &tail = contour.back();
                    const double take = std::min(tail.duration, remaining);
                    tail.duration -= take;
                    remaining -= take;
                    if (tail.duration <= 1e-12) {
                        contour.pop_back();
                    }
                }
                const double direction =
                    targets[index + 1] >= target ? 1.0 : -1.0;
                const double prepared =
                    target *
                    detail::exp2Approx(
                        direction * std::abs(note.preparationCents) / 1200.0);
                contour.push_back({preparation, target, prepared});
                previousEndHz = prepared;
            } else {
                previousEndHz = target;
            }
        } else {
            previousEndHz = target;
        }

        std::size_t baseIndex = 0;
        double baseRemaining = base.front().duration;
        double noteElapsed = 0.0;
        for (const Piece &piece : contour) {
            double remaining = piece.duration;
            while (remaining > 1e-12 && baseIndex < base.size()) {
                const double take = std::min(remaining, baseRemaining);
                Phoneme out = base[baseIndex];
                out.duration = take;
                out.f0StartHz = piece.fromHz;
                out.f0EndHz = piece.toHz;
                out.pitchRise = false;
                out.source = "song:note:" + std::to_string(index + 1);
                NoteF0Control control;
                control.noteOffsetSeconds = noteElapsed;
                control.vibratoRateHz = note.vibrato.rateHz;
                control.vibratoExtentCents = note.vibrato.extentCents;
                control.vibratoDelaySeconds = note.vibrato.delaySeconds;
                control.vibratoFadeSeconds = note.vibrato.fadeSeconds;
                out.noteF0 = control;
                result.push_back(std::move(out));
                baseRemaining -= take;
                remaining -= take;
                noteElapsed += take;
                if (baseRemaining <= 1e-12) {
                    ++baseIndex;
                    if (baseIndex < base.size()) {
                        baseRemaining = base[baseIndex].duration;
                    }
                }
            }
        }
    }
    return result;
}

} // namespace formant
