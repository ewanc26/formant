#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "formant/formant.h"

namespace {

int failures = 0;

#define CHECK(condition)                                                       \
    do {                                                                       \
        if (!(condition)) {                                                    \
            std::cerr << "FAIL " << __FILE__ << ":" << __LINE__                \
                      << ": " #condition << "\n";                              \
            ++failures;                                                        \
        }                                                                      \
    } while (false)

std::vector<std::string> names(const std::vector<formant::Phoneme> &phonemes) {
    std::vector<std::string> result;
    for (const formant::Phoneme &phoneme : phonemes) {
        result.push_back(phoneme.name);
    }
    return result;
}

// The first word of a phrase must match the given IPA, before the word gap
// and the remaining front-end fallback words.
bool startsWithIpa(const std::vector<formant::Phoneme> &actual,
                   std::string_view ipa) {
    const std::vector<std::string> expected = names(formant::parseIpa(ipa));
    const std::vector<std::string> got = names(actual);
    return got.size() >= expected.size() &&
           std::equal(expected.begin(), expected.end(), got.begin());
}

bool contains(const std::vector<std::string> &items,
              const std::string &wanted) {
    for (const std::string &item : items) {
        if (item == wanted) {
            return true;
        }
    }
    return false;
}

double peak(const std::vector<float> &samples) {
    double result = 0.0;
    for (float sample : samples) {
        result = std::max(result, std::abs(static_cast<double>(sample)));
    }
    return result;
}

uint16_t littleEndian16(const std::vector<uint8_t> &bytes, size_t offset) {
    return static_cast<uint16_t>(bytes[offset]) |
           static_cast<uint16_t>(bytes[offset + 1] << 8);
}

uint32_t littleEndian32(const std::vector<uint8_t> &bytes, size_t offset) {
    return static_cast<uint32_t>(bytes[offset]) |
           (static_cast<uint32_t>(bytes[offset + 1]) << 8) |
           (static_cast<uint32_t>(bytes[offset + 2]) << 16) |
           (static_cast<uint32_t>(bytes[offset + 3]) << 24);
}

std::vector<uint8_t> bytesOf(std::ostringstream &stream) {
    const std::string text = stream.str();
    return {text.begin(), text.end()};
}

} // namespace

int main() {
    using namespace formant;

    // Voice presets are data, and every built-in name resolves.
    const std::vector<std::string> presets = voicePresetNames();
    CHECK(presets.size() >= 6);
    for (const std::string &name : presets) {
        CHECK(voicePreset(name).name == name);
    }
    bool rejected = false;
    try {
        (void)voicePreset("nonexistent");
    } catch (const std::invalid_argument &) {
        rejected = true;
    }
    CHECK(rejected);

    // Text uses the lexicon, and phoneme strings use the bracket syntax.
    const std::vector<std::string> hello = names(phonemize("Hello, world."));
    CHECK(contains(hello, "h"));
    CHECK(contains(hello, "e"));
    CHECK(contains(hello, "l"));
    CHECK(contains(hello, "o"));
    CHECK(contains(hello, "w"));
    CHECK(contains(hello, "er"));
    CHECK(contains(hello, "d"));
    CHECK(contains(names(phonemize("Formant is a voice engine?")), "?"));

    // A question mark carries the final pitch rise, but an exclamation mark
    // does not. The two used to share the '?' phoneme, so hello! rendered
    // byte-identically to hello? including the rising contour.
    const auto question = phonemize("hello?");
    const auto exclamation = phonemize("hello!");
    CHECK(question.back().name == "?");
    CHECK(question.back().pitchRise);
    CHECK(exclamation.back().name == "!");
    CHECK(!exclamation.back().pitchRise);
    CHECK(exclamation.back().duration == question.back().duration);
    CHECK(question.back().kind == PhonemeKind::Silence);
    CHECK(exclamation.back().kind == PhonemeKind::Silence);

    // A final rise belongs to the end of the utterance. A question mark in the
    // middle of a phrase used to enable the global rise for whatever followed,
    // so "Hello? I am here." ended on a rising statement.
    {
        Voice flat = voicePreset("robot");
        flat.vibratoDepth = 0.0;
        flat.breathiness = 0.0;
        const auto mixed = phonemize("Hello? I am here.");
        CHECK(mixed.back().name == ".");
        CHECK(contains(names(mixed), "?"));
        auto withoutCue = mixed;
        for (Phoneme &phoneme : withoutCue) {
            phoneme.pitchRise = false;
        }
        CHECK(Synthesizer(flat).render(mixed, 22050) ==
              Synthesizer(flat).render(withoutCue, 22050));

        const auto question = phonemize("Hello?");
        auto questionWithoutCue = question;
        for (Phoneme &phoneme : questionWithoutCue) {
            phoneme.pitchRise = false;
        }
        CHECK(Synthesizer(flat).render(question, 22050) !=
              Synthesizer(flat).render(questionWithoutCue, 22050));
    }

    // An apostrophe inside a word belongs to that word. It used to split the
    // token, so "don't" became [d o n | t] with a spurious word gap.
    {
        CHECK(names(phonemize("don't")) ==
              std::vector<std::string>({"d", "o", "n", "t"}));
        CHECK(names(phonemize("it's")) ==
              std::vector<std::string>({"i", "t", "s"}));
        CHECK(names(phonemize("I'm")) == std::vector<std::string>({"ay", "m"}));
        CHECK(names(phonemize("we're")) ==
              std::vector<std::string>({"w", "ee", "r"}));
        CHECK(names(phonemize("can't")) ==
              std::vector<std::string>({"k", "a", "n", "t"}));
        CHECK(names(phonemize("couldn't")) ==
              std::vector<std::string>({"k", "uh", "d", "uh", "n", "t"}));
        CHECK(names(phonemize("they're")) ==
              std::vector<std::string>({"dh", "er"}));
        CHECK(names(phonemize("it'll")) ==
              std::vector<std::string>({"i", "t", "uh", "l"}));
        // U+2019 is the apostrophe most keyboards produce.
        CHECK(names(phonemize("don\u2019t")) == names(phonemize("don't")));
        // A possessive suffixes /s/ or /z/ by final voicing; a sibilant takes
        // a syllabic /ɪz/.
        CHECK(names(phonemize("cat's")).back() == "s");
        CHECK(names(phonemize("dog's")).back() == "z");
        const std::vector<std::string> bosses = names(phonemize("boss's"));
        CHECK(bosses.size() >= 2);
        CHECK(bosses[bosses.size() - 2] == "uh");
        CHECK(bosses.back() == "z");
        // A trailing apostrophe (plural possessive) still separates without
        // adding a phoneme, so it matches the plural base word.
        CHECK(names(phonemize("dogs'")) == names(phonemize("dogs")));
    }

    // Plural and third-person -s voice from the base's final sound. The base
    // used to keep a voiceless /s/ after every voiced sound, and -es was
    // spelled as a separate vowel plus /s/.
    CHECK(names(phonemize("cats")) ==
          std::vector<std::string>({"k", "a", "t", "s"}));
    CHECK(names(phonemize("dogs")) ==
          std::vector<std::string>({"d", "o", "g", "z"}));
    CHECK(names(phonemize("books")) ==
          std::vector<std::string>({"b", "oo", "k", "s"}));
    CHECK(names(phonemize("cars")) ==
          std::vector<std::string>({"k", "aa", "r", "z"}));
    CHECK(names(phonemize("boxes")) ==
          std::vector<std::string>({"b", "o", "k", "s", "uh", "z"}));
    CHECK(names(phonemize("watches")) ==
          std::vector<std::string>({"w", "a", "ch", "uh", "z"}));
    CHECK(names(phonemize("wishes")) ==
          std::vector<std::string>({"w", "i", "sh", "uh", "z"}));
    CHECK(names(phonemize("cities")) ==
          std::vector<std::string>({"s", "i", "t", "ee", "z"}));
    CHECK(names(phonemize("lines")) ==
          std::vector<std::string>({"l", "i", "n", "z"}));
    CHECK(names(phonemize("cakes")) ==
          std::vector<std::string>({"k", "a", "k", "s"}));
    CHECK(names(phonemize("says")) ==
          std::vector<std::string>({"s", "e", "z"}));
    CHECK(names(phonemize("goes")) ==
          std::vector<std::string>({"g", "ow", "z"}));
    // Singular -s words keep a voiceless final /s/.
    CHECK(names(phonemize("bus")) ==
          std::vector<std::string>({"b", "uh", "s"}));
    CHECK(names(phonemize("status")) ==
          std::vector<std::string>({"s", "t", "a", "t", "uh", "s"}));
    CHECK(names(phonemize("class")) ==
          std::vector<std::string>({"k", "l", "a", "s", "s"}));
    CHECK(names(phonemize("news")).back() == "z");
    CHECK(names(phonemize("series")).back() == "z");

    // Suffix rules apply to words outside the small lexicon.
    const std::vector<std::string> nation = names(phonemize("nation"));
    CHECK(contains(nation, "sh"));
    CHECK(contains(nation, "ah"));
    CHECK(contains(nation, "n"));
    const std::vector<std::string> singing = names(phonemize("singing"));
    CHECK(contains(singing, "ng"));
    CHECK(singing.size() >= 3);

    // Hard /g/ before e/i/y must not become the affricate j. English gives no
    // reliable orthographic signal, so these exceptions live in the lexicon;
    // the generic soft-g rule remains correct for gem/gym/page/engine.
    const std::vector<std::string> get = names(phonemize("get"));
    CHECK(!get.empty());
    CHECK(get.front() == "g");
    CHECK(get != names(phonemize("jet")));
    CHECK(names(phonemize("give")).front() == "g");
    CHECK(names(phonemize("girl")).front() == "g");
    CHECK(contains(names(phonemize("gift")), "g"));
    CHECK(contains(names(phonemize("begin")), "g"));
    CHECK(contains(names(phonemize("forget")), "g"));
    CHECK(contains(names(phonemize("gem")), "j"));
    CHECK(contains(names(phonemize("gym")), "j"));
    CHECK(contains(names(phonemize("page")), "j"));
    CHECK(contains(names(phonemize("engine")), "j"));

    // The k in a kn- cluster is silent. The generic letter rules used to emit
    // a spurious /k/, making "knee" render byte-identically to [k n ee].
    CHECK(names(phonemize("knee")) == std::vector<std::string>({"n", "ee"}));
    CHECK(names(phonemize("knight")) ==
          std::vector<std::string>({"n", "ay", "t"}));
    CHECK(names(phonemize("know")).front() == "n");
    CHECK(names(phonemize("knock")).front() == "n");
    CHECK(names(phonemize("knew")).front() == "n");
    CHECK(names(phonemize("knowledge")).front() == "n");
    CHECK(names(phonemize("unknown")).front() == "uh");
    CHECK(contains(names(phonemize("unknown")), "n"));
    CHECK(names(phonemize("kite")).front() == "k");
    CHECK(names(phonemize("skin")).front() == "s");

    // The w in a wr- cluster is silent. The generic letter rules used to emit
    // a spurious /w/, making "wrong" render byte-identically to [w r o ng].
    CHECK(names(phonemize("wrong")) ==
          std::vector<std::string>({"r", "o", "ng"}));
    CHECK(names(phonemize("wrist")).front() == "r");
    CHECK(names(phonemize("wreck")).front() == "r");
    CHECK(names(phonemize("wring")).front() == "r");
    CHECK(names(phonemize("wrap")).front() == "r");
    CHECK(contains(names(phonemize("rewrite")), "r"));
    CHECK(names(phonemize("row")).front() == "r");
    CHECK(names(phonemize("warm")).front() == "w");
    CHECK(names(phonemize("water")).front() == "w");

    // The b in a word-final mb cluster is silent. The generic letter rules
    // used to emit it, making "bomb" render byte-identically to [b o m b].
    CHECK(names(phonemize("climb")) ==
          std::vector<std::string>({"k", "l", "ay", "m"}));
    CHECK(names(phonemize("comb")) ==
          std::vector<std::string>({"k", "ow", "m"}));
    CHECK(names(phonemize("bomb")) ==
          std::vector<std::string>({"b", "o", "m"}));
    CHECK(names(phonemize("thumb")) ==
          std::vector<std::string>({"th", "uh", "m"}));
    CHECK(names(phonemize("lamb")).back() == "m");
    CHECK(names(phonemize("bombs")) ==
          std::vector<std::string>({"b", "o", "m", "z"}));
    // A medial mb cluster keeps its /b/ (number, timber, combat).
    CHECK(contains(names(phonemize("number")), "b"));
    CHECK(contains(names(phonemize("timber")), "b"));
    CHECK(contains(names(phonemize("combat")), "b"));

    // The n in a word-final mn cluster is silent (hymn, autumn, column, damn,
    // solemn, condemn, limn, and their plurals). The generic letter rules
    // used to emit it, making "hymn" render byte-identically to [h i m n].
    CHECK(names(phonemize("hymn")) ==
          std::vector<std::string>({"h", "i", "m"}));
    CHECK(names(phonemize("autumn")) ==
          std::vector<std::string>({"a", "uh", "t", "uh", "m"}));
    CHECK(names(phonemize("column")) ==
          std::vector<std::string>({"k", "o", "l", "uh", "m"}));
    CHECK(names(phonemize("damn")) ==
          std::vector<std::string>({"d", "a", "m"}));
    CHECK(names(phonemize("solemn")) ==
          std::vector<std::string>({"s", "o", "l", "e", "m"}));
    CHECK(names(phonemize("condemn")) ==
          std::vector<std::string>({"k", "o", "n", "d", "e", "m"}));
    CHECK(names(phonemize("hymns")) ==
          std::vector<std::string>({"h", "i", "m", "z"}));
    CHECK(names(phonemize("autumns")).back() == "z");
    CHECK(names(phonemize("columns")).back() == "z");
    // A medial mn cluster keeps both /m/ and /n/ when a vowel follows (hymnal,
    // autumnal, damnation, hymnody). Word-initially the m is silent instead
    // (mnemonic), while the n at the end of hymn is silent.
    CHECK(names(phonemize("hymnal")) ==
          std::vector<std::string>({"h", "i", "m", "n", "a", "l"}));
    CHECK(contains(names(phonemize("autumnal")), "n"));
    CHECK(contains(names(phonemize("damnation")), "n"));
    CHECK(contains(names(phonemize("hymnody")), "n"));
    CHECK(names(phonemize("mnemonic")).front() == "n");

    // The t in a word-final stle or sten cluster is silent (whistle, castle,
    // listen, fasten, and their plurals). The generic letter rules used to
    // emit it, making "listen" byte-identically [l i s t e n]. Emit /s/, a
    // reduced vowel, and /l/ or /n/. tungsten is a lexicon exception whose t
    // is pronounced; medial/suffixed clusters keep their /t/.
    CHECK(names(phonemize("listen")) ==
          std::vector<std::string>({"l", "i", "s", "uh", "n"}));
    CHECK(names(phonemize("fasten")) ==
          std::vector<std::string>({"f", "a", "s", "uh", "n"}));
    CHECK(names(phonemize("whistle")) ==
          std::vector<std::string>({"w", "i", "s", "uh", "l"}));
    CHECK(names(phonemize("castle")) ==
          std::vector<std::string>({"k", "a", "s", "uh", "l"}));
    CHECK(names(phonemize("hasten")) ==
          std::vector<std::string>({"h", "a", "s", "uh", "n"}));
    CHECK(names(phonemize("moisten")) ==
          std::vector<std::string>({"m", "oi", "s", "uh", "n"}));
    CHECK(names(phonemize("glisten")).back() == "n");
    CHECK(!contains(names(phonemize("glisten")), "t"));
    CHECK(names(phonemize("listens")) ==
          std::vector<std::string>({"l", "i", "s", "uh", "n", "z"}));
    CHECK(names(phonemize("whistles")) ==
          std::vector<std::string>({"w", "i", "s", "uh", "l", "z"}));
    CHECK(names(phonemize("tungsten")) ==
          std::vector<std::string>({"t", "uh", "ng", "s", "t", "e", "n"}));
    // Medial clusters followed by a vowel keep their /t/.
    CHECK(contains(names(phonemize("listened")), "t"));
    CHECK(contains(names(phonemize("whistled")), "t"));
    CHECK(names(phonemize("stencil")).front() == "s");
    CHECK(contains(names(phonemize("stencil")), "t"));

    // The gh digraph used to be spelled as /g/ + /h/. Word-initial gh is /g/;
    // eigh/aigh mark a long vowel; non-initial gh is silent except in the
    // irregular ough/augh words, which the lexicon covers.
    CHECK(names(phonemize("ghost")) ==
          std::vector<std::string>({"g", "o", "s", "t"}));
    CHECK(names(phonemize("ghetto")).front() == "g");
    CHECK(names(phonemize("ghoul")).front() == "g");
    CHECK(!contains(names(phonemize("ghost")), "h"));
    CHECK(names(phonemize("weigh")) == std::vector<std::string>({"w", "ay"}));
    CHECK(names(phonemize("straight")).back() == "t");
    CHECK(contains(names(phonemize("straight")), "ay"));
    CHECK(names(phonemize("high")) == std::vector<std::string>({"h", "ay"}));
    CHECK(names(phonemize("night")) ==
          std::vector<std::string>({"n", "ay", "t"}));
    CHECK(!contains(names(phonemize("though")), "g"));
    CHECK(!contains(names(phonemize("though")), "h"));
    CHECK(names(phonemize("though")) == std::vector<std::string>({"dh", "ow"}));
    CHECK(names(phonemize("through")) ==
          std::vector<std::string>({"th", "r", "oo"}));
    CHECK(names(phonemize("thought")) ==
          std::vector<std::string>({"th", "or", "t"}));
    CHECK(names(phonemize("laugh")) ==
          std::vector<std::string>({"l", "aa", "f"}));
    CHECK(names(phonemize("cough")) ==
          std::vector<std::string>({"k", "o", "f"}));
    CHECK(names(phonemize("rough")) ==
          std::vector<std::string>({"r", "uh", "f"}));
    CHECK(names(phonemize("tough")) ==
          std::vector<std::string>({"t", "uh", "f"}));
    CHECK(names(phonemize("enough")).back() == "f");

    // ck is a digraph for a single /k/. Without a rule it was spelled as two
    // stops, so "back" rendered byte-identically to [b a k k] with an extra
    // burst and 40 ms of spurious audio.
    CHECK(names(phonemize("back")) ==
          std::vector<std::string>({"b", "a", "k"}));
    CHECK(names(phonemize("quick")) ==
          std::vector<std::string>({"k", "w", "i", "k"}));
    CHECK(names(phonemize("black")) ==
          std::vector<std::string>({"b", "l", "a", "k"}));
    CHECK(names(phonemize("clock")).back() == "k");
    CHECK(names(phonemize("clock")).size() == 4);
    CHECK(names(phonemize("pocket")).size() == 5);
    // A standalone k is unaffected.
    CHECK(names(phonemize("book")) ==
          std::vector<std::string>({"b", "oo", "k"}));
    CHECK(names(phonemize("kite")).front() == "k");

    // Word-initial sch is the Greek /sk/ (school, scheme, scholar). The old
    // letter rules spelled it s + ch (/stʃ/), the wrong place of articulation.
    // Medial sch (mischief, discharge) is genuinely s + ch and must stay that
    // way, so the rule applies only at the start of the word.
    CHECK(names(phonemize("school")) ==
          std::vector<std::string>({"s", "k", "oo", "l"}));
    CHECK(names(phonemize("scheme")) ==
          std::vector<std::string>({"s", "k", "e", "m"}));
    CHECK(names(phonemize("scholar")).front() == "s");
    CHECK(names(phonemize("scholar"))[1] == "k");
    CHECK(names(phonemize("schooner"))[1] == "k");
    CHECK(names(phonemize("schedule"))[1] == "k");
    CHECK(names(phonemize("mischief"))[2] == "s");
    CHECK(names(phonemize("mischief"))[3] == "ch");
    CHECK(names(phonemize("discharge"))[2] == "s");
    CHECK(names(phonemize("discharge"))[3] == "ch");

    // tch is a trigraph for a single /tʃ/ (catch, kitchen); the t is silent.
    // Without a rule it was spelled t + ch with a spurious stop release.
    CHECK(names(phonemize("catch")) ==
          std::vector<std::string>({"k", "a", "ch"}));
    CHECK(names(phonemize("kitchen")) ==
          std::vector<std::string>({"k", "i", "ch", "e", "n"}));
    CHECK(names(phonemize("match")).back() == "ch");
    CHECK(names(phonemize("switch")).back() == "ch");
    CHECK(names(phonemize("pitch")).back() == "ch");
    // Plain ch words are unaffected by the new rule.
    CHECK(names(phonemize("rich")) ==
          std::vector<std::string>({"r", "i", "ch"}));
    CHECK(contains(names(phonemize("teacher")), "ch"));
    // sch stays correct too: tch must not disturb the preceding fixes.
    CHECK(!contains(names(phonemize("school")), "t"));
    CHECK(names(phonemize("mischief"))[3] == "ch");

    // dg before e, i or y is the single affricate /dʒ/ (bridge, budget,
    // budgie); the d is silent. Only dg is consumed, so the following vowel
    // survives: budget keeps its pronounced e and budgie keeps ie -> /iː/.
    // dg before any other letter is still /d/ + /g/ (handgun).
    CHECK(names(phonemize("bridge")) ==
          std::vector<std::string>({"b", "r", "i", "j"}));
    CHECK(names(phonemize("edge")) == std::vector<std::string>({"e", "j"}));
    CHECK(names(phonemize("badge")) ==
          std::vector<std::string>({"b", "a", "j"}));
    CHECK(names(phonemize("budget")) ==
          std::vector<std::string>({"b", "uh", "j", "e", "t"}));
    CHECK(names(phonemize("budgie")) ==
          std::vector<std::string>({"b", "uh", "j", "ee"}));
    CHECK(names(phonemize("podgy")) ==
          std::vector<std::string>({"p", "o", "j", "ee"}));
    CHECK(names(phonemize("handgun")) ==
          std::vector<std::string>({"h", "a", "n", "d", "g", "uh", "n"}));
    // Earlier trigraph fixes stay correct.
    CHECK(names(phonemize("catch")) ==
          std::vector<std::string>({"k", "a", "ch"}));
    CHECK(names(phonemize("rich")) ==
          std::vector<std::string>({"r", "i", "ch"}));

    // The g in gn is silent word-initially and word-finally (including
    // plural -s), so gnome renders as [n o m] rather than [g n o m].
    // Medial gn is a real /g/ + /n/ cluster (signal, magnet, signature).
    CHECK(names(phonemize("gnome")) ==
          std::vector<std::string>({"n", "o", "m"}));
    CHECK(names(phonemize("gnat")) ==
          std::vector<std::string>({"n", "a", "t"}));
    CHECK(names(phonemize("gnaw")) ==
          std::vector<std::string>({"n", "a", "w"}));
    CHECK(names(phonemize("sign")) ==
          std::vector<std::string>({"s", "i", "n"}));
    CHECK(names(phonemize("signs")) ==
          std::vector<std::string>({"s", "i", "n", "z"}));
    CHECK(names(phonemize("design")).back() == "n");
    CHECK(names(phonemize("campaign")).back() == "n");
    CHECK(names(phonemize("campaigns")).back() == "z");
    CHECK(names(phonemize("signal")) ==
          std::vector<std::string>({"s", "i", "g", "n", "a", "l"}));
    CHECK(names(phonemize("magnet"))[2] == "g");
    CHECK(names(phonemize("signature"))[2] == "g");
    // Previous silent-letter and trigraph fixes stay correct.
    CHECK(names(phonemize("knee")).front() == "n");
    CHECK(names(phonemize("bomb")).back() == "m");

    // sc before e, i or y is a single /s/ (scent, scene, science); the c is
    // silent, so scent is a homophone of sent and cent. sc before a/o/u or a
    // consonant stays /sk/ (school, screen, score, scrape), and the sc+h
    // initial rule is untouched. sceptic is the documented /sk/ exception.
    CHECK(names(phonemize("scent")) ==
          std::vector<std::string>({"s", "e", "n", "t"}));
    CHECK(names(phonemize("scent")) == names(phonemize("sent")));
    CHECK(names(phonemize("scent")) == names(phonemize("cent")));
    CHECK(names(phonemize("ascent")) ==
          std::vector<std::string>({"a", "s", "e", "n", "t"}));
    CHECK(names(phonemize("descent")) ==
          std::vector<std::string>({"d", "e", "s", "e", "n", "t"}));
    CHECK(names(phonemize("crescent")) ==
          std::vector<std::string>({"k", "r", "e", "s", "e", "n", "t"}));
    CHECK(names(phonemize("scene")) ==
          std::vector<std::string>({"s", "e", "n"}));
    CHECK(names(phonemize("science")) ==
          std::vector<std::string>({"s", "ee", "n", "s"}));
    CHECK(names(phonemize("scissors")).front() == "s");
    CHECK(names(phonemize("scissors"))[1] == "i");
    CHECK(names(phonemize("scenic")).front() == "s");
    CHECK(names(phonemize("scintilla")).front() == "s");
    CHECK(names(phonemize("school")) ==
          std::vector<std::string>({"s", "k", "oo", "l"}));
    CHECK(names(phonemize("scheme"))[1] == "k");
    CHECK(names(phonemize("screen"))[1] == "k");
    CHECK(names(phonemize("score"))[1] == "k");
    CHECK(names(phonemize("scrape"))[1] == "k");
    CHECK(names(phonemize("sceptic")) ==
          std::vector<std::string>({"s", "k", "e", "p", "t", "i", "k"}));

    // Word-initial wh is a single /w/ (the wine-whine merger), not /w/ + /h/.
    // The closed /h/ family is in the lexicon with an /uː/ or /o/ vowel. Medial
    // wh in compounds such as cowhide is a real /w/ + /h/ cluster and stays.
    CHECK(names(phonemize("who")) == std::vector<std::string>({"h", "oo"}));
    CHECK(names(phonemize("whom")) ==
          std::vector<std::string>({"h", "oo", "m"}));
    CHECK(names(phonemize("whose")) ==
          std::vector<std::string>({"h", "oo", "z"}));
    CHECK(names(phonemize("whoever")) ==
          std::vector<std::string>({"h", "oo", "e", "v", "er"}));
    CHECK(names(phonemize("whomever")) ==
          std::vector<std::string>({"h", "oo", "m", "e", "v", "er"}));
    CHECK(names(phonemize("whosever")) ==
          std::vector<std::string>({"h", "oo", "z", "e", "v", "er"}));
    CHECK(names(phonemize("wholly")) ==
          std::vector<std::string>({"h", "o", "l", "ee"}));
    CHECK(names(phonemize("wholeness")) ==
          std::vector<std::string>({"h", "o", "l", "n", "e", "s"}));
    CHECK(names(phonemize("whore")) == std::vector<std::string>({"h", "or"}));
    CHECK(names(phonemize("when")) ==
          std::vector<std::string>({"w", "e", "n"}));
    CHECK(names(phonemize("where")) == std::vector<std::string>({"w", "er"}));
    CHECK(names(phonemize("which")) ==
          std::vector<std::string>({"w", "i", "ch"}));
    CHECK(names(phonemize("wheel")) ==
          std::vector<std::string>({"w", "ee", "l"}));
    CHECK(names(phonemize("wheat")) ==
          std::vector<std::string>({"w", "ee", "t"}));
    CHECK(names(phonemize("whisper")) ==
          std::vector<std::string>({"w", "i", "s", "p", "er"}));
    CHECK(names(phonemize("whale")) ==
          std::vector<std::string>({"w", "a", "l"}));
    // wh + o does not imply /h/: these keep /w/.
    CHECK(names(phonemize("whorl")).front() == "w");
    CHECK(names(phonemize("whoa")) == std::vector<std::string>({"w", "o"}));
    CHECK(names(phonemize("whopper")).front() == "w");
    CHECK(names(phonemize("whop")).front() == "w");
    // Medial wh in compounds is a genuine /w/ + /h/ cluster.
    CHECK(names(phonemize("cowhide")) ==
          std::vector<std::string>({"k", "o", "w", "h", "i", "d"}));
    CHECK(names(phonemize("snowhill")) ==
          std::vector<std::string>({"s", "n", "o", "w", "h", "i", "l", "l"}));

    // Word-initial pn, ps and pt clusters have a silent p (pneumonia,
    // psychology, pterodactyl). A medial cluster keeps its /p/ across the
    // syllable boundary (apnea, chapter), so only position zero may drop it.
    CHECK(names(phonemize("pneumonia")).front() == "n");
    CHECK(!contains(names(phonemize("pneumonia")), "p"));
    CHECK(names(phonemize("pseudo")).front() == "s");
    CHECK(!contains(names(phonemize("pseudo")), "p"));
    CHECK(names(phonemize("pterodactyl")).front() == "t");
    CHECK(!contains(names(phonemize("pterodactyl")), "p"));
    CHECK(contains(names(phonemize("apnea")), "p"));
    CHECK(contains(names(phonemize("chapter")), "p"));

    // A word-initial rh cluster has a silent h (rhythm, rhetoric, rhyme);
    // medial r + h spans a syllable boundary and keeps both (overheat).
    CHECK(names(phonemize("rhythm")).front() == "r");
    CHECK(!contains(names(phonemize("rhythm")), "h"));
    CHECK(names(phonemize("rhetoric")).front() == "r");
    CHECK(!contains(names(phonemize("rhetoric")), "h"));
    CHECK(names(phonemize("rhyme")).front() == "r");
    CHECK(!contains(names(phonemize("rhyme")), "h"));
    CHECK(contains(names(phonemize("overheat")), "h"));

    // A word-final l before k, f or m is silent (walk, talk, half, calm, folk,
    // yolk). The closed could/would/should set also has a silent l and an
    // irregular vowel, so it lives in the lexicon. A medial cluster such as
    // alkaline or almanac keeps its /l/.
    CHECK(names(phonemize("walk")) ==
          std::vector<std::string>({"w", "o", "k"}));
    CHECK(names(phonemize("walks")) ==
          std::vector<std::string>({"w", "o", "k", "s"}));
    CHECK(names(phonemize("talked")) ==
          std::vector<std::string>({"t", "o", "k", "t"}));
    CHECK(names(phonemize("talking")) ==
          std::vector<std::string>({"t", "o", "k", "i", "ng"}));
    CHECK(names(phonemize("chalk")) ==
          std::vector<std::string>({"ch", "o", "k"}));
    CHECK(names(phonemize("folk")) ==
          std::vector<std::string>({"f", "ow", "k"}));
    CHECK(names(phonemize("yolk")) ==
          std::vector<std::string>({"y", "ow", "k"}));
    CHECK(names(phonemize("half")) ==
          std::vector<std::string>({"h", "a", "f"}));
    CHECK(names(phonemize("calm")) ==
          std::vector<std::string>({"k", "aa", "m"}));
    CHECK(names(phonemize("could")) ==
          std::vector<std::string>({"k", "uh", "d"}));
    CHECK(names(phonemize("would")) ==
          std::vector<std::string>({"w", "uh", "d"}));
    CHECK(names(phonemize("should")) ==
          std::vector<std::string>({"sh", "uh", "d"}));
    CHECK(contains(names(phonemize("alkaline")), "l"));
    CHECK(contains(names(phonemize("almanac")), "l"));

    // A word-final b before t is silent (debt, doubt), including -btle
    // (subtle); a medial bt before a vowel keeps it (obtuse). A word-initial
    // mn drops the m (mnemonic).
    CHECK(names(phonemize("debt")) ==
          std::vector<std::string>({"d", "e", "t"}));
    CHECK(names(phonemize("doubt")) ==
          std::vector<std::string>({"d", "ow", "t"}));
    CHECK(names(phonemize("subtle")) ==
          std::vector<std::string>({"s", "uh", "t", "uh", "l"}));
    CHECK(contains(names(phonemize("obtuse")), "b"));
    CHECK(names(phonemize("mnemonic")).front() == "n");
    CHECK(contains(names(phonemize("hymnal")), "n"));

    // Silent h and w in a small closed set of common words.
    CHECK(names(phonemize("hour")) == std::vector<std::string>({"ow", "er"}));
    CHECK(names(phonemize("honest")) ==
          std::vector<std::string>({"o", "n", "i", "s", "t"}));
    CHECK(names(phonemize("sword")) ==
          std::vector<std::string>({"s", "or", "d"}));
    CHECK(names(phonemize("answer")) ==
          std::vector<std::string>({"a", "n", "s", "er"}));

    // -ssion is always a single /ʃən/ (mission, session, discussion). The
    // longer rule must precede -sion so both s letters collapse, otherwise
    // the stray s plus voiced /ʒ/ renders mission as [m i s zh ah n]. Vowel
    // + -sion keeps its /ʒən/ (vision, decision).
    CHECK(names(phonemize("mission")) ==
          std::vector<std::string>({"m", "i", "sh", "ah", "n"}));
    CHECK(names(phonemize("session")) ==
          std::vector<std::string>({"s", "e", "sh", "ah", "n"}));
    CHECK(names(phonemize("passion")) ==
          std::vector<std::string>({"p", "a", "sh", "ah", "n"}));
    CHECK(
        names(phonemize("discussion")) ==
        std::vector<std::string>({"d", "i", "s", "k", "uh", "sh", "ah", "n"}));
    CHECK(names(phonemize("admission")) ==
          std::vector<std::string>({"a", "d", "m", "i", "sh", "ah", "n"}));
    CHECK(names(phonemize("permission")) ==
          std::vector<std::string>({"p", "er", "m", "i", "sh", "ah", "n"}));
    CHECK(names(phonemize("confession")).back() == "n");
    CHECK(!contains(names(phonemize("confession")), "zh"));
    CHECK(names(phonemize("vision")) ==
          std::vector<std::string>({"v", "i", "zh", "ah", "n"}));
    CHECK(names(phonemize("decision")) ==
          std::vector<std::string>({"d", "e", "s", "i", "zh", "ah", "n"}));

    // Digits embedded in alphanumeric tokens are spelled rather than dropped;
    // the all-digit fast path alone used to leave them out (e.g. "B2", "H2O").
    const std::vector<std::string> b2 = names(phonemize("B2"));
    CHECK(contains(b2, "b"));
    CHECK(contains(b2, "t"));
    CHECK(contains(b2, "oo"));
    const std::vector<std::string> h2o = names(phonemize("H2O"));
    CHECK(contains(h2o, "h"));
    CHECK(contains(h2o, "t"));
    CHECK(contains(h2o, "oo"));
    CHECK(contains(h2o, "o"));
    const std::vector<std::string> room101b = names(phonemize("Room 101B"));
    CHECK(contains(room101b, "w"));
    CHECK(contains(room101b, "uh"));
    CHECK(contains(room101b, "n"));
    CHECK(contains(room101b, "b"));

    // Diphthongs carry a moving target; monophthongs do not.
    const Phoneme *ay = findPhoneme("ay");
    CHECK(ay != nullptr);
    if (ay) {
        CHECK(ay->hasEndBands);
        CHECK(ay->endBands[1].frequency > ay->bands[1].frequency);
    }
    const Phoneme *ee = findPhoneme("ee");
    CHECK(ee != nullptr && !ee->hasEndBands);

    const std::vector<Phoneme> vowels = parsePhonemes("[h ee l o]");
    CHECK(vowels.size() == 4);
    CHECK(vowels[0].name == "h");
    CHECK(vowels[3].name == "o");
    CHECK(parsePhonemes("[a, b]").size() == 2);

    // A comma in its own token is the documented pause phoneme; a comma
    // attached to a name is still just a separator.
    const std::vector<Phoneme> withPause = parsePhonemes("[a , b]");
    CHECK(withPause.size() == 3);
    CHECK(withPause[1].name == ",");
    CHECK(withPause[1].kind == PhonemeKind::Silence);
    CHECK(withPause[1].duration > 0.0);
    CHECK(parsePhonemes("[a,,, b]").size() == 2);
    CHECK(parsePhonemes("[a , b .]").size() == 4);
    CHECK(parsePhonemes("[a ? b]").size() == 3);

    rejected = false;
    try {
        (void)parsePhonemes("[h notaphoneme]");
    } catch (const std::invalid_argument &) {
        rejected = true;
    }
    CHECK(rejected);

    // Rendering is finite, deterministic and level-limited.
    Voice voice = voicePreset("robot");
    voice.pitch = 110.0;
    voice.vibratoDepth = 0.0;
    const std::vector<float> first = Synthesizer(voice).render(vowels, 22050);
    const std::vector<float> second = Synthesizer(voice).render(vowels, 22050);
    CHECK(first.size() > 1000);
    CHECK(first == second);
    CHECK(peak(first) > 0.2);
    CHECK(peak(first) <= 0.9001);
    for (float sample : first) {
        CHECK(std::isfinite(sample));
    }

    // A near-zero fundamental never reaches the Nyquist break, so the harmonic
    // count must be capped or rendering becomes effectively unbounded. A huge
    // count renders identically to the cap, and stays finite.
    Voice subsonic = voice;
    subsonic.pitch = 0.000001;
    subsonic.harmonics = 500000;
    subsonic.vibratoDepth = 0.0;
    Voice subsonicCapped = subsonic;
    subsonicCapped.harmonics = 4096;
    const std::vector<float> subsonicSamples =
        Synthesizer(subsonic).render(vowels, 22050);
    CHECK(subsonicSamples == Synthesizer(subsonicCapped).render(vowels, 22050));
    CHECK(peak(subsonicSamples) > 0.0);
    for (float sample : subsonicSamples) {
        CHECK(std::isfinite(sample));
    }

    // A large finite gain used to overflow the float sample before
    // normalisation, leaving an infinite peak and NaN samples. Rendering must
    // stay finite and deterministic at any finite gain.
    Voice loud = voice;
    loud.gain = 1e40;
    const std::vector<float> loudSamples =
        Synthesizer(loud).render(vowels, 22050);
    CHECK(loudSamples.size() == first.size());
    CHECK(loudSamples == Synthesizer(loud).render(vowels, 22050));
    CHECK(peak(loudSamples) > 0.2);
    CHECK(peak(loudSamples) <= 0.9001);
    for (float sample : loudSamples) {
        CHECK(std::isfinite(sample));
    }
    Voice huge = voice;
    huge.gain = 1e300;
    const std::vector<float> hugeSamples =
        Synthesizer(huge).render(vowels, 22050);
    CHECK(peak(hugeSamples) > 0.2);
    for (float sample : hugeSamples) {
        CHECK(std::isfinite(sample));
    }

    // A tiny finite gain makes the raw peak subnormal. Computing 0.9 / peak
    // first overflowed to infinity, producing inf/NaN samples that writeWav()
    // clamped to full scale. Dividing each sample by the peak first keeps every
    // ratio in [-1, 1] and the normalised output finite at the usual 0.9
    // ceiling.
    Voice tiny = voice;
    tiny.gain = std::numeric_limits<double>::min();
    tiny.vibratoDepth = 0.0;
    tiny.breathiness = 0.0;
    tiny.harmonics = 1;
    tiny.pitch = 50.0;
    const std::vector<float> tinySamples =
        Synthesizer(tiny).render(vowels, 22050);
    CHECK(tinySamples.size() == first.size());
    CHECK(tinySamples == Synthesizer(tiny).render(vowels, 22050));
    CHECK(peak(tinySamples) > 0.2);
    CHECK(peak(tinySamples) <= 0.9001);
    for (float sample : tinySamples) {
        CHECK(std::isfinite(sample));
    }

    const Phoneme *moving = findPhoneme("ay");
    CHECK(moving != nullptr);
    if (moving) {
        Phoneme staticVersion = *moving;
        staticVersion.hasEndBands = false;
        CHECK(Synthesizer(voice).render({*moving}, 22050) !=
              Synthesizer(voice).render({staticVersion}, 22050));
    }

    // A diphthong finishes at endBands, so a following phoneme must crossfade
    // from that final target. When ay's endBands equal ee's bands, ay + ee
    // must render identically to ee + ee throughout, including the 35 ms
    // crossfade at the start of the second segment. The old code started that
    // crossfade from ay's initial bands, causing a backward formant jump.
    const Phoneme *diphthong = findPhoneme("ay");
    const Phoneme *settled = findPhoneme("ee");
    CHECK(diphthong != nullptr && settled != nullptr && diphthong->hasEndBands);
    if (diphthong && settled) {
        Voice boundary = voicePreset("robot");
        boundary.breathiness = 0.0;
        boundary.vibratoRate = 0.0;
        boundary.vibratoDepth = 0.0;
        Phoneme glide = *diphthong;
        glide.duration = settled->duration; // identical timing/phase
        const auto viaGlide =
            Synthesizer(boundary).render({glide, *settled}, 8000);
        const auto viaSettled =
            Synthesizer(boundary).render({*settled, *settled}, 8000);
        CHECK(viaGlide.size() == viaSettled.size());
        // Only the second segment is comparable: the first differs because a
        // diphthong glides while a plain vowel holds its formants.
        const size_t offset = static_cast<size_t>(settled->duration * 8000);
        bool secondMatches = viaGlide.size() == viaSettled.size();
        if (secondMatches) {
            for (size_t sample = offset; sample < viaGlide.size(); ++sample) {
                if (viaGlide[sample] != viaSettled[sample]) {
                    secondMatches = false;
                    break;
                }
            }
        }
        CHECK(secondMatches);
    }

    Voice higher = voice;
    higher.pitch = 190.0;
    CHECK(Synthesizer(higher).render(vowels, 22050) != first);

    const Voice whisper = voicePreset("whisper");
    CHECK(peak(Synthesizer(whisper).render(vowels, 22050)) > 0.2);

    CHECK(synthesizeText("hello", voice, 22050) ==
          Synthesizer(voice).render(phonemize("hello"), 22050));

    rejected = false;
    try {
        (void)Synthesizer(voice).render(vowels, 4000);
    } catch (const std::invalid_argument &) {
        rejected = true;
    }
    CHECK(rejected);

    // A slow rate must really lengthen the audio. render() used to clamp the
    // rate to 0.1, silently capping the length and breaking the documented
    // round(total duration * sampleRate / rate) contract.
    const Phoneme *slowPhoneme = findPhoneme("a");
    CHECK(slowPhoneme != nullptr);
    if (slowPhoneme) {
        Voice slow = voice;
        slow.rate = 0.05;
        const std::vector<float> slowSamples =
            Synthesizer(slow).render({*slowPhoneme}, 8000);
        const size_t expected = static_cast<size_t>(
            std::llround(slowPhoneme->duration * 8000.0 / slow.rate));
        CHECK(slowSamples.size() == expected);
        CHECK(slowSamples.size() == 22400);

        Voice normal = voice;
        normal.rate = 1.0;
        const std::vector<float> normalSamples =
            Synthesizer(normal).render({*slowPhoneme}, 8000);
        CHECK(slowSamples.size() == normalSamples.size() * 20);
    }

    // The contract covers the WHOLE sequence. Rounding each phoneme in
    // isolation made the total drift by up to one sample per segment, so
    // [t e s t] at 44.1 kHz returned 12790 instead of the promised 12789.
    {
        const std::vector<Phoneme> phrase = parsePhonemes("[t e s t]");
        CHECK(phrase.size() == 4);
        double total = 0.0;
        for (const Phoneme &p : phrase) {
            total += p.duration;
        }
        for (int sr : {8000, 22050, 44100}) {
            for (double speakingRate : {1.0, 0.7, 1.3}) {
                Voice v = voice;
                v.rate = speakingRate;
                const size_t expected = static_cast<size_t>(
                    std::llround(total * sr / speakingRate));
                CHECK(Synthesizer(v).render(phrase, sr).size() == expected);
            }
        }
    }
    {
        // Accumulated drift is largest for long sequences of short segments.
        const Phoneme *t = findPhoneme("t");
        CHECK(t != nullptr);
        if (t) {
            const std::vector<Phoneme> many(20, *t);
            const size_t expected =
                static_cast<size_t>(std::llround(20.0 * t->duration * 44100.0));
            CHECK(Synthesizer(voice).render(many, 44100).size() == expected);
            CHECK(expected == 30870);
        }
    }

    // The minimum-one-sample segment clamp used to add one bogus sample per
    // phoneme at fast rates. When the cumulative boundary rounds to zero, the
    // documented contract requires an empty buffer, not one silent sample.
    {
        const Phoneme *a = findPhoneme("a");
        CHECK(a != nullptr);
        if (a) {
            for (double speakingRate : {1e6, 1e10, 1e100}) {
                Voice fast = voice;
                fast.rate = speakingRate;
                const std::vector<float> samples =
                    Synthesizer(fast).render({*a}, 44100);
                CHECK(samples.empty());
            }
            Voice fast = voice;
            fast.rate = 1e6;
            const std::vector<float> four =
                Synthesizer(fast).render(parsePhonemes("[h ee l o]"), 44100);
            CHECK(four.empty());
        }
        // A normal rate still produces the exact documented count and keeps
        // rendering finite and deterministic.
        Voice normal = voice;
        normal.rate = 1.0;
        const std::vector<Phoneme> phrase = parsePhonemes("[h ee l o]");
        double total = 0.0;
        for (const Phoneme &p : phrase) {
            total += p.duration;
        }
        const std::vector<float> normalSamples =
            Synthesizer(normal).render(phrase, 44100);
        CHECK(normalSamples.size() ==
              static_cast<size_t>(std::llround(total * 44100.0)));
        CHECK(normalSamples == Synthesizer(normal).render(phrase, 44100));
    }

    // Removing the clamp would divide by zero unless rate is validated.
    const double badRates[] = {0.0, -1.0,
                               std::numeric_limits<double>::infinity(),
                               std::numeric_limits<double>::quiet_NaN()};
    for (double badRate : badRates) {
        Voice bad = voice;
        bad.rate = badRate;
        rejected = false;
        try {
            (void)Synthesizer(bad).render(vowels, 22050);
        } catch (const std::invalid_argument &) {
            rejected = true;
        }
        CHECK(rejected);
    }

    // An extreme rate whose segment length exceeds int range is rejected
    // rather than overflowing llround or attempting a huge allocation.
    if (slowPhoneme) {
        Voice extreme = voice;
        extreme.rate = 1e-300;
        rejected = false;
        try {
            (void)Synthesizer(extreme).render({*slowPhoneme}, 8000);
        } catch (const std::invalid_argument &) {
            rejected = true;
        }
        CHECK(rejected);
    }

    // The WAV header is 44 bytes of little-endian PCM metadata.
    const std::vector<uint8_t> header = wavHeader(1000, 44100);
    CHECK(header.size() == 44);
    CHECK(std::string(header.begin(), header.begin() + 4) == "RIFF");
    CHECK(std::string(header.begin() + 8, header.begin() + 12) == "WAVE");
    CHECK(std::string(header.begin() + 12, header.begin() + 16) == "fmt ");
    CHECK(std::string(header.begin() + 36, header.begin() + 40) == "data");
    CHECK(littleEndian32(header, 24) == 44100);
    CHECK(littleEndian32(header, 28) == 88200);
    CHECK(littleEndian32(header, 40) == 2000);
    CHECK(littleEndian32(header, 4) == 36 + 2000);

    // The RIFF chunk size stores 36 + dataBytes, so data sizes that would
    // overflow that field must be rejected rather than wrapping the header.
    bool riffOverflowRejected = false;
    try {
        (void)wavHeader(0x7fffffffull, 44100);
    } catch (const std::invalid_argument &) {
        riffOverflowRejected = true;
    }
    CHECK(riffOverflowRejected);

    const std::string path = "formant_test_output.wav";
    writeWav(path, first, 22050);
    FILE *file = std::fopen(path.c_str(), "rb");
    CHECK(file != nullptr);
    if (file) {
        std::fseek(file, 0, SEEK_END);
        CHECK(std::ftell(file) == static_cast<long>(44 + first.size() * 2));
        std::fclose(file);
    }
    std::remove(path.c_str());

    // Empty input, the minimum sample rate, case-insensitive phonemes.
    CHECK(phonemize("").empty());
    CHECK(Synthesizer(voice).render({}, 22050).empty());
    CHECK(!Synthesizer(voice).render(vowels, 8000).empty());
    const std::vector<Phoneme> upper = parsePhonemes("[H EE]");
    CHECK(upper.size() == 2);
    if (upper.size() == 2) {
        CHECK(upper[0].name == "h");
        CHECK(upper[1].name == "ee");
    }

    // Samples outside [-1, 1] clamp at the 16-bit WAV limits.
    const std::string clampPath = "formant_clamp_test.wav";
    writeWav(clampPath, {2.0f, -2.0f, 0.0f}, 8000);
    FILE *clampFile = std::fopen(clampPath.c_str(), "rb");
    CHECK(clampFile != nullptr);
    if (clampFile) {
        std::fseek(clampFile, 44, SEEK_SET);
        int16_t written[3] = {0, 0, 0};
        CHECK(std::fread(written, sizeof(int16_t), 3, clampFile) == 3);
        CHECK(written[0] == 32767);
        CHECK(written[1] == -32768);
        CHECK(written[2] == 0);
        std::fclose(clampFile);
    }
    std::remove(clampPath.c_str());

    // Non-finite samples would reach std::llround, which is undefined
    // behaviour for NaN and infinity, and leave a corrupt WAV behind. They are
    // rejected before the file is opened.
    const std::string invalidPath = "formant_invalid_samples.wav";
    std::remove(invalidPath.c_str());
    for (float badSample : {std::numeric_limits<float>::quiet_NaN(),
                            std::numeric_limits<float>::infinity(),
                            -std::numeric_limits<float>::infinity()}) {
        bool nonFiniteRejected = false;
        try {
            writeWav(invalidPath, {0.0f, badSample}, 8000);
        } catch (const std::invalid_argument &) {
            nonFiniteRejected = true;
        }
        CHECK(nonFiniteRejected);
        CHECK(std::fopen(invalidPath.c_str(), "rb") == nullptr);
    }
    std::remove(invalidPath.c_str());

    // Output formats share one encoder with the default WAV path. The
    // original 16-bit writer must remain byte-identical; the new formats only
    // change the header fields and the per-sample encoding.
    CHECK(audioFormatSampleBytes(AudioFormat::Pcm16) == 2);
    CHECK(audioFormatSampleBytes(AudioFormat::Pcm24) == 3);
    CHECK(audioFormatSampleBytes(AudioFormat::Float32) == 4);
    CHECK(audioFormatSampleBytes(AudioFormat::RawPcm16) == 2);
    CHECK(audioFormatSampleBytes(AudioFormat::RawPcm24) == 3);
    CHECK(audioFormatSampleBytes(AudioFormat::RawFloat32) == 4);
    CHECK(!isRawAudioFormat(AudioFormat::Pcm16));
    CHECK(isRawAudioFormat(AudioFormat::RawPcm16));

    const std::vector<uint8_t> header24 =
        wavHeader(1000, 44100, AudioFormat::Pcm24);
    CHECK(header24.size() == 44);
    CHECK(std::string(header24.begin() + 8, header24.begin() + 12) == "WAVE");
    CHECK(littleEndian16(header24, 20) == 1); // WAVE_FORMAT_PCM
    CHECK(littleEndian16(header24, 22) == 1); // mono
    CHECK(littleEndian32(header24, 24) == 44100);
    CHECK(littleEndian32(header24, 28) == 44100u * 3u);
    CHECK(littleEndian16(header24, 32) == 3);  // packed 24-bit block align
    CHECK(littleEndian16(header24, 34) == 24); // bits per sample
    CHECK(littleEndian32(header24, 40) == 3000);
    CHECK(littleEndian32(header24, 4) == 36u + 3000u);

    const std::vector<uint8_t> headerFloat =
        wavHeader(1000, 44100, AudioFormat::Float32);
    CHECK(headerFloat.size() == 44);
    CHECK(littleEndian16(headerFloat, 20) == 3); // IEEE float
    CHECK(littleEndian32(headerFloat, 24) == 44100);
    CHECK(littleEndian32(headerFloat, 28) == 44100u * 4u);
    CHECK(littleEndian16(headerFloat, 32) == 4);
    CHECK(littleEndian16(headerFloat, 34) == 32);
    CHECK(littleEndian32(headerFloat, 40) == 4000);

    bool rawHeaderRejected = false;
    try {
        (void)wavHeader(10, 8000, AudioFormat::RawPcm16);
    } catch (const std::invalid_argument &) {
        rawHeaderRejected = true;
    }
    CHECK(rawHeaderRejected);

    // A byte rate that would overflow the 32-bit WAV header is rejected.
    bool byteRateRejected = false;
    try {
        (void)wavHeader(0, std::numeric_limits<int>::max(),
                        AudioFormat::Float32);
    } catch (const std::invalid_argument &) {
        byteRateRejected = true;
    }
    CHECK(byteRateRejected);

    // Integer encodings clamp exactly like the original 16-bit writer, with
    // the asymmetric full-scale value that gives -32768 / -8388608 at -1.
    std::ostringstream raw16;
    writeAudio(raw16, {2.0f, -2.0f, 0.0f}, 8000, AudioFormat::RawPcm16);
    const std::vector<uint8_t> pcm16Bytes = bytesOf(raw16);
    CHECK(pcm16Bytes.size() == 6);
    CHECK(littleEndian16(pcm16Bytes, 0) == 32767);
    CHECK(littleEndian16(pcm16Bytes, 2) == 32768u); // -32768 as raw bits
    CHECK(littleEndian16(pcm16Bytes, 4) == 0);

    std::ostringstream raw24;
    writeAudio(raw24, {2.0f, -2.0f, 0.0f}, 8000, AudioFormat::RawPcm24);
    const std::vector<uint8_t> pcm24Bytes = bytesOf(raw24);
    CHECK(pcm24Bytes.size() == 9);
    CHECK(pcm24Bytes[0] == 0xff && pcm24Bytes[1] == 0xff &&
          pcm24Bytes[2] == 0x7f);
    CHECK(pcm24Bytes[3] == 0x00 && pcm24Bytes[4] == 0x00 &&
          pcm24Bytes[5] == 0x80);
    CHECK(pcm24Bytes[6] == 0x00 && pcm24Bytes[7] == 0x00 &&
          pcm24Bytes[8] == 0x00);

    std::ostringstream rawFloat;
    writeAudio(rawFloat, {2.0f, -2.0f, 0.0f}, 8000, AudioFormat::RawFloat32);
    const std::vector<uint8_t> floatBytes = bytesOf(rawFloat);
    CHECK(floatBytes.size() == 12);
    CHECK(littleEndian32(floatBytes, 0) == 0x3f800000u); // +1.0f
    CHECK(littleEndian32(floatBytes, 4) == 0xbf800000u); // -1.0f
    CHECK(littleEndian32(floatBytes, 8) == 0x00000000u);

    // A WAV-format stream starts with a header; a raw stream contains only
    // sample bytes. Both use the same writing path and fixed-size chunks.
    std::ostringstream wav24Stream;
    writeAudio(wav24Stream, {0.0f, 0.5f}, 8000, AudioFormat::Pcm24);
    const std::vector<uint8_t> wav24Bytes = bytesOf(wav24Stream);
    CHECK(wav24Bytes.size() == 44u + 6u);
    CHECK(std::string(wav24Bytes.begin(), wav24Bytes.begin() + 4) == "RIFF");
    std::ostringstream matchingRaw24;
    writeAudio(matchingRaw24, {0.0f, 0.5f}, 8000, AudioFormat::RawPcm24);
    const std::vector<uint8_t> raw24Bytes = bytesOf(matchingRaw24);
    CHECK(std::equal(wav24Bytes.begin() + 44, wav24Bytes.end(),
                     raw24Bytes.begin()));

    std::vector<float> chunked(5000);
    for (size_t i = 0; i < chunked.size(); ++i) {
        chunked[i] = i % 2 == 0 ? 1.0f : -1.0f;
    }
    std::ostringstream bigRaw;
    writeAudio(bigRaw, chunked, 8000, AudioFormat::RawPcm16);
    const std::vector<uint8_t> bigBytes = bytesOf(bigRaw);
    CHECK(bigBytes.size() == 10000);
    CHECK(littleEndian16(bigBytes, 0) == 32767);
    CHECK(littleEndian16(bigBytes, 4095u * 2u) == 32768u);
    CHECK(littleEndian16(bigBytes, 4096u * 2u) == 32767);

    // Non-finite samples are rejected before any bytes are written, for every
    // format, so a failed render cannot leave a truncated file behind.
    for (AudioFormat format :
         {AudioFormat::Pcm16, AudioFormat::Pcm24, AudioFormat::Float32,
          AudioFormat::RawPcm16, AudioFormat::RawPcm24,
          AudioFormat::RawFloat32}) {
        std::ostringstream stream;
        bool nonFiniteRejected = false;
        try {
            writeAudio(stream, {0.0f, std::numeric_limits<float>::quiet_NaN()},
                       8000, format);
        } catch (const std::invalid_argument &) {
            nonFiniteRejected = true;
        }
        CHECK(nonFiniteRejected);
        CHECK(stream.str().empty());
    }
    const std::string rawPath = "formant_invalid_raw.raw";
    std::remove(rawPath.c_str());
    bool rawFileRejected = false;
    try {
        writeAudioFile(rawPath, {std::numeric_limits<float>::infinity()}, 8000,
                       AudioFormat::RawPcm16);
    } catch (const std::invalid_argument &) {
        rawFileRejected = true;
    }
    CHECK(rawFileRejected);
    CHECK(std::fopen(rawPath.c_str(), "rb") == nullptr);

    // Optional dither is deterministic from a seed and affects only integer
    // quantization. It is output noise, not a synthesis source: the oscillator
    // path remains sine-only and unchanged.
    CHECK(!isFloatAudioFormat(AudioFormat::Pcm16));
    CHECK(isFloatAudioFormat(AudioFormat::Float32));
    CHECK(isFloatAudioFormat(AudioFormat::RawFloat32));
    std::vector<float> ditherSamples(1000, 0.12345f);
    ditherSamples[0] = 1.0f;
    ditherSamples[1] = -1.0f;
    AudioWriteOptions dither42;
    dither42.dither = true;
    dither42.seed = 42;
    AudioWriteOptions dither43 = dither42;
    dither43.seed = 43;
    AudioWriteOptions dither0 = dither42;
    dither0.seed = 0;

    std::ostringstream plainStream;
    std::ostringstream ditherStreamA;
    std::ostringstream ditherStreamB;
    std::ostringstream ditherStreamC;
    std::ostringstream ditherStreamZero;
    writeAudio(plainStream, ditherSamples, 8000, AudioFormat::RawPcm16);
    writeAudio(ditherStreamA, ditherSamples, 8000, AudioFormat::RawPcm16,
               dither42);
    writeAudio(ditherStreamB, ditherSamples, 8000, AudioFormat::RawPcm16,
               dither42);
    writeAudio(ditherStreamC, ditherSamples, 8000, AudioFormat::RawPcm16,
               dither43);
    writeAudio(ditherStreamZero, ditherSamples, 8000, AudioFormat::RawPcm16,
               dither0);
    const std::vector<uint8_t> plainDitherBytes = bytesOf(plainStream);
    const std::vector<uint8_t> ditherBytesA = bytesOf(ditherStreamA);
    const std::vector<uint8_t> ditherBytesB = bytesOf(ditherStreamB);
    const std::vector<uint8_t> ditherBytesC = bytesOf(ditherStreamC);
    const std::vector<uint8_t> ditherBytesZero = bytesOf(ditherStreamZero);
    CHECK(ditherBytesA == ditherBytesB);
    CHECK(ditherBytesA != ditherBytesC);
    CHECK(ditherBytesA != ditherBytesZero);
    CHECK(ditherBytesA != plainDitherBytes);
    CHECK(ditherBytesA.size() == plainDitherBytes.size());
    // Dither cannot push a full-scale sample outside the integer range.
    CHECK(littleEndian16(ditherBytesA, 0) == 32767);
    CHECK(littleEndian16(ditherBytesA, 2) <= 32768u);

    std::ostringstream wavDitherStream;
    writeAudio(wavDitherStream, ditherSamples, 8000, AudioFormat::Pcm24,
               dither42);
    const std::vector<uint8_t> wavDitherBytes = bytesOf(wavDitherStream);
    const std::vector<uint8_t> expectedDitherHeader =
        wavHeader(ditherSamples.size(), 8000, AudioFormat::Pcm24);
    CHECK(std::equal(wavDitherBytes.begin(), wavDitherBytes.begin() + 44,
                     expectedDitherHeader.begin()));
    CHECK(wavDitherBytes.size() == 44u + ditherSamples.size() * 3u);

    std::ostringstream dither24A;
    std::ostringstream dither24B;
    writeAudio(dither24A, ditherSamples, 8000, AudioFormat::RawPcm24, dither42);
    writeAudio(dither24B, ditherSamples, 8000, AudioFormat::RawPcm24, dither42);
    CHECK(bytesOf(dither24A) == bytesOf(dither24B));
    CHECK(bytesOf(dither24A).size() == ditherSamples.size() * 3u);

    // Float formats reject dither before writing anything.
    for (AudioFormat format : {AudioFormat::Float32, AudioFormat::RawFloat32}) {
        std::ostringstream stream;
        bool ditherRejected = false;
        try {
            writeAudio(stream, ditherSamples, 8000, format, dither42);
        } catch (const std::invalid_argument &) {
            ditherRejected = true;
        }
        CHECK(ditherRejected);
        CHECK(stream.str().empty());
    }

    // Arbitrary rates keep the sample-count contract and normalised peak, and
    // their WAV headers describe the rate the samples were rendered at.
    double vowelsDuration = 0.0;
    for (const Phoneme &phoneme : vowels) {
        vowelsDuration += phoneme.duration;
    }
    for (int rate : {22050, 48000, 96000, 192000}) {
        const std::vector<float> rendered =
            Synthesizer(voice).render(vowels, rate);
        CHECK(rendered.size() == static_cast<size_t>(std::llround(
                                     vowelsDuration * rate / voice.rate)));
        CHECK(peak(rendered) <= 0.9001);
        const std::vector<uint8_t> header =
            wavHeader(rendered.size(), rate, AudioFormat::Pcm24);
        CHECK(littleEndian32(header, 24) == static_cast<uint32_t>(rate));
        CHECK(littleEndian32(header, 28) == static_cast<uint32_t>(rate) * 3u);
    }

    // The IPA front end is independent of English: every advertised symbol
    // parses, including extensions and the full vowel chart.
    const std::vector<std::string> symbols = ipaSymbols();
    CHECK(symbols.size() >= 120);
    for (const std::string &symbol : symbols) {
        CHECK(!parseIpa(symbol).empty());
        // The parser consumes one code point at a time, so a multi-codepoint
        // base entry could never match and would only be a dead list entry.
        size_t leadingBytes = 0;
        for (unsigned char byte : symbol) {
            if ((byte & 0xC0u) != 0x80u) {
                ++leadingBytes;
            }
        }
        CHECK(leadingBytes == 1);
    }
    CHECK(contains(symbols, "\u0259"));
    CHECK(contains(symbols, "\u0283"));
    CHECK(contains(symbols, "\u0288"));
    CHECK(contains(symbols, "\u2C71"));
    CHECK(contains(symbols, "\u01C3"));
    CHECK(contains(symbols, "\u1D7B"));
    CHECK(contains(symbols, "\u1D7F"));
    CHECK(!contains(symbols, "\u1D57"));

    // Articulatory features drive formants: front vowels have a higher F2
    // than back vowels, and open vowels have a higher F1 than close vowels.
    const Phoneme closeFront = parseIpa("i").front();
    const Phoneme openBack = parseIpa("\u0251").front();
    CHECK(closeFront.bands[0].frequency < openBack.bands[0].frequency);
    CHECK(closeFront.bands[1].frequency > openBack.bands[1].frequency);
    CHECK(parseIpa("\u00F8").front().bands[1].frequency <
          parseIpa("e").front().bands[1].frequency);

    // Diacritics, length, stress, ties and tones all survive parsing.
    CHECK(!parseIpa("z\u0325").front().voiced);
    CHECK(parseIpa("s\u032C").front().voiced);
    CHECK(parseIpa("\u014B\u030A").front().voiced == false);
    CHECK(parseIpa("a\u02D0").front().duration >
          parseIpa("a").front().duration);
    CHECK(parseIpa("\u02C8a").front().amplitude > 1.0);
    CHECK(parseIpa("t\u0361\u0283").size() == 2);
    CHECK(parseIpa("\u02A7").size() == 2);
    CHECK(parseIpa("a\u02E5\u02E9").front().pitchStart >
          parseIpa("a\u02E5\u02E9").front().pitchEnd);
    CHECK(parseIpa("a\u030C").front().pitchEnd >
          parseIpa("a\u030C").front().pitchStart);

    // A run of prefix tone bars describes a contour from its first level to
    // its last, matching the equivalent postfix run. Previously each prefix
    // bar overwrote the last, leaving a flat tone at the final bar's level.
    const Phoneme prefixFall = parseIpa("\u02E5\u02E9a").front();
    const Phoneme postfixFall = parseIpa("a\u02E5\u02E9").front();
    CHECK(prefixFall.pitchStart == postfixFall.pitchStart);
    CHECK(prefixFall.pitchEnd == postfixFall.pitchEnd);
    CHECK(prefixFall.pitchStart > prefixFall.pitchEnd);
    const Phoneme prefixRise = parseXsampa("<LH>a").front();
    const Phoneme postfixRise = parseXsampa("a<LH>").front();
    CHECK(prefixRise.pitchStart == postfixRise.pitchStart);
    CHECK(prefixRise.pitchEnd == postfixRise.pitchEnd);
    CHECK(prefixRise.pitchEnd > prefixRise.pitchStart);
    CHECK(prefixFall.pitchStart != prefixRise.pitchStart);
    CHECK(parseIpa("p\u02BC").front().kind == PhonemeKind::Plosive);
    CHECK(parseIpa("n\u0303").front().bands[1].amplitude <
          parseIpa("n").front().bands[1].amplitude);
    CHECK(parseIpa("e\u031E").front().bands[0].frequency >
          parseIpa("e").front().bands[0].frequency);

    // A diacritic written after a suprasegmental still qualifies its own base.
    // Previously it became a prefix for the next segment, or was dropped at
    // end of input, so order relative to the length mark changed the audio.
    const Phoneme lateNasal = parseIpa("a\u02D0\u0303").front();
    CHECK(lateNasal.duration == parseIpa("a\u02D0").front().duration);
    CHECK(lateNasal.bands[1].amplitude <
          parseIpa("a\u02D0").front().bands[1].amplitude);
    CHECK(lateNasal.bands[1].amplitude ==
          parseIpa("a\u0303\u02D0").front().bands[1].amplitude);
    const auto nasalFirst = parseIpa("a\u02D0\u0303b");
    CHECK(nasalFirst.size() == 2);
    CHECK(nasalFirst[0].bands[1].amplitude <
          parseIpa("a\u02D0").front().bands[1].amplitude);
    CHECK(nasalFirst[1].bands[1].amplitude ==
          parseIpa("b").front().bands[1].amplitude);
    const auto nasalSecond = parseIpa("a\u02D0b\u0303");
    CHECK(nasalSecond.size() == 2);
    CHECK(nasalSecond[0].bands[1].amplitude ==
          parseIpa("a\u02D0").front().bands[1].amplitude);
    CHECK(nasalSecond[1].bands[1].amplitude <
          parseIpa("b").front().bands[1].amplitude);
    CHECK(!parseIpa("a\u02D0\u0325").front().voiced);
    CHECK(parseXsampa("a_X:").front().duration ==
          parseXsampa("a:_X").front().duration);

    // An affricate ligature is one consonant with two articulatory components,
    // so a diacritic qualifies the whole symbol rather than only its release.
    // The tie-bar spelling of the same affricate keeps applying the diacritic
    // to just the symbol it follows.
    const std::vector<Phoneme> voicedTesh = parseIpa("\u02A7\u032C");
    CHECK(voicedTesh.size() == 2);
    CHECK(voicedTesh[0].voiced);
    CHECK(voicedTesh[1].voiced);
    const std::vector<Phoneme> voicedLigature = parseIpa("\u02A4");
    CHECK(voicedTesh[0].voiced == voicedLigature[0].voiced);
    CHECK(voicedTesh[1].voiced == voicedLigature[1].voiced);
    const std::vector<Phoneme> dentalTesh = parseIpa("\u02A7\u032A");
    CHECK(dentalTesh[0].bands[1].frequency ==
          parseIpa("t\u032A").front().bands[1].frequency);
    CHECK(dentalTesh[1].bands[1].frequency ==
          parseIpa("\u0283\u032A").front().bands[1].frequency);
    CHECK(!parseIpa("t").front().voiced);
    const std::vector<Phoneme> tiedVoiced = parseIpa("t\u0361\u0283\u032C");
    CHECK(tiedVoiced.size() == 2);
    CHECK(!tiedVoiced[0].voiced);
    CHECK(tiedVoiced[1].voiced);

    // Precomposed rhotic vowels are the compiled equivalents of ə˞ and ɜ˞:
    // they lower F3 and strengthen its band rather than rendering as plain ə
    // and ɜ. The X-SAMPA @` alias inherits the same articulation.
    const Phoneme rhoticSchwa = parseIpa("\u025A").front();
    CHECK(rhoticSchwa.bands[2].frequency ==
          parseIpa("\u0259\u02DE").front().bands[2].frequency);
    CHECK(rhoticSchwa.bands[2].amplitude >
          parseIpa("\u0259").front().bands[2].amplitude);
    const Phoneme rhoticOpenMid = parseIpa("\u025D").front();
    CHECK(rhoticOpenMid.bands[2].frequency ==
          parseIpa("\u025C\u02DE").front().bands[2].frequency);
    CHECK(rhoticOpenMid.bands[2].amplitude >
          parseIpa("\u025C").front().bands[2].amplitude);
    CHECK(parseXsampa("@`").front().bands[2].frequency ==
          rhoticSchwa.bands[2].frequency);

    // Suprasegmentals add pauses and do not swallow surrounding symbols.
    const std::vector<Phoneme> phrase = parseIpa("/\u02C8h\u0259lo\u028A/");
    CHECK(phrase.size() >= 4);
    CHECK(phrase.front().amplitude > 1.0);
    CHECK(parseIpa("a|b").size() == 3);
    CHECK(parseIpa("a.b").size() == 3);
    CHECK(parseIpa("a\u203Fb").size() == 2);

    rejected = false;
    try {
        (void)parseIpa("a\uFFFF");
    } catch (const std::invalid_argument &) {
        rejected = true;
    }
    CHECK(rejected);

    // X-SAMPA translates to the same articulatory representation.
    CHECK(names(parseXsampa("h@")) == names(parseIpa("h\u0259")));
    CHECK(parseXsampa("tS").size() == 2);
    CHECK(parseXsampa("b_<").front().kind == PhonemeKind::Plosive);
    CHECK(parseXsampa("k_>").front().duration > 0.0);
    CHECK(parseXsampa("\"a").front().amplitude > 1.0);
    CHECK(parseXsampa("e_o").front().bands[0].frequency >
          parseXsampa("e").front().bands[0].frequency);
    CHECK(parseXsampa("d_}").front().duration <
          parseXsampa("d").front().duration);
    CHECK(parseXsampa("a_R").front().pitchEnd >
          parseXsampa("a_R").front().pitchStart);

    // Non-IPA near-close central vowels use the standard X-SAMPA aliases.
    CHECK(names(parseXsampa("I\\")) == names(parseIpa("\u1D7B")));
    CHECK(names(parseXsampa("U\\")) == names(parseIpa("\u1D7F")));
    CHECK(parseXsampa("I\\").front().bands[1].frequency <
          parseXsampa("I").front().bands[1].frequency);
    CHECK(parseXsampa("U\\").front().bands[1].frequency >
          parseXsampa("U").front().bands[1].frequency);
    CHECK(parseXsampa("U\\").front().bands[1].frequency <
          parseXsampa("I\\").front().bands[1].frequency);

    // Apical and laminal diacritics are recognised no-op modifiers, and the
    // underscore tie-bar convention produces the same double articulations.
    const Phoneme apical = parseIpa("t\u033A").front();
    const Phoneme laminal = parseIpa("t\u033B").front();
    CHECK(apical.kind == parseIpa("t").front().kind);
    CHECK(laminal.kind == parseIpa("t").front().kind);
    CHECK(apical.bands[1].frequency ==
          parseIpa("t").front().bands[1].frequency);
    CHECK(laminal.bands[1].frequency ==
          parseIpa("t").front().bands[1].frequency);
    CHECK(parseXsampa("t_a").front().name == apical.name);
    CHECK(parseXsampa("t_m").front().name == laminal.name);
    CHECK(names(parseXsampa("k_p")) == names(parseXsampa("kp")));
    CHECK(names(parseXsampa("g_b")) == names(parseXsampa("gb")));
    CHECK(names(parseXsampa("N_m")) == names(parseXsampa("Nm")));
    CHECK(parseXsampa("k_p").size() == 2);
    CHECK(names(parseXsampa("t_s")) == names(parseXsampa("ts")));
    CHECK(names(parseXsampa("d_Z")) == names(parseXsampa("dZ")));
    CHECK(names(parseXsampa("t_s\\")) == names(parseXsampa("ts\\")));
    CHECK(names(parseXsampa("d_K\\")) == names(parseXsampa("dK\\")));

    // _r is the raised diacritic (U+031D), not the global-rise arrow: it
    // raises vowel height (lowering F1) in the opposite direction to _o.
    const Phoneme xsampaRaised = parseXsampa("a_r").front();
    CHECK(xsampaRaised.name == parseIpa("a\u031D").front().name);
    CHECK(xsampaRaised.bands[0].frequency <
          parseXsampa("a").front().bands[0].frequency);
    CHECK(xsampaRaised.bands[0].frequency <
          parseXsampa("a_o").front().bands[0].frequency);
    CHECK(xsampaRaised.pitchStart == parseXsampa("a").front().pitchStart);
    CHECK(xsampaRaised.pitchEnd == parseXsampa("a").front().pitchEnd);
    CHECK(names(parseXsampa("a<R>")) != names(parseXsampa("a_r")));

    // A global rise is position-independent. A prefix arrow used to be
    // discarded because it was only applied to an existing base.
    {
        const std::vector<Phoneme> prefixed = parseIpa("\u2197a");
        CHECK(prefixed.back().pitchRise);
        const std::vector<Phoneme> prefixedXsampa = parseXsampa("<R>a");
        CHECK(prefixedXsampa.back().pitchRise);
        CHECK(!parseIpa("a").back().pitchRise);
        Voice flat = voicePreset("robot");
        flat.vibratoDepth = 0.0;
        flat.breathiness = 0.0;
        auto withoutCue = prefixed;
        for (Phoneme &phoneme : withoutCue) {
            phoneme.pitchRise = false;
        }
        CHECK(Synthesizer(flat).render(prefixed, 22050) !=
              Synthesizer(flat).render(withoutCue, 22050));
    }

    rejected = false;
    try {
        (void)parseXsampa("a|b");
    } catch (const std::invalid_argument &) {
        rejected = true;
    }
    CHECK(!rejected);

    const Voice ipaVoice = voicePreset("neutral");
    CHECK(
        synthesizeIpa("\u02C8h\u0259lo\u028A", ipaVoice, 22050) ==
        Synthesizer(ipaVoice).render(parseIpa("\u02C8h\u0259lo\u028A"), 22050));
    CHECK(synthesizeXsampa("h@", ipaVoice, 22050) ==
          Synthesizer(ipaVoice).render(parseXsampa("h@"), 22050));
    CHECK(peak(synthesizeIpa("a\u02E5\u02E9", ipaVoice, 22050)) > 0.2);

    // Text front ends are language-tagged, but every module returns the same
    // phoneme sequence and feeds the same synthesiser. English stays default.
    CHECK(names(phonemize("Hello, world.", Language::English)) == hello);
    CHECK(languageTags().size() == 2);
    CHECK(languageTag(Language::English) == "en");
    CHECK(languageTag(Language::Spanish) == "es");
    CHECK(languageName(Language::English) == "English");
    CHECK(languageName(Language::Spanish) == "Spanish");
    CHECK(languageFromTag("en") == Language::English);
    CHECK(languageFromTag("EN-us") == Language::English);
    CHECK(languageFromTag("es-MX") == Language::Spanish);
    CHECK(languageFromTag("es_419") == Language::Spanish);
    rejected = false;
    try {
        (void)languageFromTag("fr");
    } catch (const std::invalid_argument &) {
        rejected = true;
    }
    CHECK(rejected);

    // Castilian Spanish is rule based: context-sensitive c/g/qu/gü, b/v
    // merger, ll/ñ/ch, trill versus tap, silent h and orthographic stress.
    CHECK(names(phonemize("hola", Language::Spanish)) ==
          std::vector<std::string>({"o", "l", "a"}));
    CHECK(names(phonemize("casa", Language::Spanish)) ==
          std::vector<std::string>({"k", "a", "s", "a"}));
    CHECK(names(phonemize("gente", Language::Spanish)) ==
          std::vector<std::string>({"x", "e", "n", "t", "e"}));
    CHECK(names(phonemize("guitarra", Language::Spanish)) ==
          std::vector<std::string>({"g", "i", "t", "a", "r", "a"}));
    CHECK(names(phonemize("perro", Language::Spanish)) ==
          std::vector<std::string>({"p", "e", "r", "o"}));
    CHECK(names(phonemize("pero", Language::Spanish)) ==
          std::vector<std::string>({"p", "e", "\u027E", "o"}));
    CHECK(names(phonemize("llave", Language::Spanish)) ==
          std::vector<std::string>({"\u029D", "a", "b", "e"}));
    CHECK(names(phonemize("ma\u00F1ana", Language::Spanish)) ==
          std::vector<std::string>({"m", "a", "\u0272", "a", "n", "a"}));
    CHECK(names(phonemize("zapato", Language::Spanish)) ==
          std::vector<std::string>({"\u03B8", "a", "p", "a", "t", "o"}));
    CHECK(names(phonemize("queso", Language::Spanish)) ==
          std::vector<std::string>({"k", "e", "s", "o"}));
    CHECK(names(phonemize("chico", Language::Spanish)) ==
          std::vector<std::string>({"t", "\u0283", "i", "k", "o"}));
    // Diphthongs become glides, and the default stress lands on the
    // penultimate syllable. ciudad keeps its final /d/ and stresses the a.
    const auto ciudad = phonemize("ciudad", Language::Spanish);
    CHECK(names(ciudad) ==
          std::vector<std::string>({"\u03B8", "j", "w", "d", "a", "d"}));
    CHECK(ciudad.back().name == "d");
    // A written acute accent overrides the default stress position; the
    // tokeniser is UTF-8 aware, including accented capitals.
    {
        const auto cafe = phonemize("caf\u00E9", Language::Spanish);
        CHECK(names(cafe) == std::vector<std::string>({"k", "a", "f", "e"}));
        CHECK(cafe.back().amplitude > 1.0);
        const auto angela = phonemize("\u00C1NGELA", Language::Spanish);
        CHECK(names(angela) ==
              std::vector<std::string>({"a", "n", "x", "e", "l", "a"}));
        CHECK(angela.front().amplitude > 1.0);
    }
    // gü keeps its u as a glide, and the i after it carries the stress.
    {
        const auto pinguino = phonemize("ping\u00FCino", Language::Spanish);
        CHECK(names(pinguino) == std::vector<std::string>(
                                     {"p", "i", "n", "g", "w", "i", "n", "o"}));
        CHECK(pinguino[5].amplitude > 1.0);
    }
    // Inverted punctuation separates words, and a closing question mark still
    // carries the utterance-final rise.
    {
        const auto question =
            phonemize("\u00BFQu\u00E9 tal?", Language::Spanish);
        CHECK(question.back().name == "?");
        CHECK(question.back().pitchRise);
        CHECK(contains(names(question), "k"));
        CHECK(contains(names(question), "t"));
        CHECK(contains(names(question), "a"));
    }
    // Digits are read in Spanish, one word at a time.
    CHECK(names(phonemize("123", Language::Spanish)) ==
          std::vector<std::string>(
              {"u", "n", "o", "d", "o", "s", "t", "\u027E", "e", "s"}));
    // The language-aware convenience overload uses the shared synthesis path.
    CHECK(peak(synthesizeText("hola", Language::Spanish, voicePreset("neutral"),
                              8000)) > 0.2);
    // A Spanish word and its IPA transcription produce identical PCM: the
    // language module is only a front end, not a second synthesis path.
    CHECK(Synthesizer(voicePreset("neutral"))
              .render(phonemize("hola", Language::Spanish), 8000) ==
          Synthesizer(voicePreset("neutral"))
              .render(parseIpa("\u02C8ola"), 8000));

    // User-defined language files turn romanisation, a lexicon and phonetic
    // realization into data. No C++ is needed to add a conlang.
    const LanguageDefinition velta = parseLanguageDefinition(
        "# Velta test definition\n"
        "name Velta\n"
        "tag x-velta\n"
        "phoneme a duration-scale=1.25 formant-scale=1.05\n"
        "phoneme \u0283 duration-scale=1.10\n"
        "grapheme sh \u0283\n"
        "grapheme s s\n"
        "grapheme h\n"
        "grapheme a a\n"
        "word sh a\n"
        "word velta \u02C8velta\n");
    CHECK(velta.name == "Velta");
    CHECK(velta.tag == "x-velta");
    // Longest grapheme wins: sh is one phoneme, not s + silent h.
    CHECK(names(phonemize("sha", velta)) ==
          std::vector<std::string>({"\u0283", "a"}));
    // A whole-word entry beats the grapheme table, and lookup is ASCII
    // case-insensitive.
    CHECK(names(phonemize("sh", velta)) == std::vector<std::string>({"a"}));
    CHECK(names(phonemize("SH", velta)) == std::vector<std::string>({"a"}));
    // An empty IPA mapping makes a grapheme silent.
    CHECK(names(phonemize("ha", velta)) == std::vector<std::string>({"a"}));
    // Realization overrides apply after the universal IPA mapping.
    {
        const auto plain = parseIpa("a").front();
        const auto tuned = phonemize("a", velta).front();
        CHECK(tuned.duration > plain.duration);
        CHECK(std::abs(tuned.bands[0].frequency -
                       plain.bands[0].frequency * 1.05) < 1e-9);
    }
    // Punctuation and the utterance-final rise still work through the shared
    // phoneme sequence.
    {
        const auto phrase = phonemize("sh, a!", velta);
        CHECK(contains(names(phrase), ","));
        CHECK(phrase.back().name == "!");
        CHECK(!phrase.back().pitchRise);
    }
    // Strict mode rejects undeclared phonemes; lenient mode skips an unknown
    // grapheme instead of failing.
    {
        const LanguageDefinition strict =
            parseLanguageDefinition("name Strict\n"
                                    "strict\n"
                                    "phoneme a\n"
                                    "grapheme a a\n"
                                    "grapheme b b\n");
        bool rejectedStrict = false;
        try {
            (void)phonemize("ab", strict);
        } catch (const std::invalid_argument &) {
            rejectedStrict = true;
        }
        CHECK(rejectedStrict);
        CHECK(names(phonemize("a", strict)) == std::vector<std::string>({"a"}));

        const LanguageDefinition lenient =
            parseLanguageDefinition("name Lenient\n"
                                    "grapheme a a\n");
        CHECK(names(phonemize("ab", lenient)) ==
              std::vector<std::string>({"a"}));
    }
    // Malformed files fail with line-aware errors rather than silently
    // producing the wrong language.
    {
        const auto rejects = [](std::string_view text) {
            try {
                (void)parseLanguageDefinition(text);
            } catch (const std::invalid_argument &) {
                return true;
            }
            return false;
        };
        CHECK(rejects("grapheme a a\n"));
        CHECK(rejects("name X\nunknown y\n"));
        CHECK(rejects("name X\nphoneme a duration-scale=zero\n"));
        CHECK(rejects("name X\ngrapheme\n"));
        CHECK(rejects("name X\nphoneme-rule a > b / V\n"));
        CHECK(rejects("name X\nphoneme-rule > b\n"));
        CHECK(rejects("name X\nphoneme-rule a >\n"));
        CHECK(rejects("name X\nclass V\n"));
        CHECK(rejects("name X\nphoneme-rule # > a\n"));
    }

    // Ordered grapheme rules rewrite romanisation before longest-match lookup.
    const LanguageDefinition orthography =
        parseLanguageDefinition("name Orthography\n"
                                "grapheme a a\n"
                                "grapheme e e\n"
                                "grapheme k k\n"
                                "grapheme s s\n"
                                "grapheme-rule c > s / _ e\n"
                                "grapheme-rule c > s / _ i\n"
                                "grapheme-rule c > k\n");
    std::vector<std::string> trace;
    CHECK(names(phonemize("ceca", orthography, &trace)) ==
          std::vector<std::string>({"s", "e", "k", "a"}));
    // The first rule wins at the first c; the third handles the second c, and
    // the trace reports both applications with stable rule numbers.
    CHECK(trace.size() == 2);
    CHECK(trace[0].find("grapheme-rule 1") != std::string::npos);
    CHECK(trace[0].find("c > s / _ e") != std::string::npos);
    CHECK(trace[1].find("grapheme-rule 3") != std::string::npos);

    // Phoneme rules run after mapping and fire only in their environments.
    const LanguageDefinition allophony =
        parseLanguageDefinition("name Allophony\n"
                                "strict\n"
                                "class V a\n"
                                "phoneme a\n"
                                "phoneme t\n"
                                "phoneme \u027E\n"
                                "grapheme a a\n"
                                "grapheme t t\n"
                                "phoneme-rule t > \u027E / V _ V\n");
    CHECK(names(phonemize("tata", allophony)) ==
          std::vector<std::string>({"t", "a", "\u027E", "a"}));
    // Rule-derived phonemes still render through the one deterministic PCM
    // path; the same input produces byte-identical audio.
    CHECK(Synthesizer(voicePreset("neutral"))
              .render(phonemize("tata", allophony), 8000) ==
          Synthesizer(voicePreset("neutral"))
              .render(phonemize("tata", allophony), 8000));

    // Rules are ordered: the first matching rule at a position wins.
    const LanguageDefinition ordered =
        parseLanguageDefinition("name Ordered\n"
                                "grapheme a a\n"
                                "phoneme-rule a > e\n"
                                "phoneme-rule a > o\n");
    CHECK(names(phonemize("a", ordered)) == std::vector<std::string>({"e"}));

    // '#' anchors environments to word edges; 0 deletes a match.
    const LanguageDefinition boundaries =
        parseLanguageDefinition("name Boundaries\n"
                                "class C n\n"
                                "grapheme a a\n"
                                "grapheme n n\n"
                                "grapheme ' \u0294\n"
                                "phoneme-rule a > e / # _\n"
                                "phoneme-rule \u0294 > 0 / C _ #\n");
    CHECK(names(phonemize("a", boundaries)) == std::vector<std::string>({"e"}));
    CHECK(names(phonemize("n'", boundaries)) ==
          std::vector<std::string>({"n"}));

    // A language file is deterministic and feeds the shared renderer.
    CHECK(Synthesizer(voicePreset("neutral"))
              .render(phonemize("velta", velta), 8000) ==
          Synthesizer(voicePreset("neutral"))
              .render(phonemize("velta", velta), 8000));

    // User pronunciation lexicons layer exact words and context-sensitive
    // homograph rules ahead of the selected language front end.
    {
        const PronunciationLexicon lex =
            parsePronunciationLexicon("# a comment\n"
                                      "word Ewan juːən\n"
                                      "word ZEELAND ˈziːlənt\n"
                                      "homograph lead led after=us\n"
                                      "homograph live laɪv after=the\n");
        CHECK(lex.words.size() == 2);
        CHECK(lex.homographs.size() == 2);
        // Exact entries are case-insensitive and parsed through universal IPA.
        CHECK(names(phonemize("Ewan", Language::English, lex)) ==
              names(parseIpa("juːən")));
        CHECK(names(phonemize("EWAN", Language::English, lex)) ==
              names(parseIpa("juːən")));
        // Homograph context is ignored when it does not match, so the word
        // falls back to the English front end.
        CHECK(
            startsWithIpa(phonemize("lead us", Language::English, lex), "led"));
        CHECK(names(phonemize("the lead", Language::English, lex)) ==
              names(phonemize("the lead", Language::English)));
        CHECK(startsWithIpa(phonemize("live the", Language::English, lex),
                            "laɪv"));
        // Punctuation survives the layer, and unmatched words use the front
        // end.
        const auto phrase =
            phonemize("Ewan, visited Zeeland!", Language::English, lex);
        CHECK(contains(names(phrase), ","));
        CHECK(phrase.back().name == "!");
        CHECK(contains(names(phrase), "v"));
        // Determinism: identical text and lexicon render byte-identically.
        CHECK(Synthesizer(voicePreset("neutral"))
                  .render(phonemize("Ewan lead us", Language::English, lex),
                          8000) ==
              Synthesizer(voicePreset("neutral"))
                  .render(phonemize("Ewan lead us", Language::English, lex),
                          8000));
        // Validation trace reports the source layer per word.
        std::vector<std::string> trace;
        const auto traced =
            phonemize("Ewan lead us", Language::English, lex, &trace);
        CHECK(traced.size() > 0);
        CHECK(trace.size() == 3);
        CHECK(trace[0] == "1:ewan: lexicon");
        CHECK(trace[1] == "2:lead: homograph");
        CHECK(trace[2] == "3:us: fallback");
    }
    {
        // A homograph rule beats an exact entry for the same spelling.
        const PronunciationLexicon lex =
            parsePronunciationLexicon("word read ɹɛd\n"
                                      "homograph read ɹiːd after=a\n");
        CHECK(
            startsWithIpa(phonemize("read a", Language::English, lex), "ɹiːd"));
        CHECK(
            startsWithIpa(phonemize("read it", Language::English, lex), "ɹɛd"));
    }
    {
        // Malformed lexicons report the offending line number.
        const std::pair<std::string, std::string> bad[] = {
            {"bogus x\n", "lexicon line 1"},
            {"word bad not-a-symbol\n", "lexicon line 1"},
            {"\n\nword\n", "lexicon line 3"},
            {"homograph read ɹɛd bogus=x\n", "lexicon line 1"},
            {"homograph read ɹɛd before=\n", "lexicon line 1"},
        };
        for (const auto &item : bad) {
            bool rejected = false;
            std::string message;
            try {
                (void)parsePronunciationLexicon(item.first);
            } catch (const std::invalid_argument &error) {
                rejected = true;
                message = error.what();
            }
            CHECK(rejected);
            CHECK(message.find(item.second) == 0);
        }
    }
    {
        // Lexicons compose with language definitions: an exact user entry
        // overrides the definition's word table, while unmatched words still
        // go through its grapheme rules.
        const PronunciationLexicon lex =
            parsePronunciationLexicon("word velta ˈanar\n");
        CHECK(names(phonemize("velta", velta, lex)) ==
              names(parseIpa("ˈanar")));
        CHECK(names(phonemize("sha", velta, lex)) ==
              names(phonemize("sha", velta)));
    }

    // Exact timing: the plan uses the same cumulative scheduler as render().
    {
        const Voice voice = voicePreset("neutral");
        const std::vector<Phoneme> phrase = parsePhonemes("[h ee l o]");
        const TimingPlan plan = planTiming(phrase, voice, 22050);
        const std::vector<float> samples =
            Synthesizer(voice).render(phrase, 22050);
        CHECK(plan.sampleRate == 22050);
        CHECK(plan.totalSamples == static_cast<int>(samples.size()));
        CHECK(plan.phonemes.size() == phrase.size());
        CHECK(plan.duration ==
              static_cast<double>(plan.totalSamples) / 22050.0);

        double cumulative = 0.0;
        int previous = 0;
        for (size_t i = 0; i < phrase.size(); ++i) {
            cumulative += phrase[i].duration;
            const int expected = static_cast<int>(
                std::llround(cumulative * 22050.0 / voice.rate));
            CHECK(plan.phonemes[i].startSample == previous);
            CHECK(plan.phonemes[i].endSample == std::max(expected, previous));
            CHECK(plan.phonemes[i].startTime ==
                  static_cast<double>(plan.phonemes[i].startSample) / 22050.0);
            CHECK(plan.phonemes[i].endTime >= plan.phonemes[i].startTime);
            previous = plan.phonemes[i].endSample;
        }
        CHECK(previous == plan.totalSamples);

        // Text labels are attached to words, and gaps/pauses extend a word to
        // the next spoken word rather than appearing as extra units.
        const std::string text = "Hello, world.";
        const std::vector<Phoneme> spoken = phonemize(text);
        const TimingPlan words =
            planTiming(spoken, voice, 22050, splitWordsForTiming(text));
        CHECK(words.words.size() == 2);
        CHECK(words.words[0].label == "Hello");
        CHECK(words.words[1].label == "world");
        CHECK(words.words[0].startSample == 0);
        CHECK(words.words[0].endSample == words.words[1].startSample);
        CHECK(words.words[1].endSample == words.totalSamples);
        for (size_t i = 0; i < words.words.size(); ++i) {
            CHECK(words.words[i].startSample <= words.words[i].endSample);
            CHECK(words.words[i].endTime >= words.words[i].startTime);
            if (i > 0) {
                CHECK(words.words[i].startSample >=
                      words.words[i - 1].startSample);
            }
        }

        // If caller labels do not describe every word, deterministic phoneme
        // labels are used instead of silently misaligning captions.
        const TimingPlan derived = planTiming(parsePhonemes("[a ee]"), voice,
                                              8000, {"wrong", "extra"});
        CHECK(derived.words.size() == 1);
        CHECK(derived.words[0].label == "a ee");

        // The fast-rate contract and slow-rate contract still hold exactly.
        Voice fast = voice;
        fast.rate = 1e6;
        const TimingPlan zero = planTiming(phrase, fast, 22050);
        CHECK(zero.totalSamples == 0);
        CHECK(zero.duration == 0.0);
        CHECK(Synthesizer(fast).render(phrase, 22050).empty());
        Voice slow = voice;
        slow.rate = 0.05;
        const TimingPlan longer = planTiming(phrase, slow, 8000);
        double phraseDuration = 0.0;
        for (const Phoneme &phoneme : phrase) {
            phraseDuration += phoneme.duration;
        }
        CHECK(longer.totalSamples == static_cast<int>(std::llround(
                                         phraseDuration * 8000.0 / slow.rate)));
        CHECK(longer.totalSamples ==
              static_cast<int>(Synthesizer(slow).render(phrase, 8000).size()));

        bool rejected = false;
        try {
            (void)planTiming(phrase, voice, 7999);
        } catch (const std::invalid_argument &) {
            rejected = true;
        }
        CHECK(rejected);
        Voice invalidRate = voice;
        invalidRate.rate = 0.0;
        rejected = false;
        try {
            (void)planTiming(phrase, invalidRate, 22050);
        } catch (const std::invalid_argument &) {
            rejected = true;
        }
        CHECK(rejected);
    }

    // Word splitting and viseme mapping are deterministic data transforms.
    {
        CHECK(splitWordsForTiming("Hello, world.") ==
              std::vector<std::string>({"Hello", "world"}));
        CHECK(splitWordsForTiming("don't stop") ==
              std::vector<std::string>({"don't", "stop"}));
        CHECK(splitWordsForTiming("Hola, señor.") ==
              std::vector<std::string>({"Hola", "señor"}));
        bool rejected = false;
        try {
            (void)splitWordsForTiming(std::string("\xff", 1));
        } catch (const std::invalid_argument &) {
            rejected = true;
        }
        CHECK(rejected);

        const Voice voice = voicePreset("neutral");
        const TimingPlan plan =
            planTiming(parsePhonemes("[a m s .]"), voice, 8000);
        const std::vector<VisemeFrame> visemes = planVisemes(plan);
        CHECK(visemes.size() == 4);
        CHECK(visemes[0].shape == "open");
        CHECK(visemes[0].openness > 0.5);
        CHECK(visemes[1].shape == "closed");
        CHECK(visemes[2].shape == "alveolar");
        CHECK(visemes[3].shape == "rest");
        for (size_t i = 0; i < visemes.size(); ++i) {
            CHECK(visemes[i].phoneme == plan.phonemes[i].label);
            CHECK(visemes[i].startSample == plan.phonemes[i].startSample);
            CHECK(visemes[i].endSample == plan.phonemes[i].endSample);
        }
        CHECK(planVisemes(plan) == visemes);

        // Unknown names still fall back to a stable shape from the phoneme
        // kind, so new inventory entries never break a lip-sync track.
        Phoneme custom = *findPhoneme("a");
        custom.name = "future";
        const TimingPlan unknown = planTiming({custom}, voice, 8000);
        CHECK(planVisemes(unknown)[0].shape == "open");
    }

    // Caption and JSON writers have fixed layouts and are byte-identical for
    // equal plans.
    {
        const Voice voice = voicePreset("neutral");
        const TimingPlan plan = planTiming(phonemize("Hello, world."), voice,
                                           22050, {"Hello", "world"});
        const std::vector<VisemeFrame> visemes = planVisemes(plan);
        const std::string json = timingJson(plan, visemes);
        CHECK(json == timingJson(plan, visemes));
        CHECK(json.find("\"format\": \"formant-timing\"") != std::string::npos);
        CHECK(json.find("\"label\": \"Hello\"") != std::string::npos);
        CHECK(json.find("\"startSample\": 0") != std::string::npos);
        CHECK(json.find("\"shape\": \"") != std::string::npos);
        CHECK(timingVisemesJson(plan, visemes)
                  .find("\"format\": \"formant-visemes\"") !=
              std::string::npos);

        const std::string escapedPlan =
            timingJson(planTiming(parsePhonemes("[a]"), voice, 8000,
                                  {std::string("say \"hi\"\n")}),
                       {});
        CHECK(escapedPlan.find("say \\\"hi\\\"\\n") != std::string::npos);

        const std::string srt = timingSrt(plan);
        CHECK(srt.find("1\n00:00:00,000 --> ") == 0);
        CHECK(srt.find("\nHello\n\n2\n") != std::string::npos);
        CHECK(timingVtt(plan).find("WEBVTT\n\n00:00:00.000 --> ") == 0);
        const std::string lrc = timingLrc(plan);
        CHECK(lrc.find("[ar:formant]\n") == 0);
        CHECK(lrc.find("[00:00.") != std::string::npos);
        CHECK(lrc.find("<00:00.") != std::string::npos);
        CHECK(lrc.find(">Hello ") != std::string::npos);

        const TimingPlan empty = planTiming({}, voice, 8000);
        CHECK(empty.totalSamples == 0);
        CHECK(empty.words.empty());
        CHECK(timingJson(empty, {}).find("\"words\": [\n  ],") !=
              std::string::npos);
        CHECK(timingSrt(empty).empty());
        CHECK(timingVtt(empty) == "WEBVTT\n\n");
        CHECK(timingLrc(empty).find("<") == std::string::npos);
    }

    // The English paths remain byte-for-byte unchanged.
    CHECK(names(phonemize("Hello, world.")) == hello);

    if (failures == 0) {
        std::cout << "All formant tests passed\n";
    }
    return failures == 0 ? 0 : 1;
}
