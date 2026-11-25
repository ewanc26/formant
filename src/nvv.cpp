#include "formant/nvv.h"

#include "formant/nvv_data.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>

#include "formant/ipa.h"

namespace formant {
namespace {

std::string trim(std::string_view text) {
    size_t begin = 0;
    size_t end = text.size();
    while (begin < end &&
           std::isspace(static_cast<unsigned char>(text[begin])) != 0) {
        ++begin;
    }
    while (end > begin &&
           std::isspace(static_cast<unsigned char>(text[end - 1])) != 0) {
        --end;
    }
    return std::string(text.substr(begin, end - begin));
}

[[noreturn]] void fail(size_t line, const std::string &message) {
    throw std::invalid_argument("nvv line " + std::to_string(line) + ": " +
                                message);
}

double number(const std::string &token, size_t line, const char *field) {
    try {
        size_t used = 0;
        const double value = std::stod(token, &used);
        if (used != token.size() || !std::isfinite(value)) {
            fail(line, std::string("invalid ") + field + ": " + token);
        }
        return value;
    } catch (const std::exception &) {
        fail(line, std::string("invalid ") + field + ": " + token);
    }
}

bool isTokenChar(unsigned char c) {
    return std::isalnum(c) != 0 || c == '-' || c == '_';
}

} // namespace

NvvLibrary::NvvLibrary(std::string_view builtin, std::string_view user) {
    load(builtin);
    if (!user.empty()) {
        load(user);
    }
    for (const auto &entry : definitions_) {
        orderedNames_.push_back(entry.first);
    }
    for (const auto &entry : styles_) {
        orderedStyles_.push_back(entry.first);
    }
    if (styles_.find("neutral") == styles_.end()) {
        styles_["neutral"] = NvvStyle{"neutral", 1.0, 1.0, 1.0};
        orderedStyles_.insert(orderedStyles_.begin(), "neutral");
    } else {
        auto found =
            std::find(orderedStyles_.begin(), orderedStyles_.end(), "neutral");
        if (found != orderedStyles_.begin()) {
            std::rotate(orderedStyles_.begin(), found, found + 1);
        }
    }
}

void NvvLibrary::load(std::string_view source) {
    size_t lineNumber = 0;
    size_t pos = 0;
    NvvDefinition *current = nullptr;
    std::map<std::string, size_t> definitionLines;
    while (pos <= source.size()) {
        size_t end = source.find('\n', pos);
        if (end == std::string_view::npos) {
            end = source.size();
        }
        const std::string line = trim(source.substr(pos, end - pos));
        ++lineNumber;
        pos = end + 1;
        if (line.empty() || line[0] == '#') {
            continue;
        }
        if (line.rfind("nvv ", 0) == 0) {
            const std::string rest = trim(std::string_view(line).substr(4));
            const size_t bar = rest.find('|');
            const std::string name =
                trim(std::string_view(rest).substr(0, bar));
            if (name.empty() || name.find(' ') != std::string::npos) {
                fail(lineNumber, "nvv needs a single-word name");
            }
            // User definitions replace built-ins of the same name.
            definitions_[name] = NvvDefinition{name, {}};
            definitionLines[name] = lineNumber;
            current = &definitions_[name];
            continue;
        }
        if (line.rfind("style ", 0) == 0) {
            std::istringstream fields(line.substr(6));
            NvvStyle style;
            std::string rate;
            std::string pitch;
            std::string amplitude;
            if (!(fields >> style.name >> rate >> pitch >> amplitude)) {
                fail(lineNumber, "style needs NAME RATE PITCH AMPLITUDE");
            }
            style.rateScale = number(rate, lineNumber, "rate scale");
            style.pitchScale = number(pitch, lineNumber, "pitch scale");
            style.amplitudeScale =
                number(amplitude, lineNumber, "amplitude scale");
            if (style.rateScale <= 0.0) {
                fail(lineNumber, "rate scale must be positive");
            }
            styles_[style.name] = style;
            current = nullptr;
            continue;
        }
        if (current == nullptr) {
            fail(lineNumber, "segment outside an nvv definition");
        }
        std::istringstream fields(line);
        std::string symbol;
        std::string durationToken;
        std::string pitchStartToken;
        std::string pitchEndToken;
        std::string amplitudeToken;
        if (!(fields >> symbol >> durationToken)) {
            fail(lineNumber, "segment needs an IPA symbol and duration");
        }
        const double duration = number(durationToken, lineNumber, "duration");
        if (duration <= 0.0) {
            fail(lineNumber, "duration must be positive");
        }
        double pitchStart = 1.0;
        double pitchEnd = 1.0;
        double amplitude = 1.0;
        if (fields >> pitchStartToken) {
            pitchStart = number(pitchStartToken, lineNumber, "pitch start");
        }
        if (fields >> pitchEndToken) {
            pitchEnd = number(pitchEndToken, lineNumber, "pitch end");
        }
        if (fields >> amplitudeToken) {
            amplitude = number(amplitudeToken, lineNumber, "amplitude");
        }
        std::vector<Phoneme> parsed;
        try {
            parsed = parseIpa(symbol);
        } catch (const std::invalid_argument &) {
            fail(lineNumber, "unknown IPA symbol: " + symbol);
        }
        if (parsed.empty()) {
            fail(lineNumber, "unknown IPA symbol: " + symbol);
        }
        for (Phoneme &phoneme : parsed) {
            phoneme.duration = duration;
            phoneme.pitchStart = pitchStart;
            phoneme.pitchEnd = pitchEnd;
            phoneme.amplitude = amplitude;
            current->segments.push_back(std::move(phoneme));
        }
    }
    for (const auto &entry : definitions_) {
        if (entry.second.segments.empty()) {
            const auto found = definitionLines.find(entry.first);
            fail(found == definitionLines.end() ? 0 : found->second,
                 "nvv '" + entry.first + "' has no segments");
        }
    }
}

bool NvvLibrary::has(std::string_view name) const {
    return definitions_.find(std::string(name)) != definitions_.end();
}

std::vector<Phoneme> NvvLibrary::get(std::string_view name) const {
    const auto found = definitions_.find(std::string(name));
    if (found == definitions_.end()) {
        throw std::invalid_argument("unknown nonverbal vocalization: '" +
                                    std::string(name) + "'");
    }
    const auto styleFound = styles_.find(activeStyle_);
    const NvvStyle style = styleFound == styles_.end()
                               ? NvvStyle{"neutral", 1.0, 1.0, 1.0}
                               : styleFound->second;
    std::vector<Phoneme> result = found->second.segments;
    for (Phoneme &phoneme : result) {
        phoneme.duration *= style.rateScale;
        phoneme.pitchStart *= style.pitchScale;
        phoneme.pitchEnd *= style.pitchScale;
        phoneme.amplitude *= style.amplitudeScale;
        // Provenance lets later layers (#143 breath planning) recognise an
        // author-placed event without changing how this segment renders.
        phoneme.source = "nvv:" + std::string(name);
    }
    return result;
}

void NvvLibrary::setStyle(std::string_view name) {
    if (styles_.find(std::string(name)) == styles_.end()) {
        throw std::invalid_argument("unknown nonverbal style: '" +
                                    std::string(name) + "'");
    }
    activeStyle_ = std::string(name);
}

std::vector<Phoneme>
NvvLibrary::expand(std::string_view text,
                   const std::function<std::vector<Phoneme>(std::string_view)>
                       &frontEnd) const {
    std::vector<Phoneme> result;
    size_t runStart = 0;
    size_t pos = 0;
    while (pos < text.size()) {
        if (text[pos] != '[') {
            ++pos;
            continue;
        }
        const size_t close = text.find(']', pos + 1);
        if (close == std::string_view::npos) {
            break; // an unmatched '[' stays literal text
        }
        const std::string_view candidate =
            text.substr(pos + 1, close - pos - 1);
        bool looksLikeToken = !candidate.empty();
        for (unsigned char c : candidate) {
            if (!isTokenChar(c)) {
                looksLikeToken = false;
                break;
            }
        }
        if (!looksLikeToken) {
            pos = close + 1;
            continue;
        }
        if (runStart < pos) {
            const std::vector<Phoneme> run =
                frontEnd(text.substr(runStart, pos - runStart));
            result.insert(result.end(), run.begin(), run.end());
        }
        const std::vector<Phoneme> nvv = get(candidate);
        result.insert(result.end(), nvv.begin(), nvv.end());
        pos = close + 1;
        runStart = pos;
    }
    if (runStart < text.size()) {
        const std::vector<Phoneme> run = frontEnd(text.substr(runStart));
        result.insert(result.end(), run.begin(), run.end());
    }
    return result;
}

std::string_view builtinNvvData() { return detail::kBuiltinNvvData; }

} // namespace formant
