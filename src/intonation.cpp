#include "formant/intonation.h"

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

[[noreturn]] void fail(std::size_t line, const std::string &message) {
    throw std::invalid_argument("intonation line " + std::to_string(line) +
                                ": " + message);
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

std::size_t integer(const std::string &token, std::size_t line,
                    const std::string &what, std::size_t maximum) {
    if (token.empty() ||
        !std::all_of(token.begin(), token.end(),
                     [](unsigned char c) { return std::isdigit(c) != 0; })) {
        fail(line, "invalid " + what + " '" + token + "'");
    }
    char *end = nullptr;
    const unsigned long long value = std::strtoull(token.c_str(), &end, 10);
    if (end != token.c_str() + token.size() || value > maximum) {
        fail(line, "invalid " + what + " '" + token + "'");
    }
    return static_cast<std::size_t>(value);
}

double seconds(const std::string &token, std::size_t line) {
    const std::string value = lower(token);
    if (endsWith(value, "ms")) {
        return number(value.substr(0, value.size() - 2), line,
                      "time in milliseconds") /
               1000.0;
    }
    if (endsWith(value, "s")) {
        return number(value.substr(0, value.size() - 1), line, "time");
    }
    return number(value, line, "time");
}

} // namespace

ToneInventory defaultToneInventory() {
    ToneInventory result;
    // Plateau accents and two directional contours. All targets are
    // multipliers of the voice's base pitch, so the existing declination,
    // final rise and vibrato stages still compose with them.
    result.accents = {
        {"H*", {1.30, 1.30}},
        {"L*", {0.75, 0.75}},
        {"L+H*", {0.85, 1.35}},
        {"H+L*", {1.35, 0.85}},
    };
    result.boundaries = {
        {"L-", {0.85, 0.85}},   {"H-", {1.25, 1.25}},   {"L%", {0.72, 0.72}},
        {"H%", {1.35, 1.35}},   {"L-L%", {1.20, 0.72}}, // falling statement
        {"L-H%", {0.85, 1.32}}, // rising yes/no question
        {"H-L%", {1.35, 0.80}}, // wh-question fall from a high peak
        {"H-H%", {1.25, 1.38}}, // exclamation / continuing rise
    };
    return result;
}

ToneInventory languageToneInventory(Language language) {
    ToneInventory result = defaultToneInventory();
    if (language == Language::Spanish) {
        // Shallow, later peaks and a tighter fall are the shipped defaults for
        // the Castilian profile; every number is still replaceable data.
        result.accents["H*"] = {1.27, 1.27};
        result.accents["L+H*"] = {0.88, 1.30};
        result.boundaries["L-L%"] = {1.18, 0.75};
        result.boundaries["L-H%"] = {0.88, 1.28};
        result.downstep = 0.80;
    }
    return result;
}

namespace {

ToneEvent accent(std::size_t line, IntonationSelector selector,
                 std::string label, int downsteps) {
    ToneEvent result;
    result.kind = ToneEventKind::Accent;
    result.selector = selector;
    result.label = std::move(label);
    result.downsteps = downsteps;
    result.line = line;
    return result;
}

ToneEvent boundary(std::size_t line, std::string label) {
    ToneEvent result;
    result.kind = ToneEventKind::Boundary;
    result.label = std::move(label);
    result.line = line;
    return result;
}

ToneEvent pause(std::size_t line, IntonationSelector selector, int tier) {
    ToneEvent result;
    result.kind = ToneEventKind::Break;
    result.selector = selector;
    result.tier = tier;
    result.line = line;
    return result;
}

IntonationSelector parseSelector(const std::string &token, std::size_t line) {
    const std::string value = lower(token);
    IntonationSelector result;
    if (value == "vowel:last") {
        result.kind = IntonationSelectorKind::LastVowel;
        return result;
    }
    if (value == "vowels") {
        result.kind = IntonationSelectorKind::AllVowels;
        return result;
    }
    if (value.rfind("vowel:", 0) == 0) {
        result.kind = IntonationSelectorKind::Vowel;
        result.index = integer(value.substr(6), line, "vowel index", 1000000);
        return result;
    }
    if (!value.empty() && value.front() == '@') {
        result.kind = IntonationSelectorKind::Time;
        const double at = seconds(value.substr(1), line);
        if (!(at >= 0.0) || at > 86400.0) {
            fail(line, "time must be between 0 and 86400 seconds");
        }
        result.timeSeconds = at;
        return result;
    }
    result.kind = IntonationSelectorKind::Segment;
    result.index = integer(value, line, "segment index", 1000000);
    return result;
}

std::pair<std::string, int> parseAccentLabel(const std::string &raw,
                                             std::size_t line) {
    std::size_t prefix = 0;
    while (prefix < raw.size() && raw[prefix] == '!') {
        ++prefix;
    }
    if (prefix == raw.size()) {
        fail(line, "accent label needs a tone name after '!'");
    }
    if (prefix > 8) {
        fail(line, "too many downstep operators");
    }
    return {raw.substr(prefix), static_cast<int>(prefix)};
}

std::vector<std::size_t> resolve(const IntonationSelector &selector,
                                 const std::vector<Phoneme> &phonemes,
                                 const ToneEvent &event) {
    std::vector<std::size_t> result;
    std::vector<std::size_t> vowels;
    for (std::size_t i = 0; i < phonemes.size(); ++i) {
        if (phonemes[i].kind == PhonemeKind::Voiced) {
            vowels.push_back(i);
        }
    }
    switch (selector.kind) {
    case IntonationSelectorKind::Segment:
        if (selector.index < phonemes.size()) {
            result.push_back(selector.index);
        }
        break;
    case IntonationSelectorKind::Vowel:
        if (selector.index < vowels.size()) {
            result.push_back(vowels[selector.index]);
        }
        break;
    case IntonationSelectorKind::LastVowel:
        if (!vowels.empty()) {
            result.push_back(vowels.back());
        }
        break;
    case IntonationSelectorKind::AllVowels:
        result = vowels;
        break;
    case IntonationSelectorKind::Time: {
        double total = 0.0;
        for (std::size_t i = 0; i < phonemes.size(); ++i) {
            total += phonemes[i].duration;
            if (selector.timeSeconds < total) {
                result.push_back(i);
                break;
            }
        }
        break;
    }
    }
    if (result.empty() && !event.fromTune) {
        fail(event.line, "selector matches no segment");
    }
    return result;
}

ToneShape scaled(const ToneShape &shape, int downsteps, double factor) {
    const double amount = std::pow(factor, downsteps);
    return {shape.start * amount, shape.end * amount};
}

} // namespace

IntonationFile parseIntonationFile(std::string_view text) {
    IntonationFile result;
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
        fields >> kind;
        const std::string name = lower(kind);
        if (name == "tune") {
            std::string tune;
            fields >> tune;
            if (!fields || tune.empty()) {
                fail(line, "tune needs a name");
            }
            result.tune = tune;
        } else if (name == "boundary") {
            std::string label;
            fields >> label;
            if (!fields || label.empty()) {
                fail(line, "boundary needs a label");
            }
            result.events.push_back(boundary(line, label));
        } else if (name == "accent") {
            std::string selector;
            std::string label;
            fields >> selector >> label;
            if (!fields) {
                fail(line, "accent needs a selector and a label");
            }
            const auto parsed = parseAccentLabel(label, line);
            result.events.push_back(accent(line, parseSelector(selector, line),
                                           parsed.first, parsed.second));
        } else if (name == "break") {
            std::string selector;
            std::string tier;
            fields >> selector >> tier;
            if (!fields) {
                fail(line, "break needs a selector and a break index (0-4)");
            }
            const std::size_t at = integer(tier, line, "break index", 4);
            result.events.push_back(pause(line, parseSelector(selector, line),
                                          static_cast<int>(at)));
        } else {
            fail(line, "unsupported directive '" + kind + "'");
        }
        std::string extra;
        if (fields >> extra) {
            fail(line, "unexpected extra value '" + extra + "'");
        }
    }
    return result;
}

IntonationFile loadIntonationFile(const std::string &path) {
    std::ifstream file(path);
    if (!file) {
        throw std::runtime_error("cannot open intonation file: " + path);
    }
    std::ostringstream source;
    source << file.rdbuf();
    if (file.bad()) {
        throw std::runtime_error("cannot read intonation file: " + path);
    }
    return parseIntonationFile(source.str());
}

std::vector<std::string> intonationTuneNames(Language language) {
    (void)
        language; // every shipping profile exposes the same five constructions
    return {"statement", "yesno", "wh", "list", "exclamation"};
}

const IntonationTune *findIntonationTune(Language language,
                                         std::string_view name) {
    // Tunes are immutable data with programmatic lifetime. Each language has
    // its own table so the inventory and the symbolic events stay in step.
    static const IntonationTune english[] = {
        {"statement",
         {accent(0, {IntonationSelectorKind::Vowel, 0, 0.0}, "H*", 0),
          boundary(0, "L-L%")}},
        {"yesno",
         {accent(0, {IntonationSelectorKind::Vowel, 0, 0.0}, "L+H*", 0),
          accent(0, {IntonationSelectorKind::LastVowel, 0, 0.0}, "H*", 0),
          boundary(0, "L-H%")}},
        {"wh",
         {accent(0, {IntonationSelectorKind::Vowel, 0, 0.0}, "H*", 0),
          boundary(0, "H-L%")}},
        {"list",
         {accent(0, {IntonationSelectorKind::Vowel, 0, 0.0}, "H*", 0),
          accent(0, {IntonationSelectorKind::Vowel, 1, 0.0}, "H*", 1),
          accent(0, {IntonationSelectorKind::Vowel, 2, 0.0}, "H*", 2),
          boundary(0, "L-H%")}},
        {"exclamation",
         {accent(0, {IntonationSelectorKind::Vowel, 0, 0.0}, "L+H*", 0),
          accent(0, {IntonationSelectorKind::LastVowel, 0, 0.0}, "H*", 0),
          boundary(0, "H-H%")}},
    };
    static const IntonationTune spanish[] = {
        {"statement",
         {accent(0, {IntonationSelectorKind::Vowel, 0, 0.0}, "H*", 0),
          boundary(0, "L-L%")}},
        {"yesno",
         {accent(0, {IntonationSelectorKind::LastVowel, 0, 0.0}, "L+H*", 0),
          boundary(0, "L-H%")}},
        {"wh",
         {accent(0, {IntonationSelectorKind::Vowel, 0, 0.0}, "H*", 0),
          boundary(0, "H-L%")}},
        {"list",
         {accent(0, {IntonationSelectorKind::Vowel, 0, 0.0}, "H*", 0),
          accent(0, {IntonationSelectorKind::Vowel, 1, 0.0}, "H*", 1),
          boundary(0, "L-H%")}},
        {"exclamation",
         {accent(0, {IntonationSelectorKind::Vowel, 0, 0.0}, "L+H*", 0),
          boundary(0, "H-H%")}},
    };
    const auto &table = language == Language::Spanish ? spanish : english;
    for (const IntonationTune &tune : table) {
        if (tune.name == name) {
            return &tune;
        }
    }
    return nullptr;
}

void applyIntonation(std::vector<Phoneme> &phonemes, Language language,
                     const IntonationFile &file,
                     const ToneInventory &inventory) {
    if (file.empty() || phonemes.empty()) {
        return;
    }
    std::vector<ToneEvent> events;
    if (file.tune) {
        const IntonationTune *tune = findIntonationTune(language, *file.tune);
        if (tune == nullptr) {
            throw std::invalid_argument(
                "intonation: unknown tune '" + *file.tune + "' for language '" +
                std::string(languageTag(language)) + "'");
        }
        for (ToneEvent event : tune->events) {
            event.fromTune = true;
            events.push_back(std::move(event));
        }
    }
    events.insert(events.end(), file.events.begin(), file.events.end());

    std::vector<std::pair<std::size_t, double>> insertions;
    for (const ToneEvent &event : events) {
        if (event.kind == ToneEventKind::Boundary) {
            const auto it = inventory.boundaries.find(event.label);
            if (it == inventory.boundaries.end()) {
                fail(event.line,
                     "unknown boundary label '" + event.label + "'");
            }
            for (std::size_t i = phonemes.size(); i-- > 0;) {
                if (phonemes[i].kind == PhonemeKind::Voiced) {
                    phonemes[i].pitchStart = it->second.start;
                    phonemes[i].pitchEnd = it->second.end;
                    break;
                }
            }
            continue;
        }
        if (event.kind == ToneEventKind::Break) {
            for (const std::size_t at :
                 resolve(event.selector, phonemes, event)) {
                const double seconds = inventory.breakTiers[event.tier];
                if (seconds > 0.0) {
                    insertions.emplace_back(at + 1, seconds);
                }
            }
            continue;
        }
        const auto it = inventory.accents.find(event.label);
        if (it == inventory.accents.end()) {
            fail(event.line, "unknown accent label '" + event.label + "'");
        }
        const ToneShape shape =
            scaled(it->second, event.downsteps, inventory.downstep);
        for (const std::size_t at : resolve(event.selector, phonemes, event)) {
            phonemes[at].pitchStart = shape.start;
            phonemes[at].pitchEnd = shape.end;
        }
    }

    // Insert pauses from the end so earlier positions remain valid. Each pause
    // is a plain silence phoneme, the same unit SSML <break> emits.
    std::sort(insertions.begin(), insertions.end(), std::greater<>{});
    for (const auto &insertion : insertions) {
        Phoneme gap = *findPhoneme("_");
        gap.duration = insertion.second;
        phonemes.insert(phonemes.begin() +
                            static_cast<std::ptrdiff_t>(insertion.first),
                        gap);
    }
}

} // namespace formant
