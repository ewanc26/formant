#include "formant/prosody_file.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <fstream>
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

bool endsWith(const std::string &value, const std::string &suffix) {
    return value.size() >= suffix.size() &&
           value.compare(value.size() - suffix.size(), suffix.size(), suffix) ==
               0;
}

bool endsWith(const std::string &value, char suffix) {
    return !value.empty() && value.back() == suffix;
}

[[noreturn]] void fail(std::size_t line, const std::string &message) {
    throw std::invalid_argument("prosody line " + std::to_string(line) + ": " +
                                message);
}

double number(const std::string &token, std::size_t line,
              const std::string &what) {
    if (token.empty()) {
        fail(line, "empty " + what);
    }
    char *end = nullptr;
    const double value = std::strtod(token.c_str(), &end);
    if (end != token.c_str() + token.size() || !std::isfinite(value)) {
        fail(line, "invalid " + what + " '" + token + "'");
    }
    return value;
}

std::size_t index(const std::string &token, std::size_t line) {
    if (token.empty() ||
        !std::all_of(token.begin(), token.end(),
                     [](unsigned char c) { return std::isdigit(c) != 0; })) {
        fail(line, "invalid segment index '" + token + "'");
    }
    char *end = nullptr;
    const unsigned long long value = std::strtoull(token.c_str(), &end, 10);
    if (end != token.c_str() + token.size() || value > 1000000ULL) {
        fail(line, "segment index out of range '" + token + "'");
    }
    return static_cast<std::size_t>(value);
}

ProsodySelector parseSelector(const std::string &token, std::size_t line) {
    ProsodySelector result;
    if (token.empty()) {
        fail(line, "missing selector");
    }
    if (token.front() != '@') {
        result.segment = index(token, line);
        return result;
    }
    const std::string value = lower(token.substr(1));
    double seconds = 0.0;
    if (endsWith(value, "ms")) {
        seconds = number(value.substr(0, value.size() - 2), line,
                         "time in milliseconds") /
                  1000.0;
    } else if (endsWith(value, "s")) {
        seconds = number(value.substr(0, value.size() - 1), line, "time");
    } else {
        seconds = number(value, line, "time");
    }
    if (!(seconds >= 0.0) || seconds > 86400.0) {
        fail(line, "time must be between 0 and 86400 seconds");
    }
    result.timeSeconds = seconds;
    return result;
}

// An F0 value is either absolute (Hz) or relative (a multiplier on the
// front-end contour). Cents, semitones and percent are relative; a bare value
// or an explicit "Hz" is absolute.
struct F0Value {
    std::optional<double> hz;
    std::optional<double> ratio;
};

F0Value parseF0Value(const std::string &token, std::size_t line) {
    const std::string value = lower(token);
    F0Value result;
    if (endsWith(value, "c")) {
        const double cents =
            number(value.substr(0, value.size() - 1), line, "F0 cents");
        result.ratio = std::pow(2.0, cents / 1200.0);
    } else if (endsWith(value, "st")) {
        const double semitones =
            number(value.substr(0, value.size() - 2), line, "F0 semitones");
        result.ratio = std::pow(2.0, semitones / 12.0);
    } else if (endsWith(value, '%')) {
        const double percent =
            number(value.substr(0, value.size() - 1), line, "F0 percent");
        result.ratio = 1.0 + percent / 100.0;
    } else {
        std::string digits = value;
        if (endsWith(digits, "hz")) {
            digits = digits.substr(0, digits.size() - 2);
        }
        result.hz = number(digits, line, "F0 in Hz");
        if (!(*result.hz > 0.0) || *result.hz > 20000.0) {
            fail(line, "F0 must be between 0 and 20000 Hz");
        }
    }
    if (result.ratio && !(*result.ratio > 0.0)) {
        fail(line, "relative F0 must be positive");
    }
    return result;
}

void setF0(ProsodyDirective &directive, const F0Value &value, bool isEnd,
           std::size_t line) {
    if (value.hz) {
        if (directive.f0Ratio) {
            fail(line, "cannot mix absolute and relative F0 in one directive");
        }
        (isEnd ? directive.f0EndHz : directive.f0Hz) = *value.hz;
    } else {
        if (directive.f0Hz) {
            fail(line, "cannot mix absolute and relative F0 in one directive");
        }
        (isEnd ? directive.f0EndRatio : directive.f0Ratio) = *value.ratio;
    }
}

void parseDuration(ProsodyDirective &directive, const std::string &token,
                   std::size_t line) {
    const std::string value = lower(token);
    if (endsWith(value, "ms")) {
        directive.durationSeconds = number(value.substr(0, value.size() - 2),
                                           line, "duration in milliseconds") /
                                    1000.0;
    } else if (endsWith(value, "s")) {
        directive.durationSeconds =
            number(value.substr(0, value.size() - 1), line, "duration");
    } else {
        double scale = 0.0;
        if (endsWith(value, '%')) {
            scale = 1.0 + number(value.substr(0, value.size() - 1), line,
                                 "duration percent") /
                              100.0;
        } else {
            std::string digits = value;
            if (endsWith(digits, "x")) {
                digits = digits.substr(0, digits.size() - 1);
            }
            scale = number(digits, line, "duration scale");
        }
        if (!(scale > 0.0)) {
            fail(line, "duration scale must be positive");
        }
        directive.durationScale = scale;
    }
    if (directive.durationSeconds) {
        const double seconds = *directive.durationSeconds;
        if (!(seconds > 0.0) || seconds > 3600.0) {
            fail(line, "duration must be between 0 and 3600 seconds");
        }
    }
}

void parseAmplitude(ProsodyDirective &directive, const std::string &token,
                    std::size_t line) {
    const std::string value = lower(token);
    double scale = 0.0;
    if (endsWith(value, '%')) {
        scale = 1.0 + number(value.substr(0, value.size() - 1), line,
                             "amplitude percent") /
                          100.0;
    } else {
        scale = number(value, line, "amplitude scale");
    }
    if (!(scale >= 0.0)) {
        fail(line, "amplitude must not be negative");
    }
    directive.amplitudeScale = scale;
}

struct Timeline {
    std::vector<double> ends;
};

Timeline timelineOf(const std::vector<Phoneme> &phonemes) {
    Timeline result;
    result.ends.reserve(phonemes.size());
    double total = 0.0;
    for (const Phoneme &phoneme : phonemes) {
        total += phoneme.duration;
        result.ends.push_back(total);
    }
    return result;
}

std::size_t resolve(const ProsodyDirective &directive, const Timeline &timeline,
                    std::size_t count) {
    if (directive.selector.segment) {
        const std::size_t at = *directive.selector.segment;
        if (at >= count) {
            fail(directive.line, "segment index " + std::to_string(at) +
                                     " is out of range (utterance has " +
                                     std::to_string(count) + " segments)");
        }
        return at;
    }
    const double time = *directive.selector.timeSeconds;
    for (std::size_t i = 0; i < timeline.ends.size(); ++i) {
        if (time < timeline.ends[i]) {
            return i;
        }
    }
    fail(directive.line, "time " + std::to_string(time) +
                             "s is past the end of the utterance (" +
                             std::to_string(timeline.ends.back()) + "s)");
}

} // namespace

ProsodyFile parseProsodyFile(std::string_view text) {
    ProsodyFile result;
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
        const std::size_t first = lineText.find_first_not_of(" \t\r\n");
        if (first == std::string::npos) {
            continue;
        }
        std::istringstream fields(lineText);
        std::string kind;
        std::string selector;
        fields >> kind >> selector;
        if (!fields) {
            fail(line, "expected a directive and a selector");
        }
        ProsodyDirective directive;
        directive.selector = parseSelector(selector, line);
        directive.line = line;

        const std::string name = lower(kind);
        if (name == "f0") {
            std::string start;
            std::string end;
            fields >> start;
            if (!fields) {
                fail(line,
                     "f0 needs a value in Hz, cents, semitones or percent");
            }
            setF0(directive, parseF0Value(start, line), false, line);
            if (fields >> end) {
                setF0(directive, parseF0Value(end, line), true, line);
            }
        } else if (name == "duration") {
            std::string value;
            fields >> value;
            if (!fields) {
                fail(line, "duration needs a scale or an absolute time");
            }
            parseDuration(directive, value, line);
        } else if (name == "amplitude") {
            std::string value;
            fields >> value;
            if (!fields) {
                fail(line, "amplitude needs a scale");
            }
            parseAmplitude(directive, value, line);
        } else {
            fail(line, "unsupported directive '" + kind + "'");
        }
        std::string extra;
        if (fields >> extra) {
            fail(line, "unexpected extra value '" + extra + "'");
        }
        result.directives.push_back(std::move(directive));
    }
    return result;
}

ProsodyFile loadProsodyFile(const std::string &path) {
    std::ifstream file(path);
    if (!file) {
        throw std::runtime_error("cannot open prosody file: " + path);
    }
    std::ostringstream source;
    source << file.rdbuf();
    if (file.bad()) {
        throw std::runtime_error("cannot read prosody file: " + path);
    }
    return parseProsodyFile(source.str());
}

void applyProsodyFile(std::vector<Phoneme> &phonemes, const ProsodyFile &file) {
    if (file.empty()) {
        return;
    }
    if (phonemes.empty()) {
        fail(file.directives.front().line,
             "override file has directives but the utterance has no segments");
    }

    // Phase 1: duration changes use the incoming timeline so that every time
    // selector in a file names the same segment regardless of directive order.
    const Timeline incoming = timelineOf(phonemes);
    for (const ProsodyDirective &directive : file.directives) {
        if (!directive.durationScale && !directive.durationSeconds) {
            continue;
        }
        const std::size_t at = resolve(directive, incoming, phonemes.size());
        Phoneme &phoneme = phonemes[at];
        if (directive.durationSeconds) {
            phoneme.duration = *directive.durationSeconds;
        } else {
            phoneme.duration *= *directive.durationScale;
        }
        if (!(phoneme.duration > 0.0) || !std::isfinite(phoneme.duration) ||
            phoneme.duration > 3600.0) {
            fail(directive.line, "resulting duration is out of range");
        }
    }

    // Phase 2: F0 and amplitude use the final timeline, so a time selector
    // names the segment that is actually at that instant in the rendered audio.
    const Timeline final = timelineOf(phonemes);
    for (const ProsodyDirective &directive : file.directives) {
        if (!directive.f0Hz && !directive.f0Ratio &&
            !directive.amplitudeScale) {
            continue;
        }
        const std::size_t at = resolve(directive, final, phonemes.size());
        Phoneme &phoneme = phonemes[at];
        if (directive.f0Hz) {
            phoneme.f0StartHz = *directive.f0Hz;
            phoneme.f0EndHz = directive.f0EndHz.value_or(*directive.f0Hz);
        } else if (directive.f0Ratio) {
            phoneme.pitchStart *= *directive.f0Ratio;
            phoneme.pitchEnd *=
                directive.f0EndRatio.value_or(*directive.f0Ratio);
        }
        if (directive.amplitudeScale) {
            phoneme.amplitude *= *directive.amplitudeScale;
        }
    }
}

} // namespace formant
