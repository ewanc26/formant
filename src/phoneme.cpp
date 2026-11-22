#include "formant/phoneme.h"

#include <algorithm>
#include <cctype>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>

namespace formant {
namespace {

FormantBand band(double frequency, double amplitude, double bandwidth) {
    return {frequency, amplitude, bandwidth};
}

Phoneme phone(std::string name, PhonemeKind kind, bool voiced, double duration,
              std::array<FormantBand, 4> bands, bool pitchRise = false) {
    return {std::move(name), kind, bands, duration, voiced, pitchRise};
}

Phoneme diphthong(std::string name, double duration,
                  std::array<FormantBand, 4> from,
                  std::array<FormantBand, 4> to) {
    Phoneme result =
        phone(std::move(name), PhonemeKind::Voiced, true, duration, from);
    result.endBands = to;
    result.hasEndBands = true;
    return result;
}

bool startsWith(std::string_view text, size_t pos, std::string_view token) {
    return pos + token.size() <= text.size() &&
           text.substr(pos, token.size()) == token;
}

std::string lower(std::string_view text) {
    std::string result;
    result.reserve(text.size());
    for (unsigned char c : text) {
        result.push_back(static_cast<char>(std::tolower(c)));
    }
    return result;
}

bool isVowel(char c) {
    c = static_cast<char>(std::tolower(c));
    return c == 'a' || c == 'e' || c == 'i' || c == 'o' || c == 'u' || c == 'y';
}

// A deliberately small pronunciation lexicon. Words missing from it are
// handled by the rule-based speller below; the engine is not tied to a
// dictionary file at runtime.
const std::unordered_map<std::string, std::vector<std::string>> &lexicon() {
    static const std::unordered_map<std::string, std::vector<std::string>>
        words = {
            {"a", {"ah"}},
            {"anything", {"ah", "n", "i", "th", "i", "ng"}},
            {"an", {"ah", "n"}},
            {"and", {"ah", "n", "d"}},
            {"are", {"aa", "r"}},
            {"as", {"ah", "z"}},
            {"at", {"ah", "t"}},
            {"bad", {"b", "ah", "d"}},
            {"be", {"b", "ee"}},
            {"begin", {"b", "i", "g", "i", "n"}},
            {"by", {"b", "ay"}},
            {"can", {"k", "ah", "n"}},
            {"do", {"d", "oo"}},
            {"does", {"d", "uh", "z"}},
            {"engine", {"e", "n", "j", "i", "n"}},
            {"for", {"f", "ah", "r"}},
            {"forget", {"f", "er", "g", "e", "t"}},
            {"formant", {"f", "ah", "r", "m", "ah", "n", "t"}},
            {"from", {"f", "r", "ah", "m"}},
            {"get", {"g", "e", "t"}},
            {"gift", {"g", "i", "f", "t"}},
            {"girl", {"g", "er", "l"}},
            {"give", {"g", "i", "v"}},
            {"good", {"g", "uh", "d"}},
            {"has", {"h", "ah", "z"}},
            {"have", {"h", "ah", "v"}},
            {"he", {"h", "ee"}},
            {"hello", {"h", "e", "l", "o"}},
            {"hi", {"h", "ay"}},
            {"i", {"ay"}},
            {"in", {"i", "n"}},
            {"is", {"i", "z"}},
            {"it", {"i", "t"}},
            {"just", {"j", "uh", "s", "t"}},
            {"like", {"l", "ay", "k"}},
            {"no", {"n", "o"}},
            {"not", {"n", "o", "t"}},
            {"of", {"ah", "v"}},
            {"on", {"ah", "n"}},
            {"one", {"w", "uh", "n"}},
            {"or", {"or"}},
            {"our", {"aa", "r"}},
            {"say", {"s", "ay"}},
            {"she", {"sh", "ee"}},
            {"sine", {"s", "ay", "n"}},
            {"sound", {"s", "ow", "n", "d"}},
            {"speech", {"s", "p", "ee", "ch"}},
            {"synthesis", {"s", "i", "n", "th", "e", "s", "i", "s"}},
            {"test", {"t", "e", "s", "t"}},
            {"that", {"dh", "ah", "t"}},
            {"the", {"dh", "ah"}},
            {"their", {"dh", "er"}},
            {"them", {"dh", "ah", "m"}},
            {"then", {"dh", "e", "n"}},
            {"there", {"dh", "er"}},
            {"these", {"dh", "ee", "z"}},
            {"they", {"dh", "ay"}},
            {"this", {"dh", "i", "s"}},
            {"those", {"dh", "o", "z"}},
            {"three", {"th", "r", "ee"}},
            {"to", {"t", "oo"}},
            {"two", {"t", "oo"}},
            {"voice", {"v", "oy", "s"}},
            {"was", {"w", "ah", "z"}},
            {"we", {"w", "ee"}},
            {"were", {"w", "er"}},
            {"what", {"w", "ah", "t"}},
            {"with", {"w", "i", "th"}},
            {"world", {"w", "er", "l", "d"}},
            {"yes", {"y", "e", "s"}},
            {"made", {"m", "ay", "d"}},
            {"machine", {"m", "ah", "sh", "ee", "n"}},
            {"needs", {"n", "ee", "d", "z"}},
            {"small", {"s", "m", "ah", "l"}},
            {"whole", {"h", "o", "l"}},
            {"runs", {"r", "uh", "n", "z"}},
            {"anywhere", {"e", "n", "ee", "w", "er"}},
            {"compile", {"k", "ah", "m", "p", "ay", "l"}},
            {"build", {"b", "i", "l", "d"}},
            {"waves", {"w", "ay", "v", "z"}},
            {"you", {"y", "oo"}},
        };
    return words;
}

std::vector<std::string> spellDigits(std::string_view word) {
    static const std::unordered_map<char, std::vector<std::string>> names = {
        {'0', {"o"}},
        {'1', {"w", "uh", "n"}},
        {'2', {"t", "oo"}},
        {'3', {"th", "r", "ee"}},
        {'4', {"f", "o", "r"}},
        {'5', {"f", "ay", "v"}},
        {'6', {"s", "i", "k", "s"}},
        {'7', {"s", "e", "v", "e", "n"}},
        {'8', {"ay", "t"}},
        {'9', {"n", "ay", "n"}},
    };
    std::vector<std::string> result;
    for (char c : word) {
        auto it = names.find(c);
        if (it != names.end()) {
            result.insert(result.end(), it->second.begin(), it->second.end());
        }
    }
    return result;
}

std::vector<std::string> spellWord(std::string_view word) {
    static const std::unordered_set<std::string> voicedTh = {
        "the",  "this",  "that", "these", "those", "they",
        "them", "their", "then", "there", "with",
    };

    std::string clean;
    for (char c : word) {
        if (std::isalnum(static_cast<unsigned char>(c))) {
            clean.push_back(static_cast<char>(std::tolower(c)));
        }
    }
    if (clean.empty()) {
        return {};
    }

    auto found = lexicon().find(clean);
    if (found != lexicon().end()) {
        return found->second;
    }
    if (std::all_of(clean.begin(), clean.end(), [](char c) {
            return std::isdigit(static_cast<unsigned char>(c));
        })) {
        return spellDigits(clean);
    }

    std::vector<std::string> result;
    const bool softTh = voicedTh.count(clean) != 0;

    for (size_t i = 0; i < clean.size();) {
        if (startsWith(clean, i, "igh")) {
            result.push_back("ay");
            i += 3;
            continue;
        }

        struct Rule {
            const char *text;
            const char *first;
            const char *second = nullptr;
            const char *third = nullptr;
        };
        static const Rule rules[] = {
            // The first letter of kn- and wr- clusters is silent in English
            // (knee, knight / wrong, wrist), including after prefixes as in
            // unknown and rewrite.
            {"kn", "n"},
            {"wr", "r"},
            {"tion", "sh", "ah", "n"},
            {"sion", "zh", "ah", "n"},
            {"ing", "i", "ng"},
            {"th", "th"},
            {"sh", "sh"},
            {"ch", "ch"},
            {"ph", "f"},
            {"ee", "ee"},
            {"oo", "oo"},
            {"ai", "ay"},
            {"ay", "ay"},
            {"ou", "ow"},
            {"oi", "oi"},
            {"ng", "ng"},
            {"qu", "k"},
            {"ea", "ee"},
            {"oa", "o"},
            {"ie", "ee"},
            {"ue", "oo"},
            {"er", "er"},
            {"ar", "aa"},
            {"or", "ah"},
            {"ur", "er"},
        };

        bool matched = false;
        for (const Rule &rule : rules) {
            if (!startsWith(clean, i, rule.text)) {
                continue;
            }
            std::string phoneme = rule.first;
            if (phoneme == "th" && softTh) {
                phoneme = "dh";
            }
            result.push_back(phoneme);
            if (rule.second) {
                result.push_back(rule.second);
            }
            if (rule.third) {
                result.push_back(rule.third);
            }
            if (std::string(rule.text) == "qu") {
                result.push_back("w");
            }
            if (std::string(rule.text) == "ar" ||
                std::string(rule.text) == "or") {
                result.push_back("r");
            }
            i += std::string(rule.text).size();
            matched = true;
            break;
        }
        if (matched) {
            continue;
        }

        // A digit embedded in an alphanumeric token (e.g. "B2" or "Room
        // 101B") must be spelled just like a standalone number. The all-digit
        // fast path above only covers tokens made entirely of digits, and the
        // letter switch below would otherwise drop these silently.
        if (std::isdigit(static_cast<unsigned char>(clean[i]))) {
            const std::vector<std::string> spelled =
                spellDigits(std::string_view(clean).substr(i, 1));
            result.insert(result.end(), spelled.begin(), spelled.end());
            ++i;
            continue;
        }

        const char c = clean[i];
        const char next = i + 1 < clean.size() ? clean[i + 1] : '\0';
        const bool final = i + 1 == clean.size();

        // Silent final 'e' as in "like" or "voice".
        if (c == 'e' && final && clean.size() > 2 && !isVowel(clean[i - 1])) {
            ++i;
            continue;
        }

        switch (c) {
        case 'a':
            result.push_back("a");
            break;
        case 'e':
            result.push_back("e");
            break;
        case 'i':
            result.push_back("i");
            break;
        case 'o':
            result.push_back("o");
            break;
        case 'u':
            result.push_back("uh");
            break;
        case 'y':
            result.push_back(i == 0 ? "y" : (final ? "ee" : "i"));
            break;
        case 'c':
            result.push_back(next == 'e' || next == 'i' || next == 'y' ? "s"
                                                                       : "k");
            break;
        case 'g':
            result.push_back(next == 'e' || next == 'i' || next == 'y' ? "j"
                                                                       : "g");
            break;
        case 'x':
            result.push_back("k");
            result.push_back("s");
            break;
        case 'q':
            result.push_back("k");
            break;
        case 'b':
        case 'd':
        case 'f':
        case 'h':
        case 'j':
        case 'k':
        case 'l':
        case 'm':
        case 'n':
        case 'p':
        case 'r':
        case 's':
        case 't':
        case 'v':
        case 'w':
        case 'z':
            result.push_back(std::string(1, c));
            break;
        default:
            break;
        }
        ++i;
    }

    return result;
}

} // namespace

const std::vector<Phoneme> &phonemeTable() {
    static const std::vector<Phoneme> table = {
        phone("_", PhonemeKind::Silence, false, 0.12,
              {band(0, 0, 1), band(0, 0, 1), band(0, 0, 1), band(0, 0, 1)}),
        phone(",", PhonemeKind::Silence, false, 0.12,
              {band(0, 0, 1), band(0, 0, 1), band(0, 0, 1), band(0, 0, 1)}),
        phone(".", PhonemeKind::Silence, false, 0.28,
              {band(0, 0, 1), band(0, 0, 1), band(0, 0, 1), band(0, 0, 1)}),
        phone("?", PhonemeKind::Silence, false, 0.30,
              {band(0, 0, 1), band(0, 0, 1), band(0, 0, 1), band(0, 0, 1)},
              true),

        phone("a", PhonemeKind::Voiced, true, 0.14,
              {band(660, 1.0, 110), band(1720, 0.55, 120),
               band(2410, 0.25, 180), band(3500, 0.08, 250)}),
        phone("aa", PhonemeKind::Voiced, true, 0.17,
              {band(730, 1.0, 110), band(1090, 0.60, 120),
               band(2440, 0.22, 180), band(3500, 0.08, 250)}),
        phone("ah", PhonemeKind::Voiced, true, 0.13,
              {band(520, 1.0, 110), band(1190, 0.55, 120),
               band(2390, 0.25, 180), band(3500, 0.08, 250)}),
        phone("e", PhonemeKind::Voiced, true, 0.13,
              {band(530, 1.0, 100), band(1840, 0.60, 120),
               band(2480, 0.30, 180), band(3500, 0.08, 250)}),
        phone("ee", PhonemeKind::Voiced, true, 0.15,
              {band(270, 1.0, 90), band(2290, 0.70, 110), band(3010, 0.35, 170),
               band(3600, 0.08, 250)}),
        phone("i", PhonemeKind::Voiced, true, 0.11,
              {band(390, 1.0, 90), band(1990, 0.60, 110), band(2550, 0.30, 170),
               band(3500, 0.08, 250)}),
        phone("o", PhonemeKind::Voiced, true, 0.16,
              {band(570, 1.0, 100), band(840, 0.65, 120), band(2410, 0.25, 180),
               band(3500, 0.08, 250)}),
        phone("oo", PhonemeKind::Voiced, true, 0.16,
              {band(300, 1.0, 90), band(870, 0.60, 110), band(2240, 0.25, 170),
               band(3500, 0.08, 250)}),
        phone("uh", PhonemeKind::Voiced, true, 0.12,
              {band(440, 1.0, 100), band(1020, 0.60, 120),
               band(2240, 0.25, 170), band(3500, 0.08, 250)}),
        phone("er", PhonemeKind::Voiced, true, 0.15,
              {band(490, 1.0, 100), band(1350, 0.55, 130),
               band(1690, 0.40, 160), band(3500, 0.08, 250)}),
        phone("or", PhonemeKind::Voiced, true, 0.15,
              {band(500, 1.0, 110), band(800, 0.60, 120), band(2500, 0.25, 180),
               band(3500, 0.08, 250)}),
        diphthong("ay", 0.17,
                  {band(530, 1.0, 100), band(1840, 0.60, 120),
                   band(2480, 0.30, 180), band(3500, 0.08, 250)},
                  {band(270, 1.0, 90), band(2290, 0.70, 110),
                   band(3010, 0.35, 170), band(3600, 0.08, 250)}),
        diphthong("oy", 0.17,
                  {band(500, 1.0, 110), band(800, 0.60, 120),
                   band(2500, 0.25, 180), band(3500, 0.08, 250)},
                  {band(390, 1.0, 90), band(1990, 0.60, 110),
                   band(2550, 0.30, 170), band(3500, 0.08, 250)}),
        diphthong("ow", 0.18,
                  {band(660, 1.0, 110), band(1720, 0.55, 120),
                   band(2410, 0.25, 180), band(3500, 0.08, 250)},
                  {band(440, 1.0, 100), band(1020, 0.60, 120),
                   band(2240, 0.25, 170), band(3500, 0.08, 250)}),
        diphthong("oi", 0.17,
                  {band(500, 1.0, 110), band(800, 0.60, 120),
                   band(2500, 0.25, 180), band(3500, 0.08, 250)},
                  {band(390, 1.0, 90), band(1990, 0.60, 110),
                   band(2550, 0.30, 170), band(3500, 0.08, 250)}),
        diphthong("eye", 0.17,
                  {band(730, 1.0, 110), band(1090, 0.60, 120),
                   band(2440, 0.22, 180), band(3500, 0.08, 250)},
                  {band(390, 1.0, 90), band(1990, 0.60, 110),
                   band(2550, 0.30, 170), band(3500, 0.08, 250)}),

        phone("h", PhonemeKind::Fricative, false, 0.06,
              {band(500, 0.5, 200), band(1500, 0.4, 300), band(2500, 0.2, 400),
               band(3500, 0.1, 500)}),
        phone("m", PhonemeKind::Nasal, true, 0.08,
              {band(250, 1.0, 100), band(1100, 0.25, 200), band(2200, 0.1, 300),
               band(3000, 0.05, 400)}),
        phone("n", PhonemeKind::Nasal, true, 0.07,
              {band(250, 1.0, 100), band(1500, 0.30, 200), band(2500, 0.1, 300),
               band(3000, 0.05, 400)}),
        phone("ng", PhonemeKind::Nasal, true, 0.09,
              {band(250, 1.0, 100), band(2000, 0.30, 200), band(3000, 0.1, 300),
               band(3600, 0.05, 400)}),
        phone("f", PhonemeKind::Fricative, false, 0.08,
              {band(400, 0.2, 300), band(1800, 0.5, 500), band(3000, 0.8, 600),
               band(4500, 0.5, 700)}),
        phone("v", PhonemeKind::Fricative, true, 0.07,
              {band(400, 0.2, 300), band(1800, 0.5, 500), band(3000, 0.8, 600),
               band(4500, 0.5, 700)}),
        phone("s", PhonemeKind::Fricative, false, 0.09,
              {band(400, 0.1, 300), band(1800, 0.2, 400), band(4500, 1.0, 500),
               band(6500, 0.7, 600)}),
        phone("z", PhonemeKind::Fricative, true, 0.08,
              {band(400, 0.1, 300), band(1800, 0.2, 400), band(4500, 1.0, 500),
               band(6500, 0.7, 600)}),
        phone("sh", PhonemeKind::Fricative, false, 0.10,
              {band(400, 0.1, 300), band(1800, 0.6, 400), band(3000, 1.0, 500),
               band(4500, 0.6, 600)}),
        phone("zh", PhonemeKind::Fricative, true, 0.09,
              {band(400, 0.1, 300), band(1800, 0.6, 400), band(3000, 1.0, 500),
               band(4500, 0.6, 600)}),
        phone("th", PhonemeKind::Fricative, false, 0.08,
              {band(400, 0.2, 300), band(1400, 0.6, 500), band(2800, 0.8, 600),
               band(4000, 0.4, 700)}),
        phone("dh", PhonemeKind::Fricative, true, 0.07,
              {band(400, 0.2, 300), band(1400, 0.6, 500), band(2800, 0.8, 600),
               band(4000, 0.4, 700)}),
        phone("ch", PhonemeKind::Plosive, false, 0.07,
              {band(500, 0.2, 300), band(2000, 0.7, 400), band(3500, 1.0, 500),
               band(5000, 0.5, 600)}),
        phone("j", PhonemeKind::Plosive, true, 0.06,
              {band(500, 0.2, 300), band(2000, 0.7, 400), band(3500, 1.0, 500),
               band(5000, 0.5, 600)}),
        phone("t", PhonemeKind::Plosive, false, 0.035,
              {band(500, 0.3, 300), band(1800, 0.5, 400), band(3000, 0.7, 500),
               band(4500, 0.5, 600)}),
        phone("d", PhonemeKind::Plosive, true, 0.035,
              {band(500, 0.3, 300), band(1800, 0.5, 400), band(3000, 0.7, 500),
               band(4500, 0.5, 600)}),
        phone("p", PhonemeKind::Plosive, false, 0.030,
              {band(300, 0.4, 300), band(900, 0.6, 400), band(2200, 0.5, 500),
               band(3500, 0.3, 600)}),
        phone("b", PhonemeKind::Plosive, true, 0.030,
              {band(300, 0.4, 300), band(900, 0.6, 400), band(2200, 0.5, 500),
               band(3500, 0.3, 600)}),
        phone("k", PhonemeKind::Plosive, false, 0.040,
              {band(350, 0.4, 300), band(1400, 0.7, 400), band(2600, 0.8, 500),
               band(4000, 0.5, 600)}),
        phone("g", PhonemeKind::Plosive, true, 0.040,
              {band(350, 0.4, 300), band(1400, 0.7, 400), band(2600, 0.8, 500),
               band(4000, 0.5, 600)}),
        phone("l", PhonemeKind::Approximant, true, 0.07,
              {band(300, 1.0, 100), band(1200, 0.6, 150), band(2600, 0.3, 200),
               band(3500, 0.1, 300)}),
        phone("r", PhonemeKind::Approximant, true, 0.08,
              {band(300, 1.0, 100), band(1100, 0.6, 150), band(1700, 0.5, 200),
               band(3500, 0.1, 300)}),
        phone("w", PhonemeKind::Approximant, true, 0.07,
              {band(300, 1.0, 100), band(800, 0.6, 150), band(2200, 0.3, 200),
               band(3500, 0.1, 300)}),
        phone("y", PhonemeKind::Approximant, true, 0.07,
              {band(270, 1.0, 100), band(2200, 0.7, 150), band(2900, 0.4, 200),
               band(3500, 0.1, 300)}),
    };
    return table;
}

const Phoneme *findPhoneme(std::string_view name) {
    for (const Phoneme &phoneme : phonemeTable()) {
        if (phoneme.name == name) {
            return &phoneme;
        }
    }
    return nullptr;
}

std::vector<Phoneme> phonemize(std::string_view text) {
    std::vector<Phoneme> result;
    std::string word;

    auto flush = [&]() {
        if (word.empty()) {
            return;
        }
        for (const std::string &name : spellWord(word)) {
            if (const Phoneme *phoneme = findPhoneme(name)) {
                result.push_back(*phoneme);
            }
        }
        Phoneme gap = *findPhoneme("_");
        gap.name = "|";
        gap.duration = 0.035;
        result.push_back(gap);
        word.clear();
    };

    for (char c : text) {
        const unsigned char u = static_cast<unsigned char>(c);
        if (std::isalpha(u) || std::isdigit(u)) {
            word.push_back(static_cast<char>(std::tolower(u)));
            continue;
        }
        flush();
        if (c == ',') {
            result.push_back(*findPhoneme(","));
        } else if (c == '?' || c == '!') {
            result.push_back(*findPhoneme("?"));
        } else if (c == '.' || c == ';' || c == ':') {
            result.push_back(*findPhoneme("."));
        }
    }
    flush();

    // Drop the word gap after the last word, but keep final punctuation:
    // a question mark carries the rising intonation cue for the renderer.
    while (!result.empty() && result.back().name == "|") {
        result.pop_back();
    }
    return result;
}

std::vector<Phoneme> parsePhonemes(std::string_view spec) {
    const size_t begin = spec.find_first_not_of(" \t\r\n");
    const size_t end = spec.find_last_not_of(" \t\r\n");
    if (begin == std::string_view::npos || spec[begin] != '[' ||
        spec[end] != ']') {
        throw std::invalid_argument("phoneme sequence must be enclosed in [ ]");
    }

    std::vector<Phoneme> result;
    std::string_view body = spec.substr(begin + 1, end - begin - 1);
    size_t pos = 0;
    while (pos < body.size()) {
        while (pos < body.size() &&
               std::isspace(static_cast<unsigned char>(body[pos]))) {
            ++pos;
        }
        if (pos == body.size()) {
            break;
        }
        size_t stop = pos;
        while (stop < body.size() &&
               !std::isspace(static_cast<unsigned char>(body[stop]))) {
            ++stop;
        }
        std::string token(body.substr(pos, stop - pos));
        // A comma occupying its own token is the documented pause phoneme,
        // not a separator. Only strip commas attached to a phoneme name.
        if (token != ",") {
            while (!token.empty() && token.back() == ',') {
                token.pop_back();
            }
        }
        if (token.empty()) {
            pos = stop;
            continue;
        }

        const Phoneme *phoneme = findPhoneme(token);
        if (!phoneme) {
            const std::string lowered = lower(token);
            phoneme = findPhoneme(lowered);
        }
        if (!phoneme) {
            throw std::invalid_argument("unknown phoneme: " + token);
        }
        result.push_back(*phoneme);
        pos = stop;
    }
    return result;
}

std::vector<std::string> phonemeNames() {
    std::vector<std::string> names;
    for (const Phoneme &phoneme : phonemeTable()) {
        names.push_back(phoneme.name);
    }
    return names;
}

} // namespace formant
