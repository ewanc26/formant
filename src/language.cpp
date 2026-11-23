#include "language_internal.h"

#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "formant/ipa.h"

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

} // namespace formant
