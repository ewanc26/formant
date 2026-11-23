#include "formant/phoneme.h"

#include <algorithm>
#include <cctype>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>
#include <utility>

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

bool endsWith(std::string_view text, std::string_view token) {
    return text.size() >= token.size() &&
           text.substr(text.size() - token.size()) == token;
}

bool isSibilant(std::string_view phoneme) {
    return phoneme == "s" || phoneme == "z" || phoneme == "sh" ||
           phoneme == "zh" || phoneme == "ch" || phoneme == "j";
}

std::string lower(std::string_view text) {
    std::string result;
    result.reserve(text.size());
    for (unsigned char c : text) {
        result.push_back(static_cast<char>(std::tolower(c)));
    }
    return result;
}

// U+2019 is the apostrophe most keyboards and word processors produce. Treat
// it exactly like the ASCII apostrophe so contractions work either way.
std::string normalizeApostrophes(std::string_view text) {
    std::string result;
    result.reserve(text.size());
    for (size_t i = 0; i < text.size();) {
        if (i + 2 < text.size() &&
            static_cast<unsigned char>(text[i]) == 0xE2 &&
            static_cast<unsigned char>(text[i + 1]) == 0x80 &&
            static_cast<unsigned char>(text[i + 2]) == 0x99) {
            result.push_back('\'');
            i += 3;
        } else {
            result.push_back(text[i++]);
        }
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
            // sc is normally /s/ before e/i/y, but sceptic (unlike sceptre)
            // keeps /sk/, so it needs an explicit entry.
            {"sceptic", {"s", "k", "e", "p", "t", "i", "k"}},
            {"she", {"sh", "ee"}},
            {"sine", {"s", "ay", "n"}},
            {"sound", {"s", "ow", "n", "d"}},
            {"speech", {"s", "p", "ee", "ch"}},
            {"synthesis", {"s", "i", "n", "th", "e", "s", "i", "s"}},
            {"test", {"t", "e", "s", "t"}},
            // tungsten ends in -sten but keeps its /t/ (/ˈtʌŋstən/), unlike
            // the silent-t cluster words. Keep its existing pronunciation
            // exactly so the new word-final stle/sten rule does not drop it.
            {"tungsten", {"t", "uh", "ng", "s", "t", "e", "n"}},
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
            // Word-initial wh is normally /w/ (the wine-whine merger), but a
            // closed set keeps /h/ before a rounded vowel. These must precede
            // the generic wh -> /w/ rule in spellWord(); a third-letter check
            // cannot express the exception because whorl, whoa and whopper
            // also begin wh + o yet keep /w/.
            {"who", {"h", "oo"}},
            {"whom", {"h", "oo", "m"}},
            {"whose", {"h", "oo", "z"}},
            {"whoever", {"h", "oo", "e", "v", "er"}},
            {"whomever", {"h", "oo", "m", "e", "v", "er"}},
            {"whosever", {"h", "oo", "z", "e", "v", "er"}},
            {"wholly", {"h", "o", "l", "ee"}},
            {"wholeness", {"h", "o", "l", "n", "e", "s"}},
            {"whore", {"h", "or"}},
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
            // Contractions keep their apostrophe through tokenisation. The
            // irregular n't forms and vowel changes need explicit entries;
            // regular 's/'ll/'re/'ve/'d/'m endings are handled in spellWord.
            {"aren't", {"aa", "r", "n", "t"}},
            {"can't", {"k", "a", "n", "t"}},
            {"could", {"k", "uh", "d"}},
            {"couldn't", {"k", "uh", "d", "uh", "n", "t"}},
            {"didn't", {"d", "i", "d", "uh", "n", "t"}},
            {"doesn't", {"d", "uh", "z", "uh", "n", "t"}},
            {"don't", {"d", "o", "n", "t"}},
            {"hadn't", {"h", "a", "d", "uh", "n", "t"}},
            {"hasn't", {"h", "a", "z", "uh", "n", "t"}},
            {"haven't", {"h", "a", "v", "uh", "n", "t"}},
            {"isn't", {"i", "z", "uh", "n", "t"}},
            {"it'd", {"i", "t", "uh", "d"}},
            {"it'll", {"i", "t", "uh", "l"}},
            {"mustn't", {"m", "uh", "s", "uh", "n", "t"}},
            {"needn't", {"n", "ee", "d", "uh", "n", "t"}},
            {"should", {"sh", "uh", "d"}},
            {"shouldn't", {"sh", "uh", "d", "uh", "n", "t"}},
            {"they're", {"dh", "er"}},
            {"wasn't", {"w", "uh", "z", "uh", "n", "t"}},
            {"weren't", {"w", "er", "n", "t"}},
            {"won't", {"w", "o", "n", "t"}},
            {"would", {"w", "uh", "d"}},
            {"wouldn't", {"w", "uh", "d", "uh", "n", "t"}},
            {"o'clock", {"ah", "k", "l", "o", "k"}},
            // Silent letters that are lexical rather than positional.
            {"almond", {"aa", "m", "ah", "n", "d"}},
            {"answer", {"a", "n", "s", "er"}},
            {"heir", {"e", "r"}},
            {"honest", {"o", "n", "i", "s", "t"}},
            {"honor", {"o", "n", "er"}},
            {"honour", {"o", "n", "er"}},
            {"hour", {"ow", "er"}},
            {"salmon", {"s", "a", "m", "ah", "n"}},
            {"sword", {"s", "or", "d"}},
            // Common -s words whose final sound cannot be inferred by the
            // plural rule, either because they are singular or because their
            // -es is not a silent e.
            {"always", {"or", "l", "w", "ay", "z"}},
            {"goes", {"g", "ow", "z"}},
            {"heroes", {"h", "er", "o", "z"}},
            {"his", {"h", "i", "z"}},
            {"news", {"n", "oo", "z"}},
            {"says", {"s", "e", "z"}},
            {"series", {"s", "ee", "r", "ee", "z"}},
            {"shoes", {"sh", "oo", "z"}},
            {"species", {"s", "p", "ee", "sh", "ee", "z"}},
            {"toes", {"t", "ow", "z"}},
            // Word-final mb has a silent b; these two also have irregular
            // vowels, so the generic speller cannot produce them correctly.
            {"climb", {"k", "l", "ay", "m"}},
            {"comb", {"k", "ow", "m"}},
            {"waves", {"w", "ay", "v", "z"}},
            {"you", {"y", "oo"}},
            // The ough/augh spellings are irregular: the gh is silent in most
            // words but /f/ in a small closed set, and the vowel varies widely.
            {"although", {"or", "l", "dh", "ow"}},
            {"bought", {"b", "or", "t"}},
            {"bough", {"b", "ow"}},
            {"brought", {"b", "r", "or", "t"}},
            {"caught", {"k", "or", "t"}},
            {"cough", {"k", "o", "f"}},
            {"daughter", {"d", "or", "t", "er"}},
            {"dough", {"d", "ow"}},
            {"enough", {"e", "n", "uh", "f"}},
            {"laugh", {"l", "aa", "f"}},
            {"plough", {"p", "l", "ow"}},
            {"rough", {"r", "uh", "f"}},
            {"tough", {"t", "uh", "f"}},
            {"though", {"dh", "ow"}},
            {"thought", {"th", "or", "t"}},
            {"through", {"th", "r", "oo"}},
            {"trough", {"t", "r", "o", "f"}},
            {"taught", {"t", "or", "t"}},
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
    std::string key;
    for (size_t i = 0; i < word.size(); ++i) {
        const unsigned char c = static_cast<unsigned char>(word[i]);
        if (std::isalnum(c)) {
            const char lowerC = static_cast<char>(std::tolower(c));
            clean.push_back(lowerC);
            key.push_back(lowerC);
        } else if (word[i] == '\'' && !key.empty() && i + 1 < word.size() &&
                   std::isalnum(static_cast<unsigned char>(word[i + 1]))) {
            // Keep an apostrophe only when it sits between letters, so a
            // contraction stays one word while quotation marks still separate.
            key.push_back('\'');
        }
    }
    if (clean.empty()) {
        return {};
    }

    auto found = lexicon().find(key);
    if (found != lexicon().end()) {
        return found->second;
    }

    // A regular contraction is its base word plus a suffix. The irregular n't
    // forms are explicit lexicon entries above.
    if (key.find('\'') != std::string::npos) {
        static const std::pair<std::string_view, std::string_view> suffixes[] = {
            {"'s", "s"}, {"'ll", "l"}, {"'re", "r"},
            {"'ve", "v"}, {"'d", "d"},  {"'m", "m"},
        };
        for (const auto &[suffix, ending] : suffixes) {
            if (key.size() <= suffix.size() || !endsWith(key, suffix)) {
                continue;
            }
            std::vector<std::string> base = spellWord(
                std::string_view(key).substr(0, key.size() - suffix.size()));
            if (base.empty()) {
                continue;
            }
            if (suffix == "'s") {
                const Phoneme *last = findPhoneme(base.back());
                if (isSibilant(base.back())) {
                    base.emplace_back("uh");
                    base.emplace_back("z");
                } else {
                    base.emplace_back(last && last->voiced ? "z" : "s");
                }
            } else {
                base.emplace_back(ending);
            }
            return base;
        }
    }

    // Only strip the apostrophe after contraction handling: "we're" must not
    // resolve to the unrelated lexicon word "were".
    found = lexicon().find(clean);
    if (found != lexicon().end()) {
        return found->second;
    }

    // Inflected -s: voice the suffix from the base's final sound instead of
    // always emitting /s/. The base is spelled recursively so silent letters,
    // digraphs and contractions are resolved first. -ss/-us/-is words are
    // nearly always singular and are left to the generic letter rules.
    if (clean.size() > 2 && endsWith(clean, "s")) {
        // Consonant + y becomes -ies with a voiced /z/ (cities, studies).
        if (endsWith(clean, "ies") && clean.size() > 3) {
            std::vector<std::string> base =
                spellWord(clean.substr(0, clean.size() - 3) + "y");
            if (!base.empty()) {
                base.emplace_back("z");
                return base;
            }
        }
        // Silent-e or sibilant -es (lines, cakes, boxes, watches, pages).
        // Spell the base with its e so a silent e is handled normally, then
        // choose /s/, /z/ or a syllabic /ɪz/ from the base's final sound.
        if (endsWith(clean, "es") && clean.size() > 3) {
            std::vector<std::string> base = spellWord(
                std::string_view(clean).substr(0, clean.size() - 1));
            if (!base.empty() && base.back() != "e") {
                if (isSibilant(base.back())) {
                    base.emplace_back("uh");
                    base.emplace_back("z");
                } else {
                    const Phoneme *last = findPhoneme(base.back());
                    base.emplace_back(last && last->voiced ? "z" : "s");
                }
                return base;
            }
        }
        // A final s after a consonant voices by the base's last sound (cats,
        // dogs, books, cars).
        if (!endsWith(clean, "ss") && !endsWith(clean, "us") &&
            !endsWith(clean, "is") && !isVowel(clean[clean.size() - 2])) {
            std::vector<std::string> base = spellWord(
                std::string_view(clean).substr(0, clean.size() - 1));
            if (!base.empty()) {
                const Phoneme *last = findPhoneme(base.back());
                base.emplace_back(last && last->voiced ? "z" : "s");
                return base;
            }
        }
        // A final s after y is a plural or third-person form (days, plays).
        if (clean[clean.size() - 2] == 'y') {
            std::vector<std::string> base = spellWord(
                std::string_view(clean).substr(0, clean.size() - 1));
            if (!base.empty()) {
                base.emplace_back("z");
                return base;
            }
        }
    }
    if (std::all_of(clean.begin(), clean.end(), [](char c) {
            return std::isdigit(static_cast<unsigned char>(c));
        })) {
        return spellDigits(clean);
    }

    std::vector<std::string> result;
    const bool softTh = voicedTh.count(clean) != 0;

    for (size_t i = 0; i < clean.size();) {
        // The gh digraph has three behaviours in English: a hard /g/ at the
        // start of a word (ghost, ghetto), a silent vowel marker in eigh/aigh
        // (weigh, straight), and silent or /f/ elsewhere (though, laugh). The
        // letter-by-letter fallback used to emit a spurious [g h] for all of
        // them. The /f/ words and those with irregular vowels are in the
        // lexicon; the default here is a silent gh.
        if (startsWith(clean, i, "eigh") || startsWith(clean, i, "aigh")) {
            result.push_back("ay");
            i += 4;
            continue;
        }
        if (startsWith(clean, i, "gh")) {
            if (i == 0) {
                result.push_back("g");
            }
            i += 2;
            continue;
        }
        if (startsWith(clean, i, "igh")) {
            result.push_back("ay");
            i += 3;
            continue;
        }

        // A word-initial sch is the Greek /sk/ (school, scheme, scholar).
        // The same letters medially are s + ch (/stʃ/ as in mischief,
        // discharge) and are left to the generic ch rule, so only the initial
        // cluster is collapsed here.
        if (i == 0 && startsWith(clean, i, "sch")) {
            result.push_back("s");
            result.push_back("k");
            i += 3;
            continue;
        }

        // Word-initial wh is a single /w/ in modern English (the wine-whine
        // merger): when, where, which, wheel, whale. The generic letter loop
        // would emit a spurious /w/ + /h/. The closed /h/ family (who, whom,
        // whose, wholly, whore) is handled by lexicon entries above, so it
        // never reaches here. Medial wh in compounds such as cowhide and
        // snowhill is a real /w/ + /h/ cluster and is left to the letter rules.
        if (i == 0 && startsWith(clean, i, "wh")) {
            result.push_back("w");
            i += 2;
            continue;
        }

        // The p in a word-initial pn, ps or pt cluster is silent (pneumonia,
        // psychology, pterodactyl). A medial cluster keeps its /p/ across the
        // syllable boundary (apnea, chapter), so only position zero drops it.
        if (i == 0 &&
            (startsWith(clean, i, "pn") || startsWith(clean, i, "ps") ||
             startsWith(clean, i, "pt"))) {
            i += 1;
            continue;
        }

        // The h in a word-initial rh cluster is silent (rhythm, rhetoric,
        // rhyme). Medial r + h spans a syllable boundary (overheat), so only
        // the initial cluster collapses.
        if (i == 0 && startsWith(clean, i, "rh")) {
            result.push_back("r");
            i += 2;
            continue;
        }

        // The l in word-final alk/olk is silent (walk, talk, chalk, folk,
        // yolk). Common inflections keep the silent l: walks, walked, walking,
        // talker. A medial cluster such as alkaline keeps its /l/, so only the
        // listed endings match.
        if (startsWith(clean, i, "alk")) {
            const std::string_view rest(clean.data() + i + 3,
                                        clean.size() - (i + 3));
            if (rest.empty() || rest == "s") {
                result.push_back("o");
                result.push_back("k");
                i += 3;
                continue;
            }
            if (rest == "ed") {
                result.push_back("o");
                result.push_back("k");
                result.push_back("t");
                i += 5;
                continue;
            }
            if (rest == "ing") {
                result.push_back("o");
                result.push_back("k");
                result.push_back("i");
                result.push_back("ng");
                i += 6;
                continue;
            }
            if (rest == "er") {
                result.push_back("o");
                result.push_back("k");
                result.push_back("er");
                i += 5;
                continue;
            }
        }

        // The l in word-final olk is silent and the vowel is long (folk, yolk).
        if (startsWith(clean, i, "olk") &&
            (i + 3 == clean.size() ||
             (i + 4 == clean.size() && clean[i + 3] == 's'))) {
            result.push_back("ow");
            result.push_back("k");
            i += 3;
            continue;
        }

        // The l in word-final alf is silent (half, calf).
        if (startsWith(clean, i, "alf") &&
            (i + 3 == clean.size() ||
             (i + 4 == clean.size() && clean[i + 3] == 's'))) {
            result.push_back("a");
            result.push_back("f");
            i += 3;
            continue;
        }

        // The l in word-final alm is silent and the vowel is broad (calm,
        // palm, psalm), including calm+ly. A medial alm keeps its /l/ as in
        // almanac.
        if (startsWith(clean, i, "alm")) {
            const std::string_view rest(clean.data() + i + 3,
                                        clean.size() - (i + 3));
            if (rest.empty() || rest == "s") {
                result.push_back("aa");
                result.push_back("m");
                i += 3;
                continue;
            }
            if (rest == "ly") {
                result.push_back("aa");
                result.push_back("m");
                result.push_back("l");
                result.push_back("ee");
                i += 5;
                continue;
            }
        }

        // The b in a word-final bt cluster is silent (debt, doubt), including
        // the -btle ending of subtle. A medial bt before a vowel keeps its /b/
        // (obtuse, subtitle), so only these endings collapse.
        if (startsWith(clean, i, "btle") && i + 4 == clean.size()) {
            result.push_back("t");
            result.push_back("uh");
            result.push_back("l");
            i += 4;
            continue;
        }
        if (startsWith(clean, i, "bt") && i + 2 == clean.size()) {
            result.push_back("t");
            i += 2;
            continue;
        }

        // The b in a word-final mb cluster is silent (bomb, thumb, lamb, and
        // their plurals). A medial mb as in number or timber keeps its /b/,
        // so only the final cluster may collapse to /m/.
        if (startsWith(clean, i, "mb") &&
            (i + 2 == clean.size() ||
             (i + 3 == clean.size() && clean[i + 2] == 's'))) {
            result.push_back("m");
            i += 2;
            continue;
        }

        // The n in a word-final mn cluster is silent (hymn, autumn, column,
        // damn, solemn, condemn, and their plurals), while the m is silent in
        // a word-initial mn (mnemonic). A medial mn as in hymnal or damnation
        // keeps both, so only the edges (plus plural -s) collapse.
        if (startsWith(clean, i, "mn") &&
            (i == 0 || i + 2 == clean.size() ||
             (i + 3 == clean.size() && clean[i + 2] == 's'))) {
            // Word-initially the m is silent; word-finally the n is silent.
            result.push_back(i == 0 ? "n" : "m");
            i += 2;
            continue;
        }

        // The t in a word-final stle or sten cluster is silent (whistle,
        // castle, listen, fasten, and their plurals). Emit the /s/, a reduced
        // vowel, and the final /l/ or /n/ so the cluster has a syllable. A
        // medial cluster followed by a vowel (lastly, wristband) keeps its /t/
        // and is left to the generic letter rules. tungsten is a lexicon
        // exception because its /t/ is pronounced.
        if ((startsWith(clean, i, "stle") || startsWith(clean, i, "sten")) &&
            (i + 4 == clean.size() ||
             (i + 5 == clean.size() && clean[i + 4] == 's'))) {
            result.push_back("s");
            result.push_back("uh");
            result.push_back(startsWith(clean, i, "stle") ? "l" : "n");
            i += 4;
            continue;
        }

        // The g in gn is silent at the start of a word (gnome, gnat) and at
        // the end (sign, campaign, foreign), including plural -s (signs).
        // A medial gn is a real /g/ + /n/ cluster across syllables (signal,
        // magnet, signature) and is left to the generic letter rules.
        if (startsWith(clean, i, "gn") &&
            (i == 0 || i + 2 == clean.size() ||
             (i + 3 == clean.size() && clean[i + 2] == 's'))) {
            result.push_back("n");
            i += 2;
            continue;
        }

        // sc before e, i or y is a single /s/ (scent, scene, science); the c
        // is silent. Consume only the two letters so the following vowel is
        // still handled normally. A third letter other than e/i/y is left to
        // the generic rules, so sch-, scr-, sco- etc. stay /sk/ (school,
        // screen, score). sceptic is handled by a lexicon entry above.
        if (startsWith(clean, i, "sc") && i + 2 < clean.size() &&
            (clean[i + 2] == 'e' || clean[i + 2] == 'i' ||
             clean[i + 2] == 'y')) {
            result.push_back("s");
            i += 2;
            continue;
        }

        // dg before e, i or y is the single affricate /dʒ/ (bridge, budget,
        // budgie); the d is silent. Consume only the two letters so a
        // following vowel is still handled by the normal rules: budget keeps
        // its pronounced e, budgie keeps ie -> /iː/, and a final silent e
        // still drops. dg before any other letter stays /d/ + /g/ (handgun).
        if (startsWith(clean, i, "dge") || startsWith(clean, i, "dgi") ||
            startsWith(clean, i, "dgy")) {
            result.push_back("j");
            i += 2;
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
            // -ssion is always /ʃən/ (mission, session, discussion). It must
            // precede -sion so both s letters collapse into one /ʃ/ instead
            // of leaving a stray /s/ before the voiced /ʒ/ of -sion.
            {"ssion", "sh", "ah", "n"},
            {"sion", "zh", "ah", "n"},
            {"ing", "i", "ng"},
            {"th", "th"},
            {"sh", "sh"},
            // tch is a trigraph for a single /tʃ/ (catch, kitchen); the t is
            // silent. It must precede ch so the longer sequence matches first.
            // Plain ch words (rich, which) do not contain tch and are
            // unaffected.
            {"tch", "ch"},
            {"ch", "ch"},
            // ck is a single /k/ (back, quick); without this rule the generic
            // c and k letter rules emit two separate stops.
            {"ck", "k"},
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
        // An exclamation keeps the same punctuation pause but has no final
        // pitch rise: that contour belongs to questions only.
        phone("!", PhonemeKind::Silence, false, 0.30,
              {band(0, 0, 1), band(0, 0, 1), band(0, 0, 1), band(0, 0, 1)}),

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

    const std::string normalized = normalizeApostrophes(text);
    for (size_t i = 0; i < normalized.size(); ++i) {
        const char c = normalized[i];
        const unsigned char u = static_cast<unsigned char>(c);
        if (std::isalpha(u) || std::isdigit(u)) {
            word.push_back(static_cast<char>(std::tolower(u)));
            continue;
        }
        // An apostrophe between letters belongs to the word (don't, it's);
        // otherwise it is a quotation mark and only separates words.
        if (c == '\'' && !word.empty() && i + 1 < normalized.size() &&
            std::isalnum(static_cast<unsigned char>(normalized[i + 1]))) {
            word.push_back('\'');
            continue;
        }
        flush();
        if (c == ',') {
            result.push_back(*findPhoneme(","));
        } else if (c == '?') {
            result.push_back(*findPhoneme("?"));
        } else if (c == '!') {
            result.push_back(*findPhoneme("!"));
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
