#include "language_internal.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "formant/ipa.h"
#include "formant/language.h"

namespace formant {
namespace {

// ---------------------------------------------------------------------------
// UTF-8 helpers. Text is decoded explicitly so Spanish accents never depend on
// the process locale.
// ---------------------------------------------------------------------------

bool decodeUtf8(std::string_view text, size_t &pos, uint32_t &codePoint) {
    if (pos >= text.size()) {
        return false;
    }
    const auto byte = [&](size_t at) {
        return static_cast<unsigned char>(text[at]);
    };
    const uint32_t first = byte(pos);
    if (first < 0x80) {
        codePoint = first;
        ++pos;
        return true;
    }
    int extra = 0;
    uint32_t value = 0;
    if (first >= 0xC2 && first <= 0xDF) {
        extra = 1;
        value = first & 0x1Fu;
    } else if (first >= 0xE0 && first <= 0xEF) {
        extra = 2;
        value = first & 0x0Fu;
    } else if (first >= 0xF0 && first <= 0xF4) {
        extra = 3;
        value = first & 0x07u;
    } else {
        return false;
    }
    if (pos + static_cast<size_t>(extra) >= text.size()) {
        return false;
    }
    for (int i = 1; i <= extra; ++i) {
        const uint32_t next = byte(pos + static_cast<size_t>(i));
        if ((next & 0xC0u) != 0x80u) {
            return false;
        }
        value = (value << 6) | (next & 0x3Fu);
    }
    if ((extra == 2 && value < 0x800) || (extra == 3 && value < 0x10000) ||
        value > 0x10FFFF || (value >= 0xD800 && value <= 0xDFFF)) {
        return false;
    }
    codePoint = value;
    pos += static_cast<size_t>(extra) + 1;
    return true;
}

std::string asciiLower(std::string_view text) {
    std::string result;
    result.reserve(text.size());
    for (const unsigned char c : text) {
        result.push_back(
            static_cast<char>(c >= 'A' && c <= 'Z' ? c + ('a' - 'A') : c));
    }
    return result;
}

uint32_t lowerCodePoint(uint32_t codePoint) {
    if (codePoint >= 'A' && codePoint <= 'Z') {
        return codePoint + ('a' - 'A');
    }
    switch (codePoint) {
    case 0x00C1: // Á
        return 0x00E1;
    case 0x00C9: // É
        return 0x00E9;
    case 0x00CD: // Í
        return 0x00ED;
    case 0x00D3: // Ó
        return 0x00F3;
    case 0x00DA: // Ú
        return 0x00FA;
    case 0x00DC: // Ü
        return 0x00FC;
    case 0x00D1: // Ñ
        return 0x00F1;
    default:
        return codePoint;
    }
}

bool isLatinLetter(uint32_t codePoint) {
    if ((codePoint >= 'a' && codePoint <= 'z') ||
        (codePoint >= 'A' && codePoint <= 'Z')) {
        return true;
    }
    // Latin-1 letters, excluding the multiplication and division signs.
    if (codePoint >= 0x00C0 && codePoint <= 0x00FF && codePoint != 0x00D7 &&
        codePoint != 0x00F7) {
        return true;
    }
    // Latin Extended-A, enough for the accented letters used by European
    // languages even though only the Spanish set is interpreted today.
    return codePoint >= 0x0100 && codePoint <= 0x017F;
}

bool isAsciiDigit(uint32_t codePoint) {
    return codePoint >= '0' && codePoint <= '9';
}

bool isWordCodePoint(uint32_t codePoint) {
    return isLatinLetter(codePoint) || isAsciiDigit(codePoint);
}

// ---------------------------------------------------------------------------
// Castilian Spanish phonology.
// ---------------------------------------------------------------------------

uint32_t baseVowel(uint32_t codePoint) {
    switch (codePoint) {
    case 0x00E1: // á
        return 'a';
    case 0x00E9: // é
        return 'e';
    case 0x00ED: // í
        return 'i';
    case 0x00F3: // ó
        return 'o';
    case 0x00FA: // ú
    case 0x00FC: // ü
        return 'u';
    default:
        return codePoint;
    }
}

bool isAccentedVowel(uint32_t codePoint) {
    return codePoint == 0x00E1 || codePoint == 0x00E9 || codePoint == 0x00ED ||
           codePoint == 0x00F3 || codePoint == 0x00FA;
}

bool isOralVowel(uint32_t codePoint) {
    codePoint = baseVowel(codePoint);
    return codePoint == 'a' || codePoint == 'e' || codePoint == 'i' ||
           codePoint == 'o' || codePoint == 'u';
}

// A final y is the vowel /i/ (rey, buey); elsewhere it is the consonant /ʝ/.
bool isStressVowelAt(const std::vector<uint32_t> &word, size_t index) {
    if (isOralVowel(word[index])) {
        return true;
    }
    return baseVowel(word[index]) == 'y' && index + 1 == word.size();
}

bool isStrongAt(const std::vector<uint32_t> &word, size_t index) {
    const uint32_t vowel = baseVowel(word[index]);
    return vowel == 'a' || vowel == 'e' || vowel == 'o';
}

// The u in que/qui and gue/gui is part of the consonant digraph, not a
// syllable nucleus. The diaeresis (güe/güi) deliberately keeps its u.
bool isSilentU(const std::vector<uint32_t> &word, size_t index) {
    if (word[index] != 'u' || index == 0 || index + 1 >= word.size()) {
        return false;
    }
    const uint32_t previous = word[index - 1];
    if (previous != 'q' && previous != 'g') {
        return false;
    }
    const uint32_t next = baseVowel(word[index + 1]);
    return next == 'e' || next == 'i';
}

bool combineIntoNucleus(const std::vector<uint32_t> &word, size_t first,
                        size_t second) {
    // Two strong vowels are separate syllables (po-e-ta); every other pair
    // forms a diphthong or triphthong.
    return !(isStrongAt(word, first) && isStrongAt(word, second));
}

bool hasNucleusNeighbour(const std::vector<uint32_t> &word, size_t index) {
    if (index > 0 && !isSilentU(word, index - 1) &&
        isStressVowelAt(word, index - 1) &&
        combineIntoNucleus(word, index - 1, index)) {
        return true;
    }
    if (index + 1 < word.size() && isStressVowelAt(word, index + 1) &&
        combineIntoNucleus(word, index, index + 1)) {
        return true;
    }
    return false;
}

// Returns the index of the stressed vowel, or npos for a word without
// vowels. A written acute accent wins; otherwise Spanish stresses the
// penultimate syllable when the word ends in a vowel, n or s, and the final
// syllable otherwise.
size_t stressVowelIndex(const std::vector<uint32_t> &word) {
    constexpr size_t none = std::string::npos;
    for (size_t i = 0; i < word.size(); ++i) {
        if (isAccentedVowel(word[i])) {
            return i;
        }
    }

    std::vector<std::vector<size_t>> nuclei;
    for (size_t i = 0; i < word.size(); ++i) {
        if (!isStressVowelAt(word, i) || isSilentU(word, i)) {
            continue;
        }
        if (!nuclei.empty() && nuclei.back().back() + 1 == i &&
            combineIntoNucleus(word, nuclei.back().back(), i)) {
            nuclei.back().push_back(i);
        } else {
            nuclei.push_back({i});
        }
    }
    if (nuclei.empty()) {
        return none;
    }

    size_t nucleus = nuclei.size() - 1;
    const uint32_t finalBase = baseVowel(word.back());
    const bool endsLikePenultimate = isStressVowelAt(word, word.size() - 1) ||
                                     finalBase == 'n' || finalBase == 's';
    if (endsLikePenultimate && nucleus > 0) {
        --nucleus;
    }

    // A strong vowel carries the stress inside a diphthong. A weak+weak
    // nucleus (ui, iu) stresses its second element.
    for (size_t index : nuclei[nucleus]) {
        if (isStrongAt(word, index)) {
            return index;
        }
    }
    return nuclei[nucleus].back();
}

std::string_view digitIpa(uint32_t digit) {
    switch (digit) {
    case 0:
        return "\u02C8\u03B8e\u027Eo"; // cero
    case 1:
        return "\u02C8uno"; // uno
    case 2:
        return "\u02C8dos"; // dos
    case 3:
        return "\u02C8t\u027Ees"; // tres
    case 4:
        return "\u02C8kwat\u027Eo"; // cuatro
    case 5:
        return "\u02C8\u03B8i\u014Bko"; // cinco
    case 6:
        return "\u02C8sejs"; // seis
    case 7:
        return "\u02C8sjete"; // siete
    case 8:
        return "\u02C8ot\u0283o"; // ocho
    default:
        return "\u02C8nwebe"; // nueve
    }
}

std::string spanishWordIpa(const std::vector<uint32_t> &word) {
    if (word.empty()) {
        return {};
    }
    const size_t stressed = stressVowelIndex(word);
    const size_t count = word.size();
    std::string ipa;
    ipa.reserve(count * 2);

    const auto baseAt = [&](size_t index) { return baseVowel(word[index]); };
    const auto nextBase = [&](size_t index) -> uint32_t {
        return index + 1 < count ? baseAt(index + 1) : 0;
    };
    const auto prevBase = [&](size_t index) -> uint32_t {
        return index > 0 ? baseAt(index - 1) : 0;
    };
    const auto emitVowel = [&](size_t index) {
        const uint32_t vowel = baseAt(index);
        std::string symbol(1, static_cast<char>(vowel));
        if ((vowel == 'i' || vowel == 'u') && index != stressed &&
            hasNucleusNeighbour(word, index)) {
            symbol = vowel == 'i' ? "j" : "w";
        }
        if (index == stressed) {
            ipa += "\u02C8";
        }
        ipa += symbol;
    };

    for (size_t i = 0; i < count; ++i) {
        const uint32_t cp = word[i];

        if (isAsciiDigit(cp)) {
            ipa += digitIpa(cp - '0');
            continue;
        }
        if (cp == 0x00F1) { // ñ
            ipa += "\u0272";
            continue;
        }
        if (isOralVowel(cp)) {
            emitVowel(i);
            continue;
        }

        switch (cp) {
        case 'b':
        case 'v':
            ipa += "b";
            break;
        case 'c':
            if (i + 1 < count && word[i + 1] == 'h') {
                // ch is a single affricate, not /k/ plus a silent h.
                ipa += "t\u0361\u0283";
                ++i;
            } else {
                ipa +=
                    (nextBase(i) == 'e' || nextBase(i) == 'i') ? "\u03B8" : "k";
            }
            break;
        case 'd':
            ipa += "d";
            break;
        case 'f':
            ipa += "f";
            break;
        case 'g':
            if (nextBase(i) == 'e' || nextBase(i) == 'i') {
                ipa += "x";
            } else if (i + 2 < count && word[i + 1] == 'u' &&
                       (baseAt(i + 2) == 'e' || baseAt(i + 2) == 'i')) {
                // gu + front vowel: the u is silent (guerra, guitarra).
                ipa += "g";
                ++i;
            } else {
                ipa += "g";
            }
            break;
        case 'h':
            // Silent in Spanish.
            break;
        case 'j':
            ipa += "x";
            break;
        case 'k':
            ipa += "k";
            break;
        case 'l':
            if (i + 1 < count && word[i + 1] == 'l') {
                // Yeísmo: ll is one palatal approximant, not two laterals.
                ipa += "\u029D";
                ++i;
            } else {
                ipa += "l";
            }
            break;
        case 'm':
            ipa += "m";
            break;
        case 'n':
            ipa += "n";
            break;
        case 'p':
            ipa += "p";
            break;
        case 'q':
            ipa += "k";
            if (i + 2 < count && word[i + 1] == 'u' &&
                (baseAt(i + 2) == 'e' || baseAt(i + 2) == 'i')) {
                ++i;
            }
            break;
        case 'r':
            if (i + 1 < count && word[i + 1] == 'r') {
                ipa += "r";
                ++i;
            } else if (i == 0 || prevBase(i) == 'l' || prevBase(i) == 'n' ||
                       prevBase(i) == 's') {
                // Word-initial and post-nasal/lateral r is the trill.
                ipa += "r";
            } else {
                ipa += "\u027E";
            }
            break;
        case 's':
            ipa += "s";
            break;
        case 't':
            ipa += "t";
            break;
        case 'w':
            ipa += "w";
            break;
        case 'x':
            ipa += "ks";
            break;
        case 'y':
            if (i + 1 == count) {
                ipa += (i > 0 && isStressVowelAt(word, i - 1)) ? "j" : "i";
            } else {
                ipa += "\u029D";
            }
            break;
        case 'z':
            ipa += "\u03B8";
            break;
        default:
            break;
        }
    }
    return ipa;
}

std::vector<Phoneme> spanishPhonemes(std::string_view text) {
    std::vector<Phoneme> result;
    std::vector<uint32_t> word;
    size_t pos = 0;

    const auto appendPause = [&](std::string_view name) {
        if (const Phoneme *pause = findPhoneme(name)) {
            result.push_back(*pause);
        }
    };

    const auto flush = [&]() {
        if (word.empty()) {
            return;
        }
        const std::string ipa = spanishWordIpa(word);
        if (!ipa.empty()) {
            const std::vector<Phoneme> phonemes = parseIpa(ipa);
            result.insert(result.end(), phonemes.begin(), phonemes.end());
            if (const Phoneme *gapPhoneme = findPhoneme("_")) {
                Phoneme gap = *gapPhoneme;
                gap.name = "|";
                gap.duration = 0.035;
                result.push_back(gap);
            }
        }
        word.clear();
    };

    while (pos < text.size()) {
        uint32_t cp = 0;
        if (!decodeUtf8(text, pos, cp)) {
            throw std::invalid_argument("invalid UTF-8 in text");
        }
        cp = lowerCodePoint(cp);
        if (isWordCodePoint(cp)) {
            word.push_back(cp);
            continue;
        }
        flush();
        if (cp == ',') {
            appendPause(",");
        } else if (cp == '?') {
            appendPause("?");
        } else if (cp == '!') {
            appendPause("!");
        } else if (cp == '.' || cp == ';' || cp == ':' || cp == 0x2026) {
            appendPause(".");
        }
        // Inverted ¿/¡, quotation marks and dashes only separate words.
    }
    flush();

    while (!result.empty() && result.back().name == "|") {
        result.pop_back();
    }
    return result;
}

// ---------------------------------------------------------------------------
// User-defined language files.
// ---------------------------------------------------------------------------

std::string_view trimView(std::string_view text) {
    size_t begin = 0;
    while (begin < text.size() &&
           std::isspace(static_cast<unsigned char>(text[begin]))) {
        ++begin;
    }
    size_t end = text.size();
    while (end > begin &&
           std::isspace(static_cast<unsigned char>(text[end - 1]))) {
        --end;
    }
    return text.substr(begin, end - begin);
}

std::vector<std::string> splitFields(std::string_view line) {
    std::vector<std::string> fields;
    size_t pos = 0;
    while (pos < line.size()) {
        while (pos < line.size() &&
               std::isspace(static_cast<unsigned char>(line[pos]))) {
            ++pos;
        }
        if (pos == line.size()) {
            break;
        }
        size_t stop = pos;
        while (stop < line.size() &&
               !std::isspace(static_cast<unsigned char>(line[stop]))) {
            ++stop;
        }
        fields.emplace_back(line.substr(pos, stop - pos));
        pos = stop;
    }
    return fields;
}

std::string joinFields(const std::vector<std::string> &fields, size_t begin) {
    std::string result;
    for (size_t i = begin; i < fields.size(); ++i) {
        if (!result.empty()) {
            result.push_back(' ');
        }
        result += fields[i];
    }
    return result;
}

[[noreturn]] void languageFileError(size_t line, const std::string &message) {
    throw std::invalid_argument("line " + std::to_string(line) + ": " +
                                message);
}

double languageFileNumber(std::string_view text, size_t line) {
    if (text.empty()) {
        languageFileError(line, "missing number");
    }
    const std::string copy(text);
    char *end = nullptr;
    const double value = std::strtod(copy.c_str(), &end);
    if (end != copy.c_str() + copy.size() || !std::isfinite(value)) {
        languageFileError(line, "invalid number: " + copy);
    }
    return value;
}

void applyPhonemeOverride(Phoneme &phoneme, const PhonemeOverride &changes) {
    if (changes.durationScale) {
        phoneme.duration *= *changes.durationScale;
    }
    if (changes.amplitude) {
        phoneme.amplitude = *changes.amplitude;
    }
    if (changes.pitchStart) {
        phoneme.pitchStart = *changes.pitchStart;
    }
    if (changes.pitchEnd) {
        phoneme.pitchEnd = *changes.pitchEnd;
    }
    if (changes.formantScale) {
        for (FormantBand &band : phoneme.bands) {
            band.frequency *= *changes.formantScale;
        }
        if (phoneme.hasEndBands) {
            for (FormantBand &band : phoneme.endBands) {
                band.frequency *= *changes.formantScale;
            }
        }
    }
}

using RuleClasses = std::unordered_map<std::string, std::vector<std::string>>;

struct RuleItem {
    bool boundary = false;
    // A literal has one alternative; a category has one alternative per
    // member. Alternatives are stored longest first for greedy matching.
    std::vector<std::vector<std::string>> alternatives;
};

struct RewriteRule {
    std::vector<RuleItem> pattern;
    std::vector<std::string> replacement;
    std::vector<RuleItem> before;
    std::vector<RuleItem> after;
    std::string text;
};

struct RuleToken {
    std::string symbol;
    size_t source = static_cast<size_t>(-1);
};

constexpr size_t noRuleSource = static_cast<size_t>(-1);

std::vector<std::string> splitGraphemeUnits(std::string_view text) {
    std::vector<std::string> units;
    size_t pos = 0;
    while (pos < text.size()) {
        const size_t start = pos;
        uint32_t codePoint = 0;
        if (!decodeUtf8(text, pos, codePoint)) {
            throw std::invalid_argument("invalid UTF-8 in language rule");
        }
        units.emplace_back(text.substr(start, pos - start));
    }
    return units;
}

std::vector<std::string> ruleUnits(std::string_view token, bool graphemePhase) {
    if (graphemePhase) {
        return splitGraphemeUnits(token);
    }
    return {std::string(token)};
}

RuleItem makeRuleItem(const std::string &token, bool graphemePhase,
                      const RuleClasses &classes, bool allowBoundary) {
    RuleItem item;
    if (token == "#") {
        if (!allowBoundary) {
            throw std::invalid_argument(
                "boundary '#' is only allowed in a rule environment");
        }
        item.boundary = true;
        return item;
    }

    if (const auto found = classes.find(token); found != classes.end()) {
        for (const std::string &member : found->second) {
            item.alternatives.push_back(ruleUnits(member, graphemePhase));
        }
    } else {
        item.alternatives.push_back(ruleUnits(token, graphemePhase));
    }
    if (item.alternatives.empty() || item.alternatives.front().empty()) {
        throw std::invalid_argument("rule item is empty");
    }
    std::stable_sort(item.alternatives.begin(), item.alternatives.end(),
                     [](const std::vector<std::string> &left,
                        const std::vector<std::string> &right) {
                         return left.size() > right.size();
                     });
    return item;
}

RewriteRule compileRule(std::string_view body, bool graphemePhase,
                        const RuleClasses &classes) {
    const std::vector<std::string> fields = splitFields(body);
    if (fields.empty()) {
        throw std::invalid_argument("rule is empty");
    }

    const auto greater = std::find(fields.begin(), fields.end(), ">");
    if (greater == fields.end()) {
        throw std::invalid_argument("rule needs '>'");
    }
    if (std::find(greater + 1, fields.end(), ">") != fields.end()) {
        throw std::invalid_argument("rule has more than one '>'");
    }
    const auto slash = std::find(greater + 1, fields.end(), "/");
    if (slash != fields.end() &&
        std::find(slash + 1, fields.end(), "/") != fields.end()) {
        throw std::invalid_argument("rule has more than one '/'");
    }

    const size_t greaterIndex = static_cast<size_t>(greater - fields.begin());
    const size_t slashIndex = slash == fields.end()
                                  ? fields.size()
                                  : static_cast<size_t>(slash - fields.begin());
    if (greaterIndex == 0) {
        throw std::invalid_argument("rule needs a pattern");
    }
    if (greaterIndex + 1 == slashIndex) {
        throw std::invalid_argument(
            "rule needs a replacement; use 0 to delete");
    }

    RewriteRule rule;
    for (size_t i = 0; i < greaterIndex; ++i) {
        rule.pattern.push_back(
            makeRuleItem(fields[i], graphemePhase, classes, false));
    }

    const bool deletion =
        slashIndex == greaterIndex + 2 && fields[greaterIndex + 1] == "0";
    if (!deletion) {
        for (size_t i = greaterIndex + 1; i < slashIndex; ++i) {
            const std::string &unit = fields[i];
            if (unit == "0") {
                throw std::invalid_argument("0 must be the whole replacement");
            }
            if (unit == "#") {
                throw std::invalid_argument(
                    "boundary '#' cannot appear in a replacement");
            }
            if (classes.find(unit) != classes.end()) {
                throw std::invalid_argument("class \"" + unit +
                                            "\" cannot appear in a "
                                            "replacement");
            }
            const std::vector<std::string> units =
                ruleUnits(unit, graphemePhase);
            rule.replacement.insert(rule.replacement.end(), units.begin(),
                                    units.end());
        }
    }

    if (slashIndex < fields.size()) {
        const auto underscore =
            std::find(fields.begin() + slashIndex + 1, fields.end(), "_");
        if (underscore == fields.end()) {
            throw std::invalid_argument("rule environment needs '_'");
        }
        if (std::find(underscore + 1, fields.end(), "_") != fields.end()) {
            throw std::invalid_argument(
                "rule environment has more than one '_'");
        }
        const size_t underscoreIndex =
            static_cast<size_t>(underscore - fields.begin());
        for (size_t i = slashIndex + 1; i < underscoreIndex; ++i) {
            rule.before.push_back(
                makeRuleItem(fields[i], graphemePhase, classes, true));
        }
        for (size_t i = underscoreIndex + 1; i < fields.size(); ++i) {
            rule.after.push_back(
                makeRuleItem(fields[i], graphemePhase, classes, true));
        }
    }

    rule.text = std::string(body);
    return rule;
}

bool matchesAlternative(const std::vector<RuleToken> &tokens, size_t pos,
                        const std::vector<std::string> &alternative) {
    if (alternative.size() > tokens.size() - pos) {
        return false;
    }
    for (size_t i = 0; i < alternative.size(); ++i) {
        if (tokens[pos + i].symbol != alternative[i]) {
            return false;
        }
    }
    return true;
}

bool matchPattern(const std::vector<RuleToken> &tokens, size_t pos,
                  const std::vector<RuleItem> &items, size_t index,
                  size_t &end) {
    if (index == items.size()) {
        end = pos;
        return true;
    }
    const RuleItem &item = items[index];
    if (item.boundary) {
        return false;
    }
    for (const auto &alternative : item.alternatives) {
        if (matchesAlternative(tokens, pos, alternative) &&
            matchPattern(tokens, pos + alternative.size(), items, index + 1,
                         end)) {
            return true;
        }
    }
    return false;
}

bool matchBefore(const std::vector<RuleToken> &tokens, size_t end,
                 const std::vector<RuleItem> &items, size_t remaining) {
    if (remaining == 0) {
        return true;
    }
    const RuleItem &item = items[remaining - 1];
    if (item.boundary) {
        return end == 0 && matchBefore(tokens, 0, items, remaining - 1);
    }
    for (const auto &alternative : item.alternatives) {
        if (alternative.size() <= end &&
            matchesAlternative(tokens, end - alternative.size(), alternative) &&
            matchBefore(tokens, end - alternative.size(), items,
                        remaining - 1)) {
            return true;
        }
    }
    return false;
}

bool matchAfter(const std::vector<RuleToken> &tokens, size_t pos,
                const std::vector<RuleItem> &items, size_t index) {
    if (index == items.size()) {
        return true;
    }
    const RuleItem &item = items[index];
    if (item.boundary) {
        return pos == tokens.size() &&
               matchAfter(tokens, pos, items, index + 1);
    }
    for (const auto &alternative : item.alternatives) {
        if (matchesAlternative(tokens, pos, alternative) &&
            matchAfter(tokens, pos + alternative.size(), items, index + 1)) {
            return true;
        }
    }
    return false;
}

std::vector<RuleToken> applyRewriteRule(const std::vector<RuleToken> &input,
                                        const RewriteRule &rule,
                                        std::vector<std::string> *trace,
                                        std::string_view phase,
                                        size_t ruleIndex) {
    std::vector<RuleToken> output;
    output.reserve(input.size());
    size_t pos = 0;
    while (pos < input.size()) {
        size_t end = pos;
        if (matchPattern(input, pos, rule.pattern, 0, end) && end > pos &&
            matchBefore(input, pos, rule.before, rule.before.size()) &&
            matchAfter(input, end, rule.after, 0)) {
            if (trace != nullptr) {
                trace->push_back(std::string(phase) + " " +
                                 std::to_string(ruleIndex + 1) + " at " +
                                 std::to_string(pos) + ": " + rule.text);
            }
            for (const std::string &unit : rule.replacement) {
                output.push_back({unit, noRuleSource});
            }
            pos = end;
        } else {
            output.push_back(input[pos]);
            ++pos;
        }
    }
    return output;
}

std::vector<RuleToken>
applyRules(std::vector<RuleToken> tokens, const std::vector<std::string> &rules,
           bool graphemePhase, const RuleClasses &classes,
           std::vector<std::string> *trace, std::string_view phase) {
    for (size_t i = 0; i < rules.size(); ++i) {
        const RewriteRule rule = compileRule(rules[i], graphemePhase, classes);
        tokens = applyRewriteRule(tokens, rule, trace, phase, i);
    }
    return tokens;
}

std::string applyGraphemeRules(std::string_view word,
                               const LanguageDefinition &language,
                               std::vector<std::string> *trace) {
    if (language.graphemeRules.empty()) {
        return std::string(word);
    }
    std::vector<RuleToken> tokens;
    for (const std::string &unit : splitGraphemeUnits(word)) {
        tokens.push_back({unit, noRuleSource});
    }
    tokens = applyRules(std::move(tokens), language.graphemeRules, true,
                        language.classes, trace, "grapheme-rule");
    std::string result;
    for (const RuleToken &token : tokens) {
        result += token.symbol;
    }
    return result;
}

std::vector<Phoneme> applyPhonemeRules(const std::vector<Phoneme> &input,
                                       const LanguageDefinition &language,
                                       std::vector<std::string> *trace) {
    if (language.phonemeRules.empty()) {
        return input;
    }
    std::vector<RuleToken> tokens;
    tokens.reserve(input.size());
    for (size_t i = 0; i < input.size(); ++i) {
        tokens.push_back({input[i].name, i});
    }
    tokens = applyRules(std::move(tokens), language.phonemeRules, false,
                        language.classes, trace, "phoneme-rule");

    std::vector<Phoneme> result;
    for (const RuleToken &token : tokens) {
        if (token.source != noRuleSource) {
            result.push_back(input[token.source]);
            continue;
        }
        try {
            const std::vector<Phoneme> replacement = parseIpa(token.symbol);
            result.insert(result.end(), replacement.begin(), replacement.end());
        } catch (const std::invalid_argument &error) {
            throw std::invalid_argument(
                "invalid IPA in phoneme-rule replacement \"" + token.symbol +
                "\": " + error.what());
        }
    }
    return result;
}

void finalizeConlangPhonemes(std::vector<Phoneme> &phonemes,
                             const LanguageDefinition &language) {
    for (Phoneme &phoneme : phonemes) {
        const auto found = language.phonemes.find(phoneme.name);
        if (found != language.phonemes.end()) {
            applyPhonemeOverride(phoneme, found->second);
        }
        if (language.strictInventory && found == language.phonemes.end()) {
            throw std::invalid_argument("phoneme \"" + phoneme.name +
                                        "\" is not declared in the " +
                                        language.name + " inventory");
        }
    }
}

std::vector<Phoneme> mapConlangWord(std::string_view word,
                                    const LanguageDefinition &language,
                                    std::vector<std::string> *trace) {
    const std::string key = asciiLower(word);
    std::vector<Phoneme> result;
    if (const auto found = language.words.find(key);
        found != language.words.end()) {
        result = parseIpa(found->second);
    } else {
        const std::string spelled = applyGraphemeRules(key, language, trace);

        size_t longest = 0;
        for (const auto &entry : language.graphemes) {
            longest = std::max(longest, entry.first.size());
        }

        size_t pos = 0;
        while (pos < spelled.size()) {
            bool matched = false;
            const size_t limit = std::min(longest, spelled.size() - pos);
            for (size_t length = limit; length > 0; --length) {
                const auto found =
                    language.graphemes.find(spelled.substr(pos, length));
                if (found == language.graphemes.end()) {
                    continue;
                }
                const std::vector<Phoneme> mapped = parseIpa(found->second);
                result.insert(result.end(), mapped.begin(), mapped.end());
                pos += length;
                matched = true;
                break;
            }
            if (matched) {
                continue;
            }

            size_t length = 1;
            while (pos + length < spelled.size() &&
                   (static_cast<unsigned char>(spelled[pos + length]) &
                    0xC0u) == 0x80u) {
                ++length;
            }
            if (language.strictInventory) {
                throw std::invalid_argument("no grapheme mapping for \"" +
                                            spelled.substr(pos, length) +
                                            "\" in " + language.name);
            }
            pos += length;
        }
    }

    result = applyPhonemeRules(result, language, trace);
    finalizeConlangPhonemes(result, language);
    return result;
}

bool isConlangPause(uint32_t codePoint) {
    return codePoint == ',' || codePoint == '.' || codePoint == ';' ||
           codePoint == ':' || codePoint == '?' || codePoint == '!' ||
           codePoint == 0x2026;
}

bool isConlangSeparator(uint32_t codePoint) {
    if (codePoint < 0x80) {
        return std::isspace(static_cast<unsigned char>(codePoint)) != 0 ||
               codePoint == '"' || codePoint == '(' || codePoint == ')' ||
               codePoint == '[' || codePoint == ']' || codePoint == '{' ||
               codePoint == '}' || codePoint == '/' || codePoint == '\\';
    }
    return codePoint == 0x201C || codePoint == 0x201D || codePoint == 0x2018 ||
           codePoint == 0x2019 || codePoint == 0x00AB || codePoint == 0x00BB ||
           codePoint == 0x2013 || codePoint == 0x2014;
}

std::vector<Phoneme> conlangPhonemes(std::string_view text,
                                     const LanguageDefinition &language,
                                     std::vector<std::string> *trace) {
    std::vector<Phoneme> result;
    std::string word;
    size_t pos = 0;

    const auto appendPause = [&](std::string_view name) {
        if (const Phoneme *pause = findPhoneme(name)) {
            result.push_back(*pause);
        }
    };

    const auto flush = [&]() {
        if (word.empty()) {
            return;
        }
        const std::vector<Phoneme> mapped =
            mapConlangWord(word, language, trace);
        if (!mapped.empty()) {
            result.insert(result.end(), mapped.begin(), mapped.end());
            if (const Phoneme *gapPhoneme = findPhoneme("_")) {
                Phoneme gap = *gapPhoneme;
                gap.name = "|";
                gap.duration = 0.035;
                result.push_back(gap);
            }
        }
        word.clear();
    };

    while (pos < text.size()) {
        const size_t start = pos;
        uint32_t cp = 0;
        if (!decodeUtf8(text, pos, cp)) {
            throw std::invalid_argument("invalid UTF-8 in text");
        }
        if (isConlangPause(cp)) {
            flush();
            if (cp == ',') {
                appendPause(",");
            } else if (cp == '?') {
                appendPause("?");
            } else if (cp == '!') {
                appendPause("!");
            } else {
                appendPause(".");
            }
        } else if (isConlangSeparator(cp)) {
            flush();
        } else {
            word.append(text.data() + start, pos - start);
        }
    }
    flush();

    while (!result.empty() && result.back().name == "|") {
        result.pop_back();
    }
    return result;
}

} // namespace

std::vector<Phoneme> phonemizeSpanish(std::string_view text) {
    return spanishPhonemes(text);
}

std::string_view languageTag(Language language) {
    switch (language) {
    case Language::English:
        return "en";
    case Language::Spanish:
        return "es";
    }
    return "";
}

std::string_view languageName(Language language) {
    switch (language) {
    case Language::English:
        return "English";
    case Language::Spanish:
        return "Spanish";
    }
    return "Unknown";
}

std::vector<std::string> languageTags() { return {"en", "es"}; }

Language languageFromTag(std::string_view tag) {
    const std::string lowered = asciiLower(tag);
    const size_t separator = lowered.find_first_of("-_");
    const std::string_view primary =
        separator == std::string::npos
            ? std::string_view(lowered)
            : std::string_view(lowered).substr(0, separator);
    if (primary == "en") {
        return Language::English;
    }
    if (primary == "es") {
        return Language::Spanish;
    }
    throw std::invalid_argument("unsupported language tag: " +
                                std::string(tag) + " (supported: en, es)");
}

std::vector<Phoneme> phonemize(std::string_view text, Language language) {
    switch (language) {
    case Language::English:
        return phonemizeEnglish(text);
    case Language::Spanish:
        return phonemizeSpanish(text);
    }
    throw std::invalid_argument("unsupported language");
}

LanguageDefinition parseLanguageDefinition(std::string_view text) {
    LanguageDefinition definition;
    std::vector<std::pair<std::string, size_t>> graphemeRuleLines;
    std::vector<std::pair<std::string, size_t>> phonemeRuleLines;
    size_t lineNumber = 0;
    size_t pos = 0;

    while (pos <= text.size()) {
        size_t end = text.find('\n', pos);
        if (end == std::string_view::npos) {
            end = text.size();
        }
        std::string_view line = text.substr(pos, end - pos);
        ++lineNumber;
        line = trimView(line);
        // '#' is the word-boundary token inside a rule, so rule lines reserve
        // it and keep comments on their own lines. The directive may be
        // followed by any whitespace, not just a space.
        const auto startsWithRule = [&](std::string_view directive) {
            if (line.substr(0, directive.size()) != directive) {
                return false;
            }
            return line.size() == directive.size() ||
                   std::isspace(
                       static_cast<unsigned char>(line[directive.size()])) != 0;
        };
        const bool ruleLine =
            startsWithRule("grapheme-rule") || startsWithRule("phoneme-rule");
        if (!ruleLine) {
            if (const size_t comment = line.find('#');
                comment != std::string_view::npos) {
                line = line.substr(0, comment);
            }
        }
        line = trimView(line);

        if (!line.empty()) {
            const std::vector<std::string> fields = splitFields(line);
            const std::string &directive = fields.front();
            if (directive == "name") {
                definition.name = joinFields(fields, 1);
                if (definition.name.empty()) {
                    languageFileError(lineNumber, "name needs a value");
                }
            } else if (directive == "tag") {
                definition.tag = joinFields(fields, 1);
                if (definition.tag.empty()) {
                    languageFileError(lineNumber, "tag needs a value");
                }
            } else if (directive == "strict") {
                if (fields.size() != 1) {
                    languageFileError(lineNumber, "strict takes no value");
                }
                definition.strictInventory = true;
            } else if (directive == "lenient") {
                if (fields.size() != 1) {
                    languageFileError(lineNumber, "lenient takes no value");
                }
                definition.strictInventory = false;
            } else if (directive == "phoneme") {
                if (fields.size() < 2) {
                    languageFileError(lineNumber,
                                      "phoneme needs an IPA symbol");
                }
                PhonemeOverride &changes = definition.phonemes[fields[1]];
                for (size_t i = 2; i < fields.size(); ++i) {
                    const size_t equal = fields[i].find('=');
                    if (equal == std::string::npos) {
                        languageFileError(lineNumber,
                                          "expected key=value, got: " +
                                              fields[i]);
                    }
                    const std::string key = fields[i].substr(0, equal);
                    const double value = languageFileNumber(
                        std::string_view(fields[i]).substr(equal + 1),
                        lineNumber);
                    if (key == "duration-scale") {
                        if (value <= 0.0) {
                            languageFileError(
                                lineNumber, "duration-scale must be positive");
                        }
                        changes.durationScale = value;
                    } else if (key == "amplitude") {
                        if (value < 0.0) {
                            languageFileError(lineNumber,
                                              "amplitude must not be negative");
                        }
                        changes.amplitude = value;
                    } else if (key == "pitch-start") {
                        if (value <= 0.0) {
                            languageFileError(lineNumber,
                                              "pitch-start must be positive");
                        }
                        changes.pitchStart = value;
                    } else if (key == "pitch-end") {
                        if (value <= 0.0) {
                            languageFileError(lineNumber,
                                              "pitch-end must be positive");
                        }
                        changes.pitchEnd = value;
                    } else if (key == "formant-scale") {
                        if (value <= 0.0) {
                            languageFileError(lineNumber,
                                              "formant-scale must be positive");
                        }
                        changes.formantScale = value;
                    } else {
                        languageFileError(lineNumber,
                                          "unknown override: " + key);
                    }
                }
            } else if (directive == "grapheme" || directive == "word") {
                if (fields.size() < 2) {
                    languageFileError(lineNumber,
                                      directive + " needs a spelling");
                }
                const std::string key = asciiLower(fields[1]);
                const std::string ipa = joinFields(fields, 2);
                if (directive == "grapheme") {
                    definition.graphemes[key] = ipa;
                } else {
                    definition.words[key] = ipa;
                }
            } else if (directive == "class") {
                if (fields.size() < 2) {
                    languageFileError(lineNumber, "class needs a name");
                }
                if (fields[1] == "#") {
                    languageFileError(lineNumber, "class name '#' is reserved");
                }
                if (fields.size() == 2) {
                    languageFileError(lineNumber, "class " + fields[1] +
                                                      " needs at least one "
                                                      "member");
                }
                definition.classes[fields[1]] =
                    std::vector<std::string>(fields.begin() + 2, fields.end());
            } else if (directive == "grapheme-rule" ||
                       directive == "phoneme-rule") {
                std::string body = joinFields(fields, 1);
                if (body.empty()) {
                    languageFileError(lineNumber, directive + " needs a rule");
                }
                if (directive == "grapheme-rule") {
                    graphemeRuleLines.emplace_back(std::move(body), lineNumber);
                } else {
                    phonemeRuleLines.emplace_back(std::move(body), lineNumber);
                }
            } else {
                languageFileError(lineNumber,
                                  "unknown directive: " + directive);
            }
        }

        if (end == text.size()) {
            break;
        }
        pos = end + 1;
    }

    if (definition.name.empty()) {
        throw std::invalid_argument("language definition has no name");
    }

    const auto validateRules = [&](const auto &lines, bool graphemePhase,
                                   const char *phase) {
        for (const auto &[body, line] : lines) {
            try {
                (void)compileRule(body, graphemePhase, definition.classes);
            } catch (const std::invalid_argument &error) {
                languageFileError(line, std::string(phase) +
                                            " rule: " + error.what());
            }
        }
    };
    validateRules(graphemeRuleLines, true, "grapheme");
    validateRules(phonemeRuleLines, false, "phoneme");
    for (auto &entry : graphemeRuleLines) {
        definition.graphemeRules.push_back(std::move(entry.first));
    }
    for (auto &entry : phonemeRuleLines) {
        definition.phonemeRules.push_back(std::move(entry.first));
    }
    return definition;
}

std::vector<Phoneme> phonemize(std::string_view text,
                               const LanguageDefinition &language,
                               std::vector<std::string> *trace) {
    return conlangPhonemes(text, language, trace);
}

} // namespace formant
