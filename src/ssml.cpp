#include "formant/ssml.h"

#include "formant/ipa.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>

namespace formant {
namespace {

// Cumulative prosody for a span. Values compose multiplicatively as the
// reader descends through nested <prosody> elements.
struct Prosody {
    double rate = 1.0;   // speaking rate; >1 is faster
    double pitch = 1.0;  // F0 multiplier
    double volume = 1.0; // amplitude multiplier
    // Word emphasis composes with <prosody> but applies only to speech spans
    // explicitly marked by <emphasis>. Neutral values are multiplicative 1.
    double emphasisPitch = 1.0;
    double emphasisDuration = 1.0;
    double emphasisAmplitude = 1.0;
};

SsmlEmphasisLevels withDefaultEmphasisLevels(const SsmlEmphasisLevels &levels) {
    SsmlEmphasisLevels result = {
        {"strong", {1.22, 1.30, 1.28}},
        {"moderate", {1.12, 1.15, 1.14}},
        {"reduced", {0.90, 0.85, 0.88}},
    };
    for (const auto &entry : levels) {
        result[entry.first] = entry.second;
    }
    return result;
}

double number(const std::string &text, size_t at) {
    char *end = nullptr;
    const double value = std::strtod(text.c_str(), &end);
    if (end != text.c_str() + text.size() || !std::isfinite(value)) {
        throw std::invalid_argument("SSML at byte " + std::to_string(at) +
                                    ": invalid number '" + text + "'");
    }
    return value;
}

std::string trim(std::string value) {
    const auto first = value.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) {
        return "";
    }
    const auto last = value.find_last_not_of(" \t\r\n");
    return value.substr(first, last - first + 1);
}

std::string lower(std::string value) {
    std::transform(
        value.begin(), value.end(), value.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}

// Decodes the five predefined XML entities plus decimal/hex character refs.
// Unknown entities are preserved verbatim so the text front end can decide.
std::string decodeEntities(std::string_view text) {
    std::string out;
    out.reserve(text.size());
    for (size_t i = 0; i < text.size(); ++i) {
        if (text[i] != '&') {
            out.push_back(text[i]);
            continue;
        }
        const size_t semi = text.find(';', i + 1);
        if (semi == std::string_view::npos) {
            out.push_back(text[i]);
            continue;
        }
        const std::string entity(text.substr(i + 1, semi - i - 1));
        if (entity == "amp") {
            out.push_back('&');
        } else if (entity == "lt") {
            out.push_back('<');
        } else if (entity == "gt") {
            out.push_back('>');
        } else if (entity == "quot") {
            out.push_back('"');
        } else if (entity == "apos") {
            out.push_back('\'');
        } else if (entity.size() > 1 && entity[0] == '#') {
            const bool hex =
                entity.size() > 2 && (entity[1] == 'x' || entity[1] == 'X');
            const std::string digits =
                hex ? entity.substr(2) : entity.substr(1);
            char *end = nullptr;
            const unsigned long code =
                std::strtoul(digits.c_str(), &end, hex ? 16 : 10);
            if (end == digits.c_str() + digits.size() && code <= 0x10FFFF) {
                // Encode the code point as UTF-8 by hand; no locale involved.
                if (code <= 0x7F) {
                    out.push_back(static_cast<char>(code));
                } else if (code <= 0x7FF) {
                    out.push_back(static_cast<char>(0xC0 | (code >> 6)));
                    out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
                } else if (code <= 0xFFFF) {
                    out.push_back(static_cast<char>(0xE0 | (code >> 12)));
                    out.push_back(
                        static_cast<char>(0x80 | ((code >> 6) & 0x3F)));
                    out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
                } else {
                    out.push_back(static_cast<char>(0xF0 | (code >> 18)));
                    out.push_back(
                        static_cast<char>(0x80 | ((code >> 12) & 0x3F)));
                    out.push_back(
                        static_cast<char>(0x80 | ((code >> 6) & 0x3F)));
                    out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
                }
                i = semi;
                continue;
            }
            out.append(text.substr(i, semi - i + 1));
        } else {
            out.append(text.substr(i, semi - i + 1));
        }
        i = semi;
    }
    return out;
}

std::optional<std::string>
attribute(const std::vector<std::pair<std::string, std::string>> &attrs,
          std::string_view name) {
    for (const auto &[key, value] : attrs) {
        if (key == name) {
            return value;
        }
    }
    return std::nullopt;
}

double parseRate(const std::string &raw, size_t at) {
    const std::string value = lower(trim(raw));
    if (value == "x-slow")
        return 0.5;
    if (value == "slow")
        return 0.75;
    if (value == "medium")
        return 1.0;
    if (value == "fast")
        return 1.25;
    if (value == "x-fast")
        return 1.5;
    double result = 1.0;
    if (!value.empty() && value.back() == '%') {
        result = 1.0 + number(value.substr(0, value.size() - 1), at) / 100.0;
    } else {
        result = number(value, at);
    }
    if (!(result > 0.0)) {
        throw std::invalid_argument("SSML at byte " + std::to_string(at) +
                                    ": rate must be positive");
    }
    return result;
}

double parseVolume(const std::string &raw, size_t at) {
    const std::string value = lower(trim(raw));
    if (value == "silent")
        return 0.0;
    if (value == "x-soft")
        return 0.25;
    if (value == "soft")
        return 0.5;
    if (value == "medium")
        return 1.0;
    if (value == "loud")
        return 1.25;
    if (value == "x-loud")
        return 1.5;
    double result = 1.0;
    if (!value.empty() && value.back() == '%') {
        result = 1.0 + number(value.substr(0, value.size() - 1), at) / 100.0;
    } else {
        result = number(value, at);
    }
    if (!(result >= 0.0)) {
        throw std::invalid_argument("SSML at byte " + std::to_string(at) +
                                    ": volume must not be negative");
    }
    return result;
}

double parsePitch(const std::string &raw, size_t at) {
    const std::string value = lower(trim(raw));
    const auto fail = [at]() -> double {
        throw std::invalid_argument(
            "SSML at byte " + std::to_string(at) +
            ": pitch must use a relative unit (for example '+10%' or '+2st')");
    };
    if (value.size() < 2) {
        return fail();
    }
    if (value.back() == '%') {
        const double result =
            1.0 + number(value.substr(0, value.size() - 1), at) / 100.0;
        if (!(result > 0.0)) {
            return fail();
        }
        return result;
    }
    if (value.size() >= 2 && value.substr(value.size() - 2) == "st") {
        const double semitones = number(value.substr(0, value.size() - 2), at);
        return std::pow(2.0, semitones / 12.0);
    }
    return fail();
}

double parseBreakTime(const std::string &raw, size_t at) {
    const std::string value = lower(trim(raw));
    double seconds = 0.0;
    if (value.size() >= 2 && value.substr(value.size() - 2) == "ms") {
        seconds = number(value.substr(0, value.size() - 2), at) / 1000.0;
    } else if (!value.empty() && value.back() == 's') {
        seconds = number(value.substr(0, value.size() - 1), at);
    } else {
        throw std::invalid_argument("SSML at byte " + std::to_string(at) +
                                    ": break time must end in 'ms' or 's'");
    }
    if (!(seconds >= 0.0) || seconds > 60.0) {
        throw std::invalid_argument("SSML at byte " + std::to_string(at) +
                                    ": break time must be between 0 and 60 s");
    }
    return seconds;
}

double strengthDuration(const std::string &raw, size_t at) {
    const std::string value = lower(trim(raw));
    if (value == "weak")
        return 0.12;
    if (value == "medium")
        return 0.28;
    if (value == "strong")
        return 0.5;
    throw std::invalid_argument(
        "SSML at byte " + std::to_string(at) +
        ": break strength must be weak, medium or strong");
}

const char *const kOnes[] = {"zero",    "one",       "two",      "three",
                             "four",    "five",      "six",      "seven",
                             "eight",   "nine",      "ten",      "eleven",
                             "twelve",  "thirteen",  "fourteen", "fifteen",
                             "sixteen", "seventeen", "eighteen", "nineteen"};
const char *const kTens[] = {"",      "ten",   "twenty",  "thirty", "forty",
                             "fifty", "sixty", "seventy", "eighty", "ninety"};

std::string numberToWords(long long value) {
    if (value == 0) {
        return kOnes[0];
    }
    bool negative = value < 0;
    unsigned long long n =
        negative ? static_cast<unsigned long long>(-(value + 1)) + 1
                 : static_cast<unsigned long long>(value);
    std::string result;
    auto append = [&](const char *word) {
        if (!result.empty())
            result.push_back(' ');
        result += word;
    };
    if (n >= 1000000) {
        append(numberToWords(static_cast<long long>(n / 1000000)).c_str());
        append("million");
        n %= 1000000;
    }
    if (n >= 1000) {
        append(numberToWords(static_cast<long long>(n / 1000)).c_str());
        append("thousand");
        n %= 1000;
    }
    if (n >= 100) {
        append(kOnes[n / 100]);
        append("hundred");
        n %= 100;
    }
    if (n >= 20) {
        append(kTens[n / 10]);
        n %= 10;
    }
    if (n > 0 || result.empty()) {
        append(kOnes[n]);
    }
    if (negative) {
        result.insert(0, "minus ");
    }
    return result;
}

std::string expandSayAs(std::string_view text, std::string_view interpretAs,
                        size_t at) {
    const std::string kind = lower(std::string(interpretAs));
    std::string digits;
    for (char c : text) {
        if (std::isdigit(static_cast<unsigned char>(c))) {
            digits.push_back(c);
        } else if (c == '-' && digits.empty()) {
            digits.push_back(c);
        }
    }
    if (kind == "digits" || kind == "date") {
        std::string words;
        for (char c : digits) {
            if (c == '-')
                continue;
            if (!words.empty())
                words.push_back(' ');
            words += kOnes[c - '0'];
        }
        return words;
    }
    if (kind == "number") {
        if (digits.empty() || digits == "-") {
            throw std::invalid_argument("SSML at byte " + std::to_string(at) +
                                        ": say-as number needs digits");
        }
        return numberToWords(std::strtoll(digits.c_str(), nullptr, 10));
    }
    throw std::invalid_argument("SSML at byte " + std::to_string(at) +
                                ": unsupported say-as interpretation '" +
                                std::string(interpretAs) + "'");
}

bool canEmphasize(const Phoneme &phoneme) {
    // Gaps, pauses and authored events are not words. Provenance also protects
    // anything a previous layer explicitly tagged from a second emphasis pass.
    return phoneme.kind != PhonemeKind::Silence && phoneme.name != "_" &&
           phoneme.name != "|" && phoneme.source.empty();
}

void applyProsody(std::vector<Phoneme> &phonemes, const Prosody &prosody) {
    const bool emphasize = prosody.emphasisPitch != 1.0 ||
                           prosody.emphasisDuration != 1.0 ||
                           prosody.emphasisAmplitude != 1.0;
    for (Phoneme &phoneme : phonemes) {
        if (prosody.rate > 0.0) {
            phoneme.duration /= prosody.rate;
        }
        phoneme.pitchStart *= prosody.pitch;
        phoneme.pitchEnd *= prosody.pitch;
        phoneme.amplitude *= prosody.volume;
        if (emphasize && canEmphasize(phoneme)) {
            phoneme.duration *= prosody.emphasisDuration;
            phoneme.pitchStart *= prosody.emphasisPitch;
            phoneme.pitchEnd *= prosody.emphasisPitch;
            phoneme.amplitude *= prosody.emphasisAmplitude;
            // The neural layer skips authored source tags, so emphasis is not
            // applied twice when --neural-apply is also requested.
            phoneme.source = "style:emphasis";
        }
    }
}

Phoneme pause(double seconds, double rate) {
    Phoneme result = *findPhoneme("_");
    result.duration = rate > 0.0 ? seconds / rate : seconds;
    return result;
}

struct Tag {
    std::string name;
    std::vector<std::pair<std::string, std::string>> attrs;
    bool closing = false;
    bool selfClosing = false;
    size_t start = 0;
};

class SsmlParser {
  public:
    SsmlParser(std::string_view ssml, const SsmlTextFrontEnd &frontEnd,
               const SsmlNvvResolver *resolveNvv = nullptr,
               const SsmlEmphasisLevels &emphasisLevels = {})
        : src_(ssml), frontEnd_(frontEnd), resolveNvv_(resolveNvv),
          emphasisLevels_(withDefaultEmphasisLevels(emphasisLevels)) {}

    std::vector<Phoneme> parse() {
        // A <speak> wrapper is just a container; bare text works too. Both
        // are handled uniformly by the child reader.
        skipMisc();
        return parseChildren({}, "", 0);
    }

  private:
    std::string_view src_;
    const SsmlTextFrontEnd &frontEnd_;
    const SsmlNvvResolver *resolveNvv_ = nullptr;
    SsmlEmphasisLevels emphasisLevels_;
    size_t pos_ = 0;

    [[noreturn]] void fail(const std::string &message, size_t at) const {
        throw std::invalid_argument("SSML at byte " + std::to_string(at) +
                                    ": " + message);
    }

    bool peek(char c) const { return pos_ < src_.size() && src_[pos_] == c; }

    bool startsWith(std::string_view word) const {
        return src_.substr(pos_).substr(0, word.size()) == word;
    }

    void skipWhitespace() {
        while (pos_ < src_.size() &&
               std::isspace(static_cast<unsigned char>(src_[pos_]))) {
            ++pos_;
        }
    }

    void skipMisc() {
        for (;;) {
            skipWhitespace();
            if (startsWith("<!--")) {
                const size_t end = src_.find("-->", pos_ + 4);
                if (end == std::string_view::npos) {
                    fail("unterminated comment", pos_);
                }
                pos_ = end + 3;
            } else if (startsWith("<?")) {
                const size_t end = src_.find("?>", pos_ + 2);
                if (end == std::string_view::npos) {
                    fail("unterminated processing instruction", pos_);
                }
                pos_ = end + 2;
            } else if (startsWith("<!DOCTYPE") || startsWith("<!doctype")) {
                const size_t end = src_.find('>', pos_);
                if (end == std::string_view::npos) {
                    fail("unterminated doctype", pos_);
                }
                pos_ = end + 1;
            } else {
                return;
            }
        }
    }

    Tag readTag() {
        const size_t start = pos_;
        if (!peek('<')) {
            fail("expected '<'", start);
        }
        ++pos_;
        Tag tag;
        tag.start = start;
        if (peek('/')) {
            tag.closing = true;
            ++pos_;
        }
        const size_t nameStart = pos_;
        while (pos_ < src_.size()) {
            const unsigned char c = static_cast<unsigned char>(src_[pos_]);
            if (std::isalnum(c) || c == '-' || c == '_' || c == ':') {
                ++pos_;
            } else {
                break;
            }
        }
        if (pos_ == nameStart) {
            fail("missing element name", start);
        }
        tag.name = lower(std::string(src_.substr(nameStart, pos_ - nameStart)));

        for (;;) {
            skipWhitespace();
            if (pos_ >= src_.size()) {
                fail("unterminated tag <" + tag.name + ">", start);
            }
            if (peek('/')) {
                ++pos_;
                if (!peek('>')) {
                    fail("expected '>' after '/'", start);
                }
                tag.selfClosing = true;
                ++pos_;
                break;
            }
            if (peek('>')) {
                ++pos_;
                break;
            }
            const size_t attrStart = pos_;
            while (pos_ < src_.size()) {
                const unsigned char c = static_cast<unsigned char>(src_[pos_]);
                if (std::isalnum(c) || c == '-' || c == '_' || c == ':') {
                    ++pos_;
                } else {
                    break;
                }
            }
            if (pos_ == attrStart) {
                fail("invalid attribute in <" + tag.name + ">", start);
            }
            const std::string name =
                lower(std::string(src_.substr(attrStart, pos_ - attrStart)));
            skipWhitespace();
            if (!peek('=')) {
                fail("attribute '" + name + "' needs a value", start);
            }
            ++pos_;
            skipWhitespace();
            if (!peek('"') && !peek('\'')) {
                fail("attribute '" + name + "' must be quoted", start);
            }
            const char quote = src_[pos_++];
            const size_t valueStart = pos_;
            while (pos_ < src_.size() && src_[pos_] != quote) {
                ++pos_;
            }
            if (pos_ >= src_.size()) {
                fail("unterminated attribute '" + name + "'", start);
            }
            const std::string value =
                decodeEntities(src_.substr(valueStart, pos_ - valueStart));
            ++pos_;
            tag.attrs.emplace_back(name, value);
        }
        return tag;
    }

    std::vector<Phoneme> parseChildren(const Prosody &prosody,
                                       std::string_view stopTag,
                                       size_t stopStart) {
        std::vector<Phoneme> result;
        for (;;) {
            if (pos_ >= src_.size()) {
                if (!stopTag.empty()) {
                    fail("unclosed <" + std::string(stopTag) + ">", stopStart);
                }
                return result;
            }
            if (peek('<') && startsWith("<!--")) {
                skipMisc();
                continue;
            }
            if (peek('<')) {
                const Tag tag = readTag();
                if (tag.closing) {
                    if (stopTag.empty()) {
                        fail("unexpected </" + tag.name + ">", tag.start);
                    }
                    if (tag.name != stopTag) {
                        fail("expected </" + std::string(stopTag) +
                                 "> but found </" + tag.name + ">",
                             tag.start);
                    }
                    return result;
                }
                appendElement(tag, prosody, result);
                continue;
            }
            const size_t begin = pos_;
            while (pos_ < src_.size() && !peek('<')) {
                ++pos_;
            }
            const std::string text =
                decodeEntities(src_.substr(begin, pos_ - begin));
            if (frontEnd_) {
                std::vector<Phoneme> chunk = frontEnd_(text);
                applyProsody(chunk, prosody);
                result.insert(result.end(), chunk.begin(), chunk.end());
            }
        }
    }

    void appendElement(const Tag &tag, const Prosody &prosody,
                       std::vector<Phoneme> &result) {
        if (tag.name == "speak" || tag.name == "s" || tag.name == "p") {
            const bool isBoundary = tag.name == "s" || tag.name == "p";
            std::vector<Phoneme> nested =
                parseChildren(prosody, tag.name, tag.start);
            result.insert(result.end(), nested.begin(), nested.end());
            if (isBoundary) {
                result.push_back(
                    pause(tag.name == "p" ? 0.55 : 0.28, prosody.rate));
            }
            return;
        }
        if (tag.name == "prosody") {
            Prosody next = prosody;
            if (const auto rate = attribute(tag.attrs, "rate")) {
                next.rate *= parseRate(*rate, tag.start);
            }
            if (const auto pitch = attribute(tag.attrs, "pitch")) {
                next.pitch *= parsePitch(*pitch, tag.start);
            }
            if (const auto volume = attribute(tag.attrs, "volume")) {
                next.volume *= parseVolume(*volume, tag.start);
            }
            std::vector<Phoneme> nested =
                parseChildren(next, "prosody", tag.start);
            result.insert(result.end(), nested.begin(), nested.end());
            return;
        }
        if (tag.name == "emphasis") {
            const std::string level =
                lower(attribute(tag.attrs, "level").value_or("strong"));
            const auto found = emphasisLevels_.find(level);
            if (found == emphasisLevels_.end()) {
                fail("unsupported emphasis level '" + level + "'", tag.start);
            }
            Prosody next = prosody;
            next.emphasisPitch *= found->second.pitch;
            next.emphasisDuration *= found->second.duration;
            next.emphasisAmplitude *= found->second.amplitude;
            std::vector<Phoneme> nested =
                parseChildren(next, "emphasis", tag.start);
            result.insert(result.end(), nested.begin(), nested.end());
            return;
        }
        if (tag.name == "break") {
            std::optional<double> seconds;
            if (const auto time = attribute(tag.attrs, "time")) {
                seconds = parseBreakTime(*time, tag.start);
            } else if (const auto strength = attribute(tag.attrs, "strength")) {
                seconds = strengthDuration(*strength, tag.start);
            } else {
                fail("break needs a time or strength attribute", tag.start);
            }
            result.push_back(pause(*seconds, prosody.rate));
            return;
        }
        if (tag.name == "nvv") {
            if (resolveNvv_ == nullptr) {
                fail("<nvv> is not available in this SSML context", tag.start);
            }
            const auto name = attribute(tag.attrs, "name");
            if (!name) {
                fail("nvv needs a name attribute", tag.start);
            }
            std::vector<Phoneme> chunk = (*resolveNvv_)(*name, tag.start);
            applyProsody(chunk, prosody);
            result.insert(result.end(), chunk.begin(), chunk.end());
            return;
        }
        if (tag.name == "phoneme") {
            const auto ph = attribute(tag.attrs, "ph");
            if (!ph) {
                fail("phoneme needs a ph attribute", tag.start);
            }
            const std::string alphabet =
                lower(attribute(tag.attrs, "alphabet").value_or("ipa"));
            std::vector<Phoneme> chunk;
            if (alphabet == "ipa") {
                chunk = parseIpa(*ph);
            } else if (alphabet == "x-sampa") {
                chunk = parseXsampa(*ph);
            } else {
                fail("unsupported phoneme alphabet '" + alphabet + "'",
                     tag.start);
            }
            applyProsody(chunk, prosody);
            result.insert(result.end(), chunk.begin(), chunk.end());
            return;
        }
        if (tag.name == "say-as") {
            const auto interpretAs = attribute(tag.attrs, "interpret-as");
            if (!interpretAs) {
                fail("say-as needs an interpret-as attribute", tag.start);
            }
            const size_t begin = pos_;
            while (pos_ < src_.size() && !peek('<')) {
                ++pos_;
            }
            if (pos_ >= src_.size()) {
                fail("unclosed <say-as>", tag.start);
            }
            const std::string content =
                decodeEntities(src_.substr(begin, pos_ - begin));
            const Tag closing = readTag();
            if (!closing.closing || closing.name != "say-as") {
                fail("say-as may contain text only", tag.start);
            }
            const std::string words =
                expandSayAs(content, *interpretAs, tag.start);
            if (frontEnd_ && !words.empty()) {
                std::vector<Phoneme> chunk = frontEnd_(words);
                applyProsody(chunk, prosody);
                result.insert(result.end(), chunk.begin(), chunk.end());
            }
            return;
        }
        fail("unsupported element <" + tag.name + ">", tag.start);
    }
};

} // namespace

std::vector<Phoneme> phonemizeSsml(std::string_view ssml,
                                   const SsmlTextFrontEnd &phonemizeText) {
    SsmlParser parser(ssml, phonemizeText);
    return parser.parse();
}

std::vector<Phoneme> phonemizeSsml(std::string_view ssml,
                                   const SsmlTextFrontEnd &phonemizeText,
                                   const SsmlNvvResolver &resolveNvv) {
    SsmlParser parser(ssml, phonemizeText, &resolveNvv);
    return parser.parse();
}

std::vector<Phoneme> phonemizeSsml(std::string_view ssml,
                                   const SsmlTextFrontEnd &phonemizeText,
                                   const SsmlEmphasisLevels &emphasisLevels) {
    SsmlParser parser(ssml, phonemizeText, nullptr, emphasisLevels);
    return parser.parse();
}

std::vector<Phoneme> phonemizeSsml(std::string_view ssml,
                                   const SsmlTextFrontEnd &phonemizeText,
                                   const SsmlNvvResolver &resolveNvv,
                                   const SsmlEmphasisLevels &emphasisLevels) {
    SsmlParser parser(ssml, phonemizeText, &resolveNvv, emphasisLevels);
    return parser.parse();
}

std::vector<Phoneme> phonemizeSsml(std::string_view ssml, Language language) {
    return phonemizeSsml(ssml, [language](std::string_view text) {
        return phonemize(text, language);
    });
}

} // namespace formant
