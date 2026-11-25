#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
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
#include "formant/frication.h"
#include "formant/nvv.h"
#include "../src/prosody.h"

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

// Mean of the voiced frames of an F0 track, used to compare a transposed
// render against its source without depending on a single frame's estimate.
double meanFrequency(const formant::F0Track &track) {
    double sum = 0.0;
    int count = 0;
    for (const formant::F0Frame &frame : track) {
        if (frame.frequency > 0.0) {
            sum += frame.frequency;
            ++count;
        }
    }
    return count > 0 ? sum / static_cast<double>(count) : 0.0;
}

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

std::vector<formant::Phoneme> twinklePhonemes() {
    return formant::phonemizeSong(
        formant::parseSongFile(formant::builtinSongData("twinkle")),
        formant::parseSongScale(formant::builtinScaleData("chromatic")));
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

    // Issue #110: a chain of continued renders must be byte-identical to one
    // render of the concatenation. The state is a pure value: it carries the
    // accumulated phonemes, the exact glottal phase, the absolute sine-noise
    // time base, the combined length and the utterance-final rise flag.
    {
        const int rate = 22050;
        Synthesizer synth(voice);
        const size_t split = vowels.size() / 2;
        const std::vector<Phoneme> head(vowels.begin(), vowels.begin() + split);
        const std::vector<Phoneme> tail(vowels.begin() + split, vowels.end());
        CHECK(!head.empty() && !tail.empty());

        const std::vector<float> single = synth.render(vowels, rate);
        const RenderResult first = synth.renderContinued(head, rate, {});
        const RenderResult chain =
            synth.renderContinued(tail, rate, first.state);
        const RenderResult whole = synth.renderContinued(vowels, rate, {});

        CHECK(first.samples == synth.render(head, rate));
        CHECK(chain.samples == single);
        CHECK(whole.samples == single);
        CHECK(chain.samples.size() == single.size());

        // State carry is exact: chain and whole agree field for field.
        CHECK(chain.state.phonemes == whole.state.phonemes);
        CHECK(chain.state.sampleRate == rate);
        CHECK(chain.state.sampleRate == whole.state.sampleRate);
        CHECK(chain.state.totalSamples == whole.state.totalSamples);
        CHECK(chain.state.totalSamples == static_cast<int>(single.size()));
        CHECK(chain.state.noiseTimeBase == whole.state.noiseTimeBase);
        CHECK(chain.state.noiseTimeBase ==
              static_cast<double>(single.size()) / rate);
        CHECK(chain.state.phase == whole.state.phase);
        CHECK(chain.state.phase >= 0.0 &&
              chain.state.phase < 2.0 * detail::kPi);
        CHECK(chain.state.rising == whole.state.rising);
        CHECK(chain.state.totalSamples > first.state.totalSamples);

        // An empty append is a no-op that still returns the whole chain.
        const RenderResult idle = synth.renderContinued({}, rate, chain.state);
        CHECK(idle.samples == single);
        CHECK(idle.state.phonemes == chain.state.phonemes);
        CHECK(idle.state.phase == chain.state.phase);

        // A mismatched rate is rejected rather than silently re-basing time.
        bool wrongRate = false;
        try {
            (void)synth.renderContinued(tail, 44100, first.state);
        } catch (const std::invalid_argument &) {
            wrongRate = true;
        }
        CHECK(wrongRate);

        // The splice itself is smooth: the largest adjacent-sample change in
        // a small window around the joint stays within a 0.05 bound, the same
        // order as the 108 taper bound. The byte-identity check above proves
        // the joint is treated exactly like an interior phoneme boundary.
        const size_t joint = static_cast<size_t>(first.state.totalSamples);
        CHECK(joint > 3 && joint + 3 < chain.samples.size());
        double spliceDelta = 0.0;
        for (size_t n = joint - 3; n <= joint + 3; ++n) {
            spliceDelta =
                std::max(spliceDelta,
                         std::abs(static_cast<double>(chain.samples[n]) -
                                  static_cast<double>(chain.samples[n - 1])));
        }
        CHECK(spliceDelta <= 0.05);
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
    // #112: nearest-feature inventory fallback and typology defaults.
    {
        const LanguageDefinition fallback =
            parseLanguageDefinition("name Fallback\n"
                                    "lenient\n"
                                    "tone simple\n"
                                    "stress fixed\n"
                                    "rhythm syllable\n"
                                    "rate 0.5\n"
                                    "phoneme a\n"
                                    "phoneme s\n"
                                    "grapheme a a\n"
                                    "grapheme z z\n");
        CHECK(fallback.typology.tone == "simple");
        CHECK(fallback.typology.stress == "fixed");
        CHECK(fallback.typology.rhythm == "syllable");
        CHECK(fallback.typology.rate.has_value());
        CHECK(std::abs(*fallback.typology.rate - 0.5) < 1e-12);

        std::vector<std::string> warnings;
        std::vector<std::string> again;
        const auto first = phonemize("z a", fallback, nullptr, &warnings);
        const auto second = phonemize("z a", fallback, nullptr, &again);
        CHECK(warnings == again);
        CHECK(warnings.size() == 1);
        CHECK(warnings.front().find("\"z\"") != std::string::npos);
        CHECK(warnings.front().find("\"s\"") != std::string::npos);
        CHECK(first.size() == second.size());
        CHECK(names(first) == names(second));
        CHECK(names(first)[0] == "s");
        // rate scales every segment duration; stress=fixed lifts the first
        // vowel's pitch onset and amplitude.
        const Phoneme plainA = parseIpa("a").front();
        CHECK(std::abs(first.back().duration - plainA.duration * 0.5) < 1e-9);
        CHECK(first.back().pitchStart > 1.0);
        CHECK(first.back().amplitude > 1.0);

        const LanguageDefinition strict =
            parseLanguageDefinition("name Strict\n"
                                    "strict\n"
                                    "phoneme a\n"
                                    "phoneme s\n"
                                    "grapheme a a\n"
                                    "grapheme z z\n");
        bool suggestion = false;
        try {
            (void)phonemize("z", strict);
        } catch (const std::invalid_argument &error) {
            suggestion = std::string(error.what())
                             .find("nearest declared phoneme: \"s\"") !=
                         std::string::npos;
        }
        CHECK(suggestion);
    }
    {
        const auto rejects = [](std::string_view text) {
            try {
                (void)parseLanguageDefinition(text);
            } catch (const std::invalid_argument &) {
                return true;
            }
            return false;
        };
        CHECK(rejects("name X\ntone wild\n"));
        CHECK(rejects("name X\nstress loud\n"));
        CHECK(rejects("name X\nrhythm free\n"));
        CHECK(rejects("name X\nrate 0\n"));
        CHECK(rejects("name X\nrate nope\n"));
        CHECK(rejects("name X\ntone\n"));
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

    // Objective-metric analyzers are pure transforms of their inputs.
    {
        constexpr double pi = 3.14159265358979323846;
        constexpr int rate = 44100;
        std::vector<float> sine(static_cast<size_t>(rate / 2));
        for (size_t i = 0; i < sine.size(); ++i) {
            sine[i] = static_cast<float>(
                0.8 * std::sin(2.0 * pi * 100.0 * static_cast<double>(i) /
                               static_cast<double>(rate)));
        }

        formant::AnalyzerOptions options;
        const formant::F0Track estimated =
            formant::estimateF0(sine, rate, options);
        CHECK(!estimated.empty());
        double frequencySum = 0.0;
        size_t voicedFrames = 0;
        for (const formant::F0Frame &frame : estimated) {
            if (frame.frequency > 0.0) {
                frequencySum += frame.frequency;
                ++voicedFrames;
            }
        }
        CHECK(voicedFrames > 0);
        if (voicedFrames > 0) {
            CHECK(std::abs(frequencySum / static_cast<double>(voicedFrames) -
                           100.0) < 2.0);
        }
        CHECK(formant::estimateF0(sine, rate, options).size() ==
              estimated.size());

        const formant::F0Metrics identical =
            formant::compareF0(estimated, estimated);
        CHECK(identical.frames == voicedFrames);
        CHECK(identical.rmseHz < 1e-9);
        CHECK(identical.correlation > 0.999999);

        formant::F0Track shifted = estimated;
        for (formant::F0Frame &frame : shifted) {
            frame.frequency *= 1.1;
        }
        const formant::F0Metrics changed =
            formant::compareF0(estimated, shifted);
        CHECK(changed.frames == voicedFrames);
        CHECK(changed.rmseHz > 5.0);

        // A shaped contour exercises Pearson's r; the pure-tone track above is
        // constant, for which the metrics deliberately report no shape.
        const formant::F0Track shape = {
            {0, 100.0, 1.0}, {1, 110.0, 1.0}, {2, 105.0, 1.0}, {3, 125.0, 1.0}};
        formant::F0Track noisy = shape;
        noisy[1].frequency += 4.0;
        noisy[2].frequency -= 3.0;
        const formant::F0Metrics shaped = formant::compareF0(shape, noisy);
        CHECK(shaped.frames == shape.size());
        CHECK(shaped.rmseHz > 1.0);
        CHECK(shaped.correlation > 0.8);

        const formant::EnvelopeTrack envelope =
            formant::estimateEnvelope(sine, rate, options);
        CHECK(!envelope.empty());
        CHECK(!envelope.front().logPower.empty());
        CHECK(envelope.front().cepstra.size() == 14);
        const formant::EnvelopeMetrics sameEnvelope =
            formant::compareEnvelope(envelope, envelope);
        CHECK(sameEnvelope.frames == envelope.size());
        CHECK(sameEnvelope.logDistance < 1e-9);
        CHECK(sameEnvelope.mcdDb < 1e-9);

        formant::EnvelopeTrack louder = envelope;
        for (formant::EnvelopeFrame &frame : louder) {
            for (double &power : frame.logPower) {
                power += 2.0;
            }
            if (!frame.cepstra.empty()) {
                frame.cepstra[0] +=
                    2.0 * std::sqrt(static_cast<double>(frame.logPower.size()));
            }
        }
        const formant::EnvelopeMetrics gainOnly =
            formant::compareEnvelope(envelope, louder);
        CHECK(gainOnly.frames == envelope.size());
        CHECK(gainOnly.logDistance < 1e-9);
        CHECK(gainOnly.mcdDb < 1e-9);

        bool threw = false;
        formant::AnalyzerOptions bad = options;
        bad.maxF0 = bad.minF0;
        try {
            (void)formant::estimateF0(sine, rate, bad);
        } catch (const std::invalid_argument &) {
            threw = true;
        }
        CHECK(threw);

        threw = false;
        bad = options;
        bad.melBands = 1;
        try {
            (void)formant::estimateEnvelope(sine, rate, bad);
        } catch (const std::invalid_argument &) {
            threw = true;
        }
        CHECK(threw);

        threw = false;
        bad = options;
        bad.maxHarmonic = 1;
        try {
            (void)formant::expectedEnvelope({}, formant::Voice(), rate, bad);
        } catch (const std::invalid_argument &) {
            threw = true;
        }
        CHECK(threw);

        const formant::Phoneme *source = formant::findPhoneme("ee");
        CHECK(source != nullptr);
        if (source != nullptr) {
            formant::Phoneme vowel = *source;
            vowel.duration = 0.3;
            formant::Voice voice;
            voice.pitch = 100.0;
            voice.harmonics = 100;
            voice.vibratoRate = 0.0;
            voice.vibratoDepth = 0.0;
            voice.breathiness = 0.0;
            voice.declination = 0.0;
            const std::vector<float> rendered =
                formant::Synthesizer(voice).render({vowel}, rate);
            const formant::F0Track target =
                formant::expectedF0({vowel}, voice, rate, options);
            CHECK(target.size() ==
                  formant::estimateF0(rendered, rate, options).size());
            const formant::F0Metrics againstTarget = formant::compareF0(
                formant::estimateF0(rendered, rate, options), target);
            CHECK(againstTarget.frames > 0);
            CHECK(againstTarget.rmseHz < 5.0);
            CHECK(formant::expectedEnvelope({vowel}, voice, rate, options)
                      .size() ==
                  formant::estimateEnvelope(rendered, rate, options).size());
        }
    }

    // Issue #108: a raised-cosine fade removes partials before the hard
    // Nyquist break, so a gliding F0 cannot pop them. The window is exactly
    // unity below the band and reaches zero at the ceiling.
    {
        const int rate = 44100;
        const double ceiling = detail::kHarmonicCeiling * rate;
        const double start = ceiling * (1.0 - detail::kCeilingTaper);
        CHECK(detail::ceilingGain(1000.0, rate) == 1.0);
        CHECK(detail::ceilingGain(start, rate) == 1.0);
        CHECK(detail::ceilingGain(ceiling, rate) == 0.0);
        double previous = 2.0;
        for (int i = 0; i <= 64; ++i) {
            const double f = start + (ceiling - start) * i / 64.0;
            const double g = detail::ceilingGain(f, rate);
            CHECK(g >= 0.0 && g <= 1.0 && g <= previous);
            previous = g;
        }
        CHECK(detail::capGain(1) == 1.0);
        CHECK(detail::capGain(detail::kMaxHarmonics) == 0.0);

        // A sustained vowel with a strong F4 just below the ceiling and a
        // rising F0. The same voice rendered without the fade keeps full
        // amplitude until the hard break, so it retains more high-band
        // energy in the second half of the glide.
        Phoneme glide;
        glide.name = "ceiling-glide";
        glide.kind = PhonemeKind::Voiced;
        glide.duration = 1.0;
        glide.voiced = true;
        glide.pitchStart = 1.0;
        glide.pitchEnd = 1.2;
        glide.bands[3] = FormantBand{ceiling, 2.0, 50.0};

        Voice glideVoice;
        glideVoice.pitch = 95.0;
        glideVoice.harmonics = 240;
        glideVoice.declination = 0.0;
        glideVoice.vibratoRate = 0.0;
        glideVoice.vibratoDepth = 0.0;
        glideVoice.breathiness = 0.0;
        glideVoice.attack = 0.002;
        glideVoice.release = 0.002;
        glideVoice.gain = 1.0;

        glideVoice.taperCeiling = true;
        const std::vector<float> faded =
            Synthesizer(glideVoice).render({glide}, rate);
        glideVoice.taperCeiling = false;
        const std::vector<float> abrupt =
            Synthesizer(glideVoice).render({glide}, rate);
        CHECK(faded.size() == static_cast<size_t>(rate));
        CHECK(faded == faded);
        CHECK(faded != abrupt);

        double fadedEnergy = 0.0;
        double abruptEnergy = 0.0;
        for (size_t n = static_cast<size_t>(rate) / 2; n < faded.size(); ++n) {
            fadedEnergy += static_cast<double>(faded[n]) * faded[n];
            abruptEnergy += static_cast<double>(abrupt[n]) * abrupt[n];
        }
        CHECK(fadedEnergy < 0.8 * abruptEnergy);

        // Per-sample delta of an eight-sample high-frequency envelope stays
        // within twice the mean envelope level with the fade active. The
        // untapered render breaks that bound when the partial is removed.
        auto envelope = [](const std::vector<float> &signal) {
            constexpr int width = 8;
            std::vector<double> magnitude(signal.size(), 0.0);
            for (size_t n = 1; n < signal.size(); ++n) {
                magnitude[n] = std::abs(static_cast<double>(signal[n]) -
                                        static_cast<double>(signal[n - 1]));
            }
            double maxDelta = 0.0;
            double mean = 0.0;
            size_t count = 0;
            for (size_t n = width; n + width < signal.size(); ++n) {
                double before = 0.0;
                double after = 0.0;
                for (int k = 0; k < width; ++k) {
                    before += magnitude[n + k];
                    after += magnitude[n + k + 1];
                }
                before /= width;
                after /= width;
                if (n > signal.size() / 20 && n < signal.size() * 19 / 20) {
                    maxDelta = std::max(maxDelta, std::abs(before - after));
                    mean += before;
                    ++count;
                }
            }
            return std::pair<double, double>{maxDelta, mean / count};
        };
        const auto fadedEnvelope = envelope(faded);
        const auto abruptEnvelope = envelope(abrupt);
        const double bound = 2.0 * fadedEnvelope.second;
        CHECK(fadedEnvelope.first <= bound);
        CHECK(abruptEnvelope.first > bound);
    }

    // Issue #133: transposition happens in parameter space, before any sine
    // is generated. The helpers are pure, so identical arguments render
    // byte-identically and no post-processing stage is involved.
    {
        CHECK(centsToRatio(0.0) == 1.0);
        CHECK(std::abs(centsToRatio(1200.0) - 2.0) < 1e-12);
        CHECK(std::abs(centsToRatio(-1200.0) - 0.5) < 1e-12);

        Voice base = voicePreset("robot");
        base.pitch = 100.0;
        base.harmonics = 200;
        base.declination = 0.0;
        const Voice octave = shiftPitch(base, 1200.0);
        CHECK(std::abs(octave.pitch - 200.0) < 1e-9);
        CHECK(octave.formantScale == base.formantScale);
        const Voice wider = scaleFormants(base, 1.25);
        CHECK(wider.pitch == base.pitch);
        CHECK(std::abs(wider.formantScale - 1.25) < 1e-12);
        const Voice longer = withVocalTractLength(base, 1.25);
        CHECK(std::abs(longer.formantScale - 0.8) < 1e-12);
        const Voice both = transposeVoice(base, 700.0, 1.1);
        CHECK(std::abs(both.pitch - shiftPitch(base, 700.0).pitch) < 1e-9);
        CHECK(std::abs(both.formantScale - 1.1) < 1e-12);

        const std::vector<Voice> choir = detuneVoices(base, {-12.0, 0.0, 12.0});
        CHECK(choir.size() == 3);
        CHECK(choir[0].pitch < choir[1].pitch);
        CHECK(choir[1].pitch < choir[2].pitch);
        for (const Voice &v : choir) {
            CHECK(v.formantScale == base.formantScale);
        }

        bool rejected = false;
        try {
            (void)shiftPitch(base, std::numeric_limits<double>::infinity());
        } catch (const std::invalid_argument &) {
            rejected = true;
        }
        CHECK(rejected);
        rejected = false;
        try {
            (void)scaleFormants(base, 0.0);
        } catch (const std::invalid_argument &) {
            rejected = true;
        }
        CHECK(rejected);
        rejected = false;
        try {
            (void)withVocalTractLength(base, -1.0);
        } catch (const std::invalid_argument &) {
            rejected = true;
        }
        CHECK(rejected);

        // A whisper has no F0 to shift, so transposition leaves it unvoiced.
        const Voice whisper = shiftPitch(voicePreset("whisper"), 1200.0);
        CHECK(whisper.pitch == 0.0);

        const formant::Phoneme *source = findPhoneme("ee");
        CHECK(source != nullptr);
        if (source != nullptr) {
            formant::Phoneme vowel = *source;
            vowel.duration = 1.0;
            AnalyzerOptions options;
            options.maxF0 = 800.0;
            const std::vector<float> plain =
                Synthesizer(base).render({vowel}, 44100);
            const std::vector<float> shifted =
                Synthesizer(octave).render({vowel}, 44100);
            CHECK(shifted == Synthesizer(octave).render({vowel}, 44100));
            CHECK(shifted != plain);

            // Choir detune renders distinct voices at small cent offsets, all
            // sharing the same formants. The zero-cent voice is an exact copy
            // of the base and must render byte-identically to it.
            const std::vector<Voice> choirVoices =
                detuneVoices(base, {-12.0, 0.0, 12.0});
            std::vector<std::vector<float>> choirRenders;
            for (const Voice &v : choirVoices) {
                choirRenders.push_back(Synthesizer(v).render({vowel}, 44100));
            }
            CHECK(choirRenders.size() == 3);
            CHECK(choirRenders[0] != choirRenders[1]);
            CHECK(choirRenders[1] != choirRenders[2]);
            CHECK(choirRenders[1] == plain);

            const double f0Plain =
                meanFrequency(estimateF0(plain, 44100, options));
            const double f0Shifted =
                meanFrequency(estimateF0(shifted, 44100, options));
            CHECK(std::abs(f0Shifted / f0Plain - 2.0) < 0.02);

            // Pitch alone does not move the resonances: both renders peak
            // near the same F1/F2 of the unchanged phoneme table.
            const std::array<double, 2> expected = {
                source->bands[0].frequency * base.formantScale,
                source->bands[1].frequency * base.formantScale};
            const auto plainPeaks = estimateFormantPeaks(
                plain, 44100, f0Plain, base.harmonics, expected);
            const auto shiftedPeaks = estimateFormantPeaks(
                shifted, 44100, f0Shifted, octave.harmonics, expected);
            // A peak lands on the nearest strong harmonic, so a doubled F0
            // can move the selected bin by a whole grid step. The absolute
            // formant-error checks below are the substantive assertion.
            const double gridTolerance = f0Shifted + 20.0;
            CHECK(std::abs(plainPeaks[0].frequency -
                           shiftedPeaks[0].frequency) < gridTolerance);
            CHECK(std::abs(plainPeaks[1].frequency -
                           shiftedPeaks[1].frequency) < gridTolerance);
            CHECK(compareFormants(plainPeaks, expected).maxErrorHz <= 150.0);
            CHECK(compareFormants(shiftedPeaks, expected).maxErrorHz <= 150.0);

            // Scaling formants moves both peaks by the requested ratio while
            // F0 stays put.
            const std::vector<float> widened =
                Synthesizer(wider).render({vowel}, 44100);
            const std::array<double, 2> widerExpected = {expected[0] * 1.25,
                                                         expected[1] * 1.25};
            const double f0Widened =
                meanFrequency(estimateF0(widened, 44100, options));
            CHECK(std::abs(f0Widened / f0Plain - 1.0) < 0.02);
            const auto widerPeaks = estimateFormantPeaks(
                widened, 44100, f0Widened, wider.harmonics, widerExpected);
            const FormantMetrics widerMetrics =
                compareFormants(widerPeaks, widerExpected);
            CHECK(widerMetrics.maxErrorHz <= 150.0);
            CHECK(widerPeaks[0].frequency > plainPeaks[0].frequency);
            CHECK(widerPeaks[1].frequency > plainPeaks[1].frequency);

            // A very large shift stays finite and normalised; the #108 taper
            // still removes partials at the ceiling as the stack moves.
            const Voice extreme = shiftPitch(base, 3600.0);
            CHECK(std::abs(extreme.pitch / base.pitch - 8.0) < 1e-9);
            const std::vector<float> extremeSamples =
                Synthesizer(extreme).render({vowel}, 44100);
            CHECK(extremeSamples ==
                  Synthesizer(extreme).render({vowel}, 44100));
            CHECK(peak(extremeSamples) <= 0.9001);
            for (float sample : extremeSamples) {
                CHECK(std::isfinite(sample));
            }
        }
    }

    // Issue #102: a minimal SSML subset, parsed into the shared Phoneme
    // sequence. Bare text and <speak> wrappers produce exactly the same
    // phonemes as the ordinary text front end.
    {
        const std::vector<Phoneme> expected = phonemize("Hello, world.");
        CHECK(names(phonemizeSsml("Hello, world.")) == names(expected));
        CHECK(names(phonemizeSsml("<speak>Hello, world.</speak>")) ==
              names(expected));
        CHECK(names(phonemizeSsml("<?xml version=\"1.0\"?>\n<!-- c -->"
                                  "<speak>Hello, world.</speak>")) ==
              names(expected));
        CHECK(names(phonemizeSsml("<speak>Hello,&#32;world.</speak>")) ==
              names(expected));
        CHECK(names(phonemizeSsml("<speak>Hello, &amp; goodbye.</speak>")) ==
              names(phonemizeSsml("<speak>Hello, & goodbye.</speak>")));

        // Explicit breaks become silence phonemes of the requested duration.
        const auto timed =
            phonemizeSsml("<speak>a<break time=\"300ms\"/>b</speak>");
        CHECK(std::any_of(timed.begin(), timed.end(), [](const Phoneme &p) {
            return p.kind == PhonemeKind::Silence &&
                   std::abs(p.duration - 0.3) < 1e-9;
        }));
        const auto strength =
            phonemizeSsml("<speak>a<break strength=\"strong\"/>b</speak>");
        CHECK(
            std::any_of(strength.begin(), strength.end(), [](const Phoneme &p) {
                return p.kind == PhonemeKind::Silence &&
                       std::abs(p.duration - 0.5) < 1e-9;
            }));

        // Inline IPA and say-as expansions flow through the same table.
        const auto inlineIpa = phonemizeSsml(
            "<speak><phoneme alphabet=\"ipa\" ph=\"ˈhəˈloʊ\"/></speak>");
        CHECK(names(inlineIpa) == names(parseIpa("ˈhəˈloʊ")));
        const auto inlineX = phonemizeSsml(
            "<speak><phoneme alphabet=\"x-sampa\" ph=\"h@loU\"/></speak>");
        CHECK(names(inlineX) == names(parseXsampa("h@loU")));
        const auto digits = phonemizeSsml(
            "<speak><say-as interpret-as=\"digits\">123</say-as></speak>");
        CHECK(names(digits) == names(phonemize("one two three")));
        const auto number = phonemizeSsml(
            "<speak><say-as interpret-as=\"number\">123</say-as></speak>");
        CHECK(names(number) == names(phonemize("one hundred twenty three")));

        // Prosody composes over nested spans: rate shortens duration, pitch
        // scales the F0 contour and volume scales amplitude.
        const auto plain = phonemize("hello");
        const auto flat = phonemizeSsml("<speak>hello</speak>");
        CHECK(names(plain) == names(flat));
        const auto fast =
            phonemizeSsml("<speak><prosody rate=\"2\">hello</prosody></speak>");
        CHECK(fast.size() == flat.size());
        for (std::size_t i = 0; i < fast.size(); ++i) {
            CHECK(std::abs(fast[i].duration * 2.0 - flat[i].duration) < 1e-9);
        }
        const auto loud = phonemizeSsml("<speak><prosody pitch=\"+12st\" "
                                        "volume=\"1.5\">a</prosody></speak>");
        const auto plainA = phonemizeSsml("<speak>a</speak>");
        CHECK(loud.size() == plainA.size());
        for (std::size_t i = 0; i < loud.size(); ++i) {
            CHECK(std::abs(loud[i].pitchStart / plainA[i].pitchStart - 2.0) <
                  1e-9);
            CHECK(std::abs(loud[i].amplitude / plainA[i].amplitude - 1.5) <
                  1e-9);
        }
        const auto nested =
            phonemizeSsml("<speak><prosody rate=\"2\"><prosody rate=\"2\">a"
                          "</prosody></prosody></speak>");
        const auto single = phonemizeSsml("<speak>a</speak>");
        CHECK(nested.size() == single.size());
        for (std::size_t i = 0; i < nested.size(); ++i) {
            CHECK(std::abs(nested[i].duration * 4.0 - single[i].duration) <
                  1e-9);
        }

        // Sentence and paragraph structure inserts defined pauses.
        const auto structured =
            phonemizeSsml("<speak><p>One.<s>Two.</s></p></speak>");
        CHECK(std::count_if(structured.begin(), structured.end(),
                            [](const Phoneme &p) {
                                return p.kind == PhonemeKind::Silence;
                            }) >= 2);

        // Determinism: identical markup renders byte-identically.
        const Voice voice = voicePreset("neutral");
        const std::vector<float> first = Synthesizer(voice).render(
            phonemizeSsml("<speak><prosody pitch=\"+2st\">hello</prosody>"
                          "<break time=\"200ms\"/>world</speak>"));
        const std::vector<float> second = Synthesizer(voice).render(
            phonemizeSsml("<speak><prosody pitch=\"+2st\">hello</prosody>"
                          "<break time=\"200ms\"/>world</speak>"));
        CHECK(first == second);

        // Malformed markup fails loudly with the element name and byte offset.
        auto expectError = [](std::string_view markup,
                              std::string_view needle) {
            bool rejected = false;
            try {
                (void)phonemizeSsml(markup);
            } catch (const std::invalid_argument &error) {
                rejected =
                    std::string(error.what()).find(needle) != std::string::npos;
            }
            CHECK(rejected);
        };
        expectError("<speak><b>bold</b></speak>", "unsupported element <b>");
        expectError("<speak><break/></speak>", "SSML at byte");
        expectError("<speak><phoneme alphabet=\"ipa\"/></speak>",
                    "ph attribute");
        expectError("<speak><phoneme alphabet=\"klingon\" ph=\"a\"/></speak>",
                    "unsupported phoneme alphabet");
        expectError("<speak><say-as interpret-as=\"time\">now</say-as></speak>",
                    "unsupported say-as interpretation");
        expectError("<speak>unclosed", "unclosed <speak>");
        expectError("<speak><prosody rate=\"0\">a</prosody></speak>",
                    "rate must be positive");
        expectError("<speak><break time=\"5\"/></speak>", "break time");
        expectError("<speak><break strength=\"loud\"/></speak>",
                    "break strength");
        expectError("<speak><prosody pitch=\"150Hz\">a</prosody></speak>",
                    "pitch must use a relative unit");
        expectError("</speak>", "unexpected </speak>");
    }

    // Issue #124: data-defined nonverbal vocalizations.
    {
        NvvLibrary library(builtinNvvData());
        CHECK(library.names().size() >= 5);
        for (const char *name :
             {"laugh", "sigh", "breath-in", "breath-out", "throat-clear"}) {
            CHECK(library.has(name));
            CHECK(!library.get(name).empty());
        }
        CHECK(library.styleNames().front() == "neutral");

        // Styles deterministically scale duration, pitch and amplitude.
        const auto plain = library.get("laugh");
        library.setStyle("nervous");
        const auto nervous = library.get("laugh");
        CHECK(plain.size() == nervous.size());
        CHECK(std::abs(nervous[0].duration / plain[0].duration - 1.18) < 1e-9);
        CHECK(std::abs(nervous[0].pitchStart / plain[0].pitchStart - 1.22) <
              1e-9);
        library.setStyle("warm");
        CHECK(library.get("laugh")[0].duration < plain[0].duration);

        // Inline [name] tokens splice events between text runs at exact
        // positions; unknown tokens fail loudly.
        library.setStyle("neutral");
        const auto withToken =
            library.expand("hi [laugh] there", [](std::string_view run) {
                if (run == "hi ") {
                    return parseIpa("ha");
                }
                return parseIpa("a");
            });
        const auto withoutToken = parseIpa("ha");
        CHECK(withToken.size() == withoutToken.size() +
                                      library.get("laugh").size() +
                                      parseIpa("a").size());
        CHECK(names(withToken).front() == names(withoutToken).front());
        CHECK(names(withToken).back() == names(parseIpa("a")).back());

        bool unknownRejected = false;
        try {
            (void)library.get("nope");
        } catch (const std::invalid_argument &error) {
            unknownRejected =
                std::string(error.what()).find("nope") != std::string::npos;
        }
        CHECK(unknownRejected);
        bool styleRejected = false;
        try {
            library.setStyle("manic");
        } catch (const std::invalid_argument &) {
            styleRejected = true;
        }
        CHECK(styleRejected);

        // User definitions override built-ins and add new events.
        const char *user = "nvv laugh | override\n"
                           "e 0.03 1.2 1.4\n"
                           "e 0.04 1.4 0.9\n"
                           "nvv snort | new event\n"
                           "s 0.05\n"
                           "a 0.06 1.1 0.8\n";
        NvvLibrary custom(builtinNvvData(), user);
        CHECK(custom.has("snort"));
        CHECK(custom.get("laugh").size() == 2);
        CHECK(std::abs(custom.get("laugh")[0].duration - 0.03) < 1e-9);

        // SSML <nvv> resolves to the same sequence and composes with prosody.
        const auto ssmlNvv = phonemizeSsml(
            "<speak><nvv name=\"laugh\"/></speak>",
            [](std::string_view) { return std::vector<Phoneme>{}; },
            [&library](std::string_view name, size_t) {
                return library.get(name);
            });
        CHECK(ssmlNvv == library.get("laugh"));
        const auto fastNvv = phonemizeSsml(
            "<speak><prosody rate=\"2\"><nvv name=\"laugh\"/>"
            "</prosody></speak>",
            [](std::string_view) { return std::vector<Phoneme>{}; },
            [&library](std::string_view name, size_t) {
                return library.get(name);
            });
        CHECK(std::abs(fastNvv[0].duration * 2.0 - ssmlNvv[0].duration) < 1e-9);

        // Malformed data fails with a line number.
        auto rejects = [](const char *text) {
            try {
                (void)NvvLibrary(text);
            } catch (const std::invalid_argument &error) {
                return std::string(error.what()).find("nvv line") !=
                       std::string::npos;
            }
            return false;
        };
        CHECK(rejects("a 0.1\n")); // segment before any definition
        CHECK(rejects("nvv x | d\n\u02C4 0.1\n")); // unknown IPA symbol
        CHECK(rejects("nvv x | d\na 0\n"));        // non-positive duration
        CHECK(rejects("style x 0 1 1\n"));         // non-positive rate scale
        CHECK(rejects("nvv x | d\n"));             // no segments
    }

    // Issue #143: opt-in, data-defined breath-group planning.
    {
        const BreathPolicySet builtin = parseBreathData(builtinBreathData());
        CHECK(builtin.defaultPolicy.minWords == 4);
        CHECK(builtin.defaultPolicy.maxWords == 12);
        const BreathGroupPolicy spanish =
            resolveBreathPolicy(builtin, "es", "neutral");
        CHECK(spanish.minWords == 3);
        CHECK(std::abs(spanish.declination - 0.972) < 1e-12);
        const BreathGroupPolicy narrative =
            resolveBreathPolicy(builtin, "es", "narrative");
        CHECK(narrative.minWords == 5);
        CHECK(std::abs(narrative.depthLong - 1.22) < 1e-12);

        // A user table layers field by field; it does not drop Spanish or
        // narrative values from the built-in table.
        const char *userData = "language default\n"
                               "  max-words 7\n"
                               "style narrative\n"
                               "  depth-long 1.30\n";
        const BreathPolicySet layered =
            parseBreathData(builtinBreathData(), userData);
        CHECK(layered.defaultPolicy.maxWords == 7);
        const BreathGroupPolicy layeredNarrative =
            resolveBreathPolicy(layered, "es", "narrative");
        // A style overlays the language: narrative keeps its own limits and
        // declination while the user file retunes only its long depth.
        CHECK(layeredNarrative.minWords == 5);
        CHECK(std::abs(layeredNarrative.declination - 0.970) < 1e-12);
        CHECK(std::abs(layeredNarrative.depthLong - 1.30) < 1e-12);

        const auto rejectsPolicy = [](const char *text) {
            try {
                (void)parseBreathData(text);
            } catch (const std::invalid_argument &error) {
                return std::string(error.what()).find("breath line") !=
                       std::string::npos;
            }
            return false;
        };
        CHECK(rejectsPolicy("language default\n  wobble 3\n"));
        CHECK(rejectsPolicy("language default\n  declination 1.4\n"));
        CHECK(rejectsPolicy("language default\n  strong ab\n"));

        NvvLibrary library(builtinNvvData());
        const auto authoredBreath = library.get("breath-in");
        CHECK(!authoredBreath.empty());
        CHECK(authoredBreath.front().source == "nvv:breath-in");
        const auto eventLoader = [&library](std::string_view name) {
            return library.get(name);
        };

        // Twelve words before a comma trigger a long upcoming group.
        const auto phraseWith = [](std::size_t firstWords, const char *boundary,
                                   std::size_t secondWords) {
            std::vector<Phoneme> result;
            // The word gap is derived from "_" with a "|" display name, not
            // a row in the phoneme table.
            Phoneme gap = *findPhoneme("_");
            gap.name = "|";
            const auto appendWords = [&](std::size_t count) {
                for (std::size_t i = 0; i < count; ++i) {
                    result.push_back(*findPhoneme("a"));
                    result.push_back(gap);
                }
            };
            appendWords(firstWords);
            result.push_back(*findPhoneme(boundary));
            appendWords(secondWords);
            result.push_back(*findPhoneme("."));
            return result;
        };
        const std::vector<Phoneme> phrase = phraseWith(12, ",", 4);
        const BreathGroupPolicy policy = builtin.defaultPolicy;

        BreathPlanOptions options;
        options.mode = BreathMode::Off;
        const BreathPlan off =
            planBreathGroups(phrase, policy, eventLoader, options);
        CHECK(off.phonemes == phrase);
        CHECK(off.groups.empty());
        CHECK(off.insertedEvents == 0);

        options.mode = BreathMode::Auto;
        const BreathPlan planned =
            planBreathGroups(phrase, policy, eventLoader, options);
        const BreathPlan again =
            planBreathGroups(phrase, policy, eventLoader, options);
        CHECK(planned.phonemes == again.phonemes);
        CHECK(planned.groups.size() == 2);
        CHECK(planned.insertedEvents == 1);
        CHECK(planned.groups[0].words == 12);
        CHECK(planned.groups[0].depthTier == 2);
        CHECK(std::abs(planned.groups[0].depthScale - 1.18) < 1e-12);

        std::size_t autoBreathSegments = 0;
        const Phoneme *firstAutoBreath = nullptr;
        for (const Phoneme &phoneme : planned.phonemes) {
            if (phoneme.source == "breath:auto") {
                ++autoBreathSegments;
                if (firstAutoBreath == nullptr) {
                    firstAutoBreath = &phoneme;
                }
            }
        }
        CHECK(autoBreathSegments == authoredBreath.size());
        CHECK(firstAutoBreath != nullptr);
        if (firstAutoBreath != nullptr) {
            CHECK(std::abs(firstAutoBreath->duration /
                               authoredBreath.front().duration -
                           1.18) < 1e-12);
        }

        // Direct multipliers are the source of truth for the F0 oracle.
        const auto pitchAt = [&](const BreathGroup &group, bool last) {
            double result = 0.0;
            std::size_t count = 0;
            for (std::size_t i = group.begin; i < group.end; ++i) {
                if (!planned.phonemes[i].voiced) {
                    continue;
                }
                if (count == 0 || last) {
                    result = planned.phonemes[i].pitchStart;
                }
                ++count;
            }
            return result;
        };
        const BreathGroup &firstGroup = planned.groups.front();
        const BreathGroup &secondGroup = planned.groups.back();
        CHECK(pitchAt(firstGroup, false) > pitchAt(firstGroup, true));
        CHECK(std::abs(pitchAt(firstGroup, true) - 0.965) < 1e-12);
        CHECK(pitchAt(secondGroup, true) > pitchAt(firstGroup, true));
        CHECK(std::abs(pitchAt(secondGroup, false) - 1.045) < 1e-12);
        CHECK(std::abs(pitchAt(secondGroup, true) - 1.045 * 0.965) < 1e-12);

        // The shared F0 oracle sees the same downtrend and reset in Hz.
        Voice flat = voicePreset("neutral");
        flat.declination = 0.0;
        flat.vibratoDepth = 0.0;
        const AnalyzerOptions analyzer;
        const F0Track track =
            expectedF0(planned.phonemes, flat, 22050, analyzer);
        const auto meanBand = [&](const BreathGroup &group, bool last) {
            std::vector<double> values;
            for (const F0Frame &frame : track) {
                const double at =
                    static_cast<double>(frame.startSample) / 22050.0;
                if (frame.frequency > 0.0 && at >= group.startSeconds &&
                    at < group.endSeconds) {
                    values.push_back(frame.frequency);
                }
            }
            if (values.empty()) {
                return 0.0;
            }
            double sum = 0.0;
            const std::size_t count = std::min<std::size_t>(3, values.size());
            const std::size_t begin = last ? values.size() - count : 0;
            for (std::size_t i = 0; i < count; ++i) {
                sum += values[begin + i];
            }
            return sum / static_cast<double>(count);
        };
        CHECK(meanBand(firstGroup, false) > meanBand(firstGroup, true));
        CHECK(meanBand(secondGroup, false) > meanBand(firstGroup, true));

        options.mode = BreathMode::Silent;
        const BreathPlan silent =
            planBreathGroups(phrase, policy, eventLoader, options);
        CHECK(silent.groups.size() == 2);
        CHECK(silent.insertedEvents == 1);
        bool silentPause = false;
        for (const Phoneme &phoneme : silent.phonemes) {
            CHECK(phoneme.source != "breath:auto");
            if (phoneme.source == "breath:pause") {
                silentPause = true;
                CHECK(phoneme.kind == PhonemeKind::Silence);
                CHECK(std::abs(phoneme.duration - 0.08 * 1.18) < 1e-12);
            }
        }
        CHECK(silentPause);

        // An author-placed breath marks its own boundary and is never
        // duplicated by the planner.
        std::vector<Phoneme> explicitPhrase = phrase;
        const auto comma = std::find_if(
            explicitPhrase.begin(), explicitPhrase.end(),
            [](const Phoneme &phoneme) { return phoneme.name == ","; });
        explicitPhrase.insert(comma + 1, authoredBreath.begin(),
                              authoredBreath.end());
        options.mode = BreathMode::Auto;
        const BreathPlan preserved =
            planBreathGroups(explicitPhrase, policy, eventLoader, options);
        CHECK(preserved.insertedEvents == 0);
        CHECK(preserved.groups.size() >= 2);
        std::size_t authoredSegments = 0;
        for (const Phoneme &phoneme : preserved.phonemes) {
            if (phoneme.source == "nvv:breath-in") {
                ++authoredSegments;
            }
            CHECK(phoneme.source != "breath:auto");
        }
        CHECK(authoredSegments == authoredBreath.size());

        // Absolute prosody-direct targets survive untouched; later CLI
        // intonation/prosody files still override the relative contour.
        std::vector<Phoneme> absolute = phrase;
        absolute.front().f0StartHz = 180.0;
        absolute.front().f0EndHz = 180.0;
        const BreathPlan absolutePlan =
            planBreathGroups(absolute, policy, eventLoader, options);
        CHECK(absolutePlan.phonemes.front().f0StartHz.has_value());
        CHECK(*absolutePlan.phonemes.front().f0StartHz == 180.0);
        CHECK(absolutePlan.phonemes.front().pitchStart == 1.0);

        CHECK(breathModeFromName("off") == BreathMode::Off);
        CHECK(breathModeFromName("silent") == BreathMode::Silent);
        CHECK(breathModeFromName("auto") == BreathMode::Auto);
        bool modeRejected = false;
        try {
            (void)breathModeFromName("loud");
        } catch (const std::invalid_argument &) {
            modeRejected = true;
        }
        CHECK(modeRejected);
    }

    // Issue #129: prosody-direct override files. Every field is optional, so
    // a file can retune one segment while neighbours keep their defaults.
    {
        const ProsodyFile empty = parseProsodyFile("\n# nothing here\n  \n");
        CHECK(empty.empty());
        auto untouched = parseIpa("h ə l oʊ");
        const auto before = untouched;
        applyProsodyFile(untouched, empty);
        CHECK(untouched == before); // an empty file is a strict no-op

        const ProsodyFile file =
            parseProsodyFile("# retune the second segment\n"
                             "f0 1 200\n"
                             "duration 2 0.25s\n"
                             "amplitude 1 0.5\n");
        CHECK(file.directives.size() == 3);
        CHECK(file.directives[0].f0Hz == 200.0);
        CHECK(file.directives[1].durationSeconds == 0.25);
        CHECK(file.directives[2].amplitudeScale == 0.5);

        auto phonemes = parseIpa("h ə l oʊ");
        applyProsodyFile(phonemes, file);
        CHECK(phonemes.size() == before.size());
        CHECK(phonemes[1].f0StartHz == 200.0);
        CHECK(phonemes[1].f0EndHz == 200.0);
        CHECK(std::abs(phonemes[2].duration - 0.25) < 1e-12);
        CHECK(std::abs(phonemes[1].amplitude / before[1].amplitude - 0.5) <
              1e-12);
        // Unspecified fields, including all other segments, are untouched.
        CHECK(!phonemes[0].f0StartHz.has_value());
        CHECK(!phonemes[3].f0StartHz.has_value());
        CHECK(phonemes[0] == before[0]);
        CHECK(phonemes[3] == before[3]);
        CHECK(std::abs(phonemes[1].duration - before[1].duration) < 1e-12);

        // Relative values keep the front-end contour and compose with it.
        auto relative = parseIpa("h ə l oʊ");
        applyProsodyFile(
            relative, parseProsodyFile("f0 1 +1200c\nf0 2 +2st\nf0 3 +10%\n"));
        CHECK(std::abs(relative[1].pitchStart / before[1].pitchStart - 2.0) <
              1e-9);
        CHECK(std::abs(relative[2].pitchEnd / before[2].pitchEnd -
                       std::pow(2.0, 2.0 / 12.0)) < 1e-9);
        CHECK(std::abs(relative[3].pitchStart / before[3].pitchStart - 1.1) <
              1e-9);
        CHECK(!relative[1].f0StartHz.has_value());

        // A two-value directive is a linear absolute ramp; mixed units are
        // rejected at parse time.
        auto ramp = parseIpa("ee");
        applyProsodyFile(ramp, parseProsodyFile("f0 0 150 210\n"));
        CHECK(ramp[0].f0StartHz == 150.0);
        CHECK(ramp[0].f0EndHz == 210.0);

        // Duration scales and percentages multiply the front-end value.
        auto scaled = parseIpa("h ə l oʊ");
        applyProsodyFile(scaled,
                         parseProsodyFile("duration 1 2x\nduration 2 +50%\n"));
        CHECK(std::abs(scaled[1].duration / before[1].duration - 2.0) < 1e-12);
        CHECK(std::abs(scaled[2].duration / before[2].duration - 1.5) < 1e-12);

        // Time selectors resolve against the final timeline. Locate the
        // midpoint of the second segment from the current durations.
        auto timed = parseIpa("h ə l oʊ");
        const double midpoint =
            (timed[0].duration + timed[1].duration + timed[0].duration) / 2.0;
        applyProsodyFile(
            timed,
            parseProsodyFile("f0 @" + std::to_string(midpoint) + "s 220\n"));
        CHECK(timed[1].f0StartHz == 220.0);
        CHECK(!timed[0].f0StartHz.has_value());

        // Determinism and the phase-continuation contract still hold with
        // overrides applied: the stored phonemes fully describe the render.
        const Voice voice = voicePreset("neutral");
        const auto overridden = [&] {
            auto result = parseIpa("h ə l oʊ");
            applyProsodyFile(result,
                             parseProsodyFile("f0 1 200\nduration 2 0.2\n"));
            return result;
        };
        const auto first = Synthesizer(voice).render(overridden(), 44100);
        const auto second = Synthesizer(voice).render(overridden(), 44100);
        CHECK(first == second);

        const std::vector<Phoneme> whole = overridden();
        const std::vector<Phoneme> head(whole.begin(), whole.begin() + 2);
        const std::vector<Phoneme> tail(whole.begin() + 2, whole.end());
        const auto expected = Synthesizer(voice).render(whole, 44100);
        const RenderResult part1 =
            Synthesizer(voice).renderContinued(head, 44100, {});
        const RenderResult part2 =
            Synthesizer(voice).renderContinued(tail, 44100, part1.state);
        CHECK(part2.samples == expected);

        auto expectError = [](std::string_view text, std::string_view needle) {
            bool rejected = false;
            try {
                (void)parseProsodyFile(text);
            } catch (const std::invalid_argument &error) {
                rejected =
                    std::string(error.what()).find(needle) != std::string::npos;
            }
            CHECK(rejected);
        };
        expectError("wobble 0 1", "unsupported directive");
        expectError("f0 x 1", "invalid segment index");
        expectError("f0 0 nonsense", "F0 in Hz");
        expectError("f0 0 150 +2st", "cannot mix absolute and relative");
        expectError("f0 0 -5", "F0 must be between");
        expectError("f0 0 0", "F0 must be between");
        expectError("duration 0 -1", "duration scale must be positive");
        expectError("duration 0 0s", "duration must be between");
        expectError("amplitude 0 -0.5", "amplitude must not be negative");
        expectError("f0 @-1s 200", "time must be between");
        expectError("f0 0 1 2 3", "unexpected extra value");
        expectError("f0", "expected a directive and a selector");

        // Out-of-range selectors are reported at application time with the
        // source line, since the parser does not know the segment count.
        bool rangeRejected = false;
        try {
            auto brief = parseIpa("ee");
            applyProsodyFile(brief, parseProsodyFile("f0 5 200\n"));
        } catch (const std::invalid_argument &error) {
            rangeRejected = std::string(error.what()).find("prosody line 1") !=
                            std::string::npos;
        }
        CHECK(rangeRejected);

        bool timeRejected = false;
        try {
            auto brief = parseIpa("ee");
            applyProsodyFile(brief, parseProsodyFile("f0 @100s 200\n"));
        } catch (const std::invalid_argument &error) {
            timeRejected = std::string(error.what()).find("past the end") !=
                           std::string::npos;
        }
        CHECK(timeRejected);
    }

    // Issue #145: symbolic ToBI intonation. Inventories and tunes are pure
    // data, selectors are strict, and unselected phonemes stay byte-identical.
    {
        CHECK(intonationTuneNames(Language::English).size() == 5);
        CHECK(intonationTuneNames(Language::Spanish).size() == 5);
        for (const std::string &name : intonationTuneNames(Language::English)) {
            CHECK(findIntonationTune(Language::English, name) != nullptr);
        }
        CHECK(findIntonationTune(Language::English, "missing") == nullptr);

        ToneInventory inventory = defaultToneInventory();
        CHECK(inventory.accents.count("H*") == 1);
        CHECK(inventory.boundaries.count("L-H%") == 1);
        CHECK(inventory.downstep > 0.0 && inventory.downstep < 1.0);
        // Inventories and tunes are editable data, not code.
        inventory.accents["H*"] = {2.0, 2.0};
        inventory.boundaries["L-H%"] = {0.5, 1.5};
        inventory.breakTiers[2] = 0.42;
        inventory.downstep = 0.5;
        CHECK(defaultToneInventory().accents.at("H*").start == 1.30);
        CHECK(languageToneInventory(Language::Spanish).downstep !=
              languageToneInventory(Language::English).downstep);

        const IntonationFile empty = parseIntonationFile("\n# none\n  \n");
        CHECK(empty.empty());
        auto untouched = parseIpa("h ə l oʊ");
        const auto beforeIntonation = untouched;
        applyIntonation(untouched, Language::English, empty);
        CHECK(untouched == beforeIntonation);

        // Accent shapes replace the local contour; leading '!' applies one
        // downstep factor per operator.
        auto accented = parseIpa("h ə l oʊ");
        // Vowel nuclei are kind==Voiced: indices 1, 3 and 4 here. The
        // approximant 'l' at index 2 is voiced but not a syllable nucleus.
        applyIntonation(
            accented, Language::English,
            parseIntonationFile("accent vowel:1 L+H*\naccent vowel:2 !!!H*\n"));
        CHECK(std::abs(accented[3].pitchStart - 0.85) < 1e-12);
        CHECK(std::abs(accented[3].pitchEnd - 1.35) < 1e-12);
        CHECK(std::abs(accented[4].pitchStart -
                       1.30 * std::pow(defaultToneInventory().downstep, 3)) <
              1e-12);
        CHECK(accented[0] == beforeIntonation[0]);
        CHECK(accented[2] == beforeIntonation[2]);

        // Absolute and score-time selectors, plus a boundary tone on the
        // final vowel. Vowel selectors count voiced phonemes only.
        auto selected = parseIpa("h ə l oʊ");
        applyIntonation(selected, Language::English,
                        parseIntonationFile("accent 0 L*\naccent @0.20s H*\n"
                                            "boundary L-H%\n"));
        CHECK(std::abs(selected[0].pitchStart - 0.75) < 1e-12);
        CHECK(std::abs(selected[1].pitchStart - 1.30) < 1e-12);
        // The boundary tone lands on the final vowel (index 4), not the
        // voiced approximant.
        CHECK(std::abs(selected[4].pitchEnd - 1.32) < 1e-12);

        // AllVowels with an explicit custom inventory.
        auto each = parseIpa("h ə l oʊ");
        applyIntonation(each, Language::English,
                        parseIntonationFile("accent vowels H*\n"), inventory);
        for (const Phoneme &phoneme : each) {
            if (phoneme.kind == PhonemeKind::Voiced) {
                CHECK(std::abs(phoneme.pitchStart - 2.0) < 1e-12);
            }
        }

        // A named tune expands its events; short utterances skip missing
        // tune targets instead of failing, while explicit events stay strict.
        auto tune = parseIpa("h ə l oʊ");
        applyIntonation(tune, Language::English,
                        parseIntonationFile("tune statement\n"));
        CHECK(tune[0].pitchStart == 1.30 || tune[1].pitchStart == 1.30);
        auto shortTune = parseIpa("ee");
        const std::size_t shortCount = shortTune.size();
        applyIntonation(shortTune, Language::English,
                        parseIntonationFile("tune list\n"));
        CHECK(shortTune.size() ==
              shortCount); // missing tune targets are skipped

        // Break indices insert silence phonemes from the pause tier table.
        auto broken = parseIpa("h ə l oʊ");
        const std::size_t count = broken.size();
        applyIntonation(broken, Language::English,
                        parseIntonationFile("break 1 2\n"), inventory);
        CHECK(broken.size() == count + 1);
        CHECK(broken[2].kind == PhonemeKind::Silence);
        CHECK(std::abs(broken[2].duration - 0.42) < 1e-12);
        // Index 0 inserts nothing.
        auto noBreak = parseIpa("h ə l oʊ");
        applyIntonation(noBreak, Language::English,
                        parseIntonationFile("break 0 0\n"));
        CHECK(noBreak.size() == count);

        // Precedence: an absolute prosody-direct contour wins over an
        // expanded symbol because fundamentalAt() prefers the Hz target.
        auto layered = parseIpa("h ə l oʊ");
        applyIntonation(layered, Language::English,
                        parseIntonationFile("accent vowel:0 H*\n"));
        applyProsodyFile(layered, parseProsodyFile("f0 1 200\n"));
        CHECK(layered[1].f0StartHz == 200.0);
        // The symbol remains on the phoneme, but fundamentalAt() gives the
        // absolute Hz target precedence, so the rendered contour is 200 Hz.
        const Voice robot = voicePreset("robot");
        const double actual = detail::fundamentalAt(robot, layered[1], 22050, 0,
                                                    44100, 44100, 44100, false);
        CHECK(std::abs(actual - 200.0) < 1e-9);

        // Determinism and phase continuation hold for expanded contours.
        const Voice voice = voicePreset("neutral");
        const auto expanded = [&] {
            auto result = parseIpa("h ə l oʊ");
            applyIntonation(result, Language::English,
                            parseIntonationFile("tune yesno\n"));
            return result;
        };
        CHECK(Synthesizer(voice).render(expanded(), 44100) ==
              Synthesizer(voice).render(expanded(), 44100));
        const std::vector<Phoneme> whole = expanded();
        const std::vector<Phoneme> head(whole.begin(), whole.begin() + 2);
        const std::vector<Phoneme> tail(whole.begin() + 2, whole.end());
        const auto expected = Synthesizer(voice).render(whole, 44100);
        const RenderResult part1 =
            Synthesizer(voice).renderContinued(head, 44100, {});
        const RenderResult part2 =
            Synthesizer(voice).renderContinued(tail, 44100, part1.state);
        CHECK(part2.samples == expected);

        auto expectError = [](std::string_view text, std::string_view needle) {
            bool rejected = false;
            try {
                (void)parseIntonationFile(text);
            } catch (const std::invalid_argument &error) {
                rejected =
                    std::string(error.what()).find(needle) != std::string::npos;
            }
            CHECK(rejected);
        };
        expectError("wobble 0 1", "unsupported directive");
        expectError("accent 0", "selector and a label");
        expectError("accent x H*", "segment index");
        expectError("accent vowel:x H*", "vowel index");
        expectError("accent @-1s H*", "time must be between");
        expectError("accent 0 !", "tone name");
        expectError("accent 0 !!!!!!!!!H*", "too many downstep");
        expectError("break 0 9", "break index");
        expectError("break 0 x", "break index");
        expectError("boundary", "boundary needs a label");
        expectError("tune", "tune needs a name");
        expectError("accent 0 H* extra", "unexpected extra value");

        bool labelRejected = false;
        try {
            auto bad = parseIpa("ee");
            applyIntonation(bad, Language::English,
                            parseIntonationFile("accent 0 Q*\n"));
        } catch (const std::invalid_argument &error) {
            labelRejected =
                std::string(error.what())
                    .find("intonation line 1: unknown accent label") !=
                std::string::npos;
        }
        CHECK(labelRejected);

        bool tuneRejected = false;
        try {
            auto bad = parseIpa("ee");
            applyIntonation(bad, Language::English,
                            parseIntonationFile("tune nope\n"));
        } catch (const std::invalid_argument &error) {
            tuneRejected = std::string(error.what()).find("unknown tune") !=
                           std::string::npos;
        }
        CHECK(tuneRejected);

        bool rangeRejectedIntonation = false;
        try {
            auto bad = parseIpa("ee");
            applyIntonation(bad, Language::English,
                            parseIntonationFile("accent 9 H*\n"));
        } catch (const std::invalid_argument &error) {
            rangeRejectedIntonation =
                std::string(error.what()).find("matches no segment") !=
                std::string::npos;
        }
        CHECK(rangeRejectedIntonation);
    }

    // Issue #126: voice definition files are data, and the built-in presets
    // are expressible as files that reproduce their audio byte-for-byte.
    {
        const std::vector<formant::VoiceParameter> parameters =
            formant::voiceParameters();
        CHECK(parameters.size() == 14);
        for (std::size_t i = 0; i < parameters.size(); ++i) {
            CHECK(!parameters[i].name.empty());
            CHECK(parameters[i].minimum <= parameters[i].safeMinimum);
            CHECK(parameters[i].safeMinimum <= parameters[i].safeMaximum);
            CHECK(parameters[i].safeMaximum <= parameters[i].maximum);
            CHECK(!parameters[i].description.empty());
            for (std::size_t j = 0; j < i; ++j) {
                CHECK(parameters[i].name != parameters[j].name);
            }
        }

        const formant::VoiceDefinition definition =
            formant::parseVoiceDefinition(
                "# a warm voice\nschema 1\nname Warm Alto\n"
                "base alto\npitch 196.5\nbreathiness 0.02\n"
                "taper-ceiling false\n");
        CHECK(definition.schema == 1);
        CHECK(definition.name == "Warm Alto");
        CHECK(definition.base == "alto");
        CHECK(definition.values.size() == 2);
        const formant::Voice voice = formant::resolveVoice(definition);
        CHECK(voice.name == "Warm Alto");
        CHECK(voice.pitch == 196.5);
        CHECK(voice.breathiness == 0.02);
        CHECK(voice.formantScale == formant::voicePreset("alto").formantScale);
        CHECK(!voice.taperCeiling);

        auto voiceRejected = [](std::string_view text,
                                std::string_view fragment) {
            try {
                formant::resolveVoice(formant::parseVoiceDefinition(text));
            } catch (const std::invalid_argument &error) {
                return std::string(error.what()).find(fragment) !=
                       std::string::npos;
            }
            return false;
        };
        CHECK(voiceRejected("name x\npitch 100\n", "requires 'schema 1'"));
        CHECK(voiceRejected("schema 3\npitch 100\n", "voice line 1"));
        CHECK(voiceRejected("schema 1\nwobble 1\n", "unknown parameter"));
        CHECK(
            voiceRejected("schema 1\npitch 3000\n", "invalid value for pitch"));
        CHECK(voiceRejected("schema 1\nharmonics 20.5\n",
                            "invalid value for harmonics"));
        CHECK(voiceRejected("schema 1\npitch abc\n", "invalid number"));
        CHECK(voiceRejected("schema 1\npitch 100 200\n", "unexpected extra"));
        CHECK(voiceRejected("schema 1\nbase nosuch\n", "unknown voice preset"));

        formant::Voice scratch;
        bool unknownRejected = false;
        try {
            formant::setVoiceParameter(scratch, "nope", 1.0);
        } catch (const std::invalid_argument &) {
            unknownRejected = true;
        }
        CHECK(unknownRejected);
        bool getterRejected = false;
        try {
            (void)formant::voiceParameter(scratch, "nope");
        } catch (const std::invalid_argument &) {
            getterRejected = true;
        }
        CHECK(getterRejected);

        const std::vector<formant::Phoneme> roundTripSource =
            formant::parseIpa("ˈhəˈloʊ");
        for (const std::string &preset : formant::voicePresetNames()) {
            const formant::Voice expected = formant::voicePreset(preset);
            const std::string file = formant::writeVoiceDefinition(expected);
            const formant::Voice actual =
                formant::resolveVoice(formant::parseVoiceDefinition(file));
            CHECK(actual.name == expected.name);
            CHECK(actual.pitch == expected.pitch);
            CHECK(actual.declination == expected.declination);
            CHECK(actual.rate == expected.rate);
            CHECK(actual.formantScale == expected.formantScale);
            CHECK(actual.vibratoRate == expected.vibratoRate);
            CHECK(actual.vibratoDepth == expected.vibratoDepth);
            CHECK(actual.breathiness == expected.breathiness);
            CHECK(actual.nasality == expected.nasality);
            CHECK(actual.harmonics == expected.harmonics);
            CHECK(actual.attack == expected.attack);
            CHECK(actual.release == expected.release);
            CHECK(actual.gain == expected.gain);
            CHECK(actual.taperCeiling == expected.taperCeiling);
            CHECK(formant::Synthesizer(actual).render(roundTripSource, 8000) ==
                  formant::Synthesizer(expected).render(roundTripSource, 8000));
        }
        CHECK(formant::writeVoiceDefinition(formant::voicePreset("bass"))
                  .find("base bass\n") != std::string::npos);
    }

    // Issue #134: distributable voice packages carry attribution metadata and
    // a SHA-256 checksum, while schema 1 files stay loadable unchanged.
    {
        // checksumSource hashes each line with a trailing LF; these vectors
        // match `printf '...\n' | shasum -a 256`.
        CHECK(
            formant::voiceFileChecksum("") ==
            "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
        CHECK(
            formant::voiceFileChecksum("abc") ==
            "edeaaff3f1774ad2888673770c6d64097e391bc362d7d6fb34982ddf0efd18cb");
        CHECK(
            formant::voiceFileChecksum("hello world") ==
            "a948904f2f0f479b8f8197694b30184b0d2ed1c1cd2a1ec0fb85d299a192a447");

        const std::vector<formant::Phoneme> packageSource =
            formant::parseIpa("ˈhəˈloʊ");
        const formant::VoicePackageMetadata metadata = {
            "Warm Alto", "Ewan", "A warm alto", "CC0-1.0", "1.2.3"};
        const formant::Voice alto = formant::voicePreset("alto");
        const std::string package = formant::writeVoicePackage(alto, metadata);
        CHECK(package.find("schema 2\n") != std::string::npos);
        CHECK(package.find("author Ewan\n") != std::string::npos);
        CHECK(package.find("license CC0-1.0\n") != std::string::npos);
        CHECK(package.find("version 1.2.3\n") != std::string::npos);
        CHECK(package.find("checksum ") != std::string::npos);

        const formant::VoiceDefinition packaged =
            formant::parseVoiceDefinition(package);
        CHECK(packaged.schema == formant::kCurrentVoiceSchema);
        CHECK(packaged.name == "Warm Alto");
        CHECK(packaged.author == "Ewan");
        CHECK(packaged.description == "A warm alto");
        CHECK(packaged.license == "CC0-1.0");
        CHECK(packaged.version == "1.2.3");
        CHECK(packaged.checksum.has_value());
        CHECK(*packaged.checksum == formant::voiceFileChecksum(package));
        const formant::Voice resolved = formant::resolveVoice(packaged);
        CHECK(resolved.pitch == alto.pitch);
        CHECK(formant::Synthesizer(resolved).render(packageSource, 8000) ==
              formant::Synthesizer(alto).render(packageSource, 8000));

        // Inspection parses with verification disabled so a bad checksum can
        // be reported rather than thrown.
        const formant::VoiceDefinition inspected =
            formant::parseVoiceDefinition(package, false);
        CHECK(inspected.checksum.has_value());

        auto packageRejected = [](std::string_view text,
                                  std::string_view fragment) {
            try {
                formant::parseVoiceDefinition(text);
            } catch (const std::invalid_argument &error) {
                return std::string(error.what()).find(fragment) !=
                       std::string::npos;
            }
            return false;
        };
        CHECK(packageRejected("schema 3\n", "voice line 1"));
        CHECK(packageRejected(
            "schema 2\nname x\nauthor y\ndescription z\nlicense l\n"
            "version 1\n",
            "requires a checksum"));
        CHECK(packageRejected(
            "schema 2\nname x\nauthor y\ndescription z\nlicense l\n"
            "version 1\nchecksum abc\n",
            "64 hexadecimal digits"));
        std::string tampered = package;
        const std::size_t pos = tampered.find("pitch ");
        tampered.replace(pos, std::string("pitch ").size(), "pitch 1");
        CHECK(packageRejected(tampered, "checksum mismatch"));

        bool missingMetadataRejected = false;
        try {
            formant::writeVoicePackage(
                alto, {"", "Ewan", "A warm alto", "CC0-1.0", "1.2.3"});
        } catch (const std::invalid_argument &) {
            missingMetadataRejected = true;
        }
        CHECK(missingMetadataRejected);

        // A CRLF package hashes like its LF form, so Windows-authored files
        // verify without regenerating the checksum.
        std::string crlf;
        for (char c : package) {
            if (c == '\n') {
                crlf += "\r\n";
            } else {
                crlf += c;
            }
        }
        CHECK(formant::voiceFileChecksum(crlf) ==
              formant::voiceFileChecksum(package));
        CHECK(formant::parseVoiceDefinition(crlf).version == "1.2.3");
    }

    // ------------------------------------------------------------------
    // #114: seeded phonotactic gibberish generator.
    // ------------------------------------------------------------------
    {
        using formant::GibberishResult;
        using formant::GibberishSettings;
        using formant::PatternGrammar;
        using formant::PatternInventory;

        auto throwsInvalid = [](auto operation) {
            try {
                operation();
            } catch (const std::invalid_argument &) {
                return true;
            }
            return false;
        };

        const PatternInventory builtin = formant::defaultPatternInventory();
        CHECK(!builtin.empty('C'));
        CHECK(!builtin.empty('V'));
        CHECK(!builtin.empty('N'));
        // Categories outside the built-in set are empty until defined.
        CHECK(builtin.empty('X'));

        // Same seed, pattern and profile => identical transcript and PCM.
        const PatternGrammar cvc = formant::parsePatternGrammar("CVC");
        GibberishSettings settings;
        settings.seed = 7;
        settings.count = 4;
        const GibberishResult first =
            formant::generateGibberish(cvc, builtin, settings);
        const GibberishResult second =
            formant::generateGibberish(cvc, builtin, settings);
        CHECK(first.words.size() == 4);
        CHECK(first.transcript == second.transcript);
        CHECK(first.phonemes == second.phonemes);
        CHECK(
            Synthesizer(voicePreset("neutral")).render(first.phonemes, 8000) ==
            Synthesizer(voicePreset("neutral")).render(second.phonemes, 8000));

        // A different seed produces a different transcript.
        settings.seed = 8;
        CHECK(formant::generateGibberish(cvc, builtin, settings).transcript !=
              first.transcript);

        // Generated transcripts are valid IPA and round-trip through parseIpa.
        for (const formant::GeneratedWord &word : first.words) {
            CHECK(!word.ipa.empty());
            CHECK(!word.phonemes.empty());
            CHECK(names(word.phonemes) == names(formant::parseIpa(word.ipa)));
        }
        CHECK(first.transcript.find('\n') != std::string::npos);

        // Weights are respected over a fixed seed sweep (bucket test).
        const PatternGrammar weighted = formant::parsePatternGrammar("a/b*3");
        settings.count = 1;
        settings.prosody = false;
        int optionA = 0;
        int optionB = 0;
        for (uint32_t seed = 1; seed <= 200; ++seed) {
            settings.seed = seed;
            const GibberishResult one =
                formant::generateGibberish(weighted, builtin, settings);
            CHECK(one.words.size() == 1);
            if (one.words[0].ipa == "a") {
                ++optionA;
            } else if (one.words[0].ipa == "b") {
                ++optionB;
            } else {
                CHECK(false);
            }
        }
        CHECK(optionA + optionB == 200);
        // 1:3 split: expect roughly 50/150; allow a generous margin so this is
        // a distribution check rather than a test of one particular seed.
        CHECK(optionB > optionA * 2);

        // Exclusions are honoured: no two identical adjacent vowels.
        const PatternGrammar filtered =
            formant::parsePatternGrammar("[[a/i/u][a/i/u]]^aa^ii^uu");
        settings.seed = 3;
        settings.count = 30;
        settings.prosody = true;
        const GibberishResult filteredWords =
            formant::generateGibberish(filtered, builtin, settings);
        for (const formant::GeneratedWord &word : filteredWords.words) {
            const std::vector<std::string> segment = names(word.phonemes);
            CHECK(segment.size() >= 2);
            CHECK(segment[0] != segment[1]);
        }

        // Optional groups and nesting work, and empty expansions are skipped.
        const PatternGrammar optional = formant::parsePatternGrammar("a(b(c))");
        settings.seed = 11;
        settings.count = 20;
        const GibberishResult optionalWords =
            formant::generateGibberish(optional, builtin, settings);
        bool sawShort = false;
        bool sawFull = false;
        for (const formant::GeneratedWord &word : optionalWords.words) {
            CHECK(word.ipa == "a" || word.ipa == "a b" || word.ipa == "a b c");
            sawShort = sawShort || word.ipa == "a";
            sawFull = sawFull || word.ipa == "a b c";
        }
        CHECK(sawShort);
        CHECK(sawFull);

        // Inline subpattern definitions override the inventory and may refer
        // to each other.
        const PatternGrammar defined =
            formant::parsePatternGrammar("C=p/t/k\nV=a/i/u\nX=CV\nXX");
        settings.seed = 5;
        settings.count = 10;
        const GibberishResult definedWords =
            formant::generateGibberish(defined, builtin, settings);
        CHECK(definedWords.words.size() == 10);
        for (const formant::GeneratedWord &word : definedWords.words) {
            const std::vector<std::string> segment = names(word.phonemes);
            CHECK(segment.size() == 4);
            CHECK(segment[0] == "p" || segment[0] == "t" || segment[0] == "k" ||
                  segment[0] == "i" || segment[0] == "u" || segment[0] == "a");
        }
        CHECK(throwsInvalid([&] {
            formant::generateGibberish(
                formant::parsePatternGrammar("X=Y\nY=X\nX"),
                formant::defaultPatternInventory(), GibberishSettings{});
        }));

        // Conlang definition files supply categories through their classes.
        const LanguageDefinition conlang =
            formant::parseLanguageDefinition("name Testlang\n"
                                             "tag zz\n"
                                             "class V a e\n"
                                             "class C p t k\n"
                                             "phoneme a\n"
                                             "phoneme e\n"
                                             "phoneme p\n"
                                             "phoneme t\n"
                                             "phoneme k\n");
        const PatternInventory conlangInventory =
            formant::patternInventory(conlang);
        CHECK(conlangInventory.get('V') ==
              std::vector<std::string>({"a", "e"}));
        CHECK(conlangInventory.get('C') ==
              std::vector<std::string>({"p", "t", "k"}));
        const GibberishResult conlangWords = formant::generateGibberish(
            cvc, conlangInventory, settings, Language::English);
        CHECK(conlangWords.words.size() == 10);
        for (const formant::GeneratedWord &word : conlangWords.words) {
            const std::vector<std::string> segment = names(word.phonemes);
            CHECK(contains(std::vector<std::string>({"a", "e", "p", "t", "k"}),
                           segment.front()));
        }

        // Prosody variation changes the phoneme contour but not the transcript.
        settings.seed = 21;
        settings.count = 8;
        settings.prosody = false;
        const GibberishResult plain =
            formant::generateGibberish(cvc, builtin, settings);
        settings.prosody = true;
        const GibberishResult varying =
            formant::generateGibberish(cvc, builtin, settings);
        CHECK(plain.transcript == varying.transcript);
        CHECK(plain.phonemes != varying.phonemes);
        CHECK(contains(names(varying.phonemes), "?") ||
              contains(names(varying.phonemes), ",") ||
              contains(names(varying.phonemes), "."));

        // Grammar errors are rejected early and instructively.
        CHECK(throwsInvalid([] { formant::parsePatternGrammar(""); }));
        CHECK(throwsInvalid([] { formant::parsePatternGrammar("a/b*0"); }));
        CHECK(throwsInvalid([] { formant::parsePatternGrammar("a/b*129"); }));
        CHECK(throwsInvalid([] { formant::parsePatternGrammar("[a"); }));
        CHECK(throwsInvalid([] { formant::parsePatternGrammar("a(b"); }));
        CHECK(throwsInvalid([] { formant::parsePatternGrammar("[a]^"); }));
        CHECK(throwsInvalid([&] {
            formant::generateGibberish(formant::parsePatternGrammar("X"),
                                       builtin, GibberishSettings{});
        }));
        CHECK(throwsInvalid([&] {
            GibberishSettings bad;
            bad.count = 0;
            formant::generateGibberish(cvc, builtin, bad);
        }));
        CHECK(formant::patternGrammarHelp().find("weight") !=
              std::string::npos);

        // #115: statistical/combined gibberish modes from n-gram tables.
        const auto enTable = formant::builtinPhonotacticNgramTable("en-us");
        CHECK(enTable.languageTag == "en-us");
        CHECK(!enTable.empty());
        CHECK(formant::availableGibberishStyles().size() >= 1);

        GibberishSettings stat = settings;
        stat.seed = 42;
        stat.count = 6;
        stat.style = formant::GibberishStyle::Statistical;
        stat.styleTag = "en-us";
        const GibberishResult statA = formant::generateGibberish(
            formant::PatternGrammar{}, builtin, stat);
        const GibberishResult statB = formant::generateGibberish(
            formant::PatternGrammar{}, builtin, stat);
        CHECK(statA.transcript == statB.transcript);
        CHECK(statA.phonemes == statB.phonemes);
        CHECK(
            Synthesizer(voicePreset("neutral")).render(statA.phonemes, 8000) ==
            Synthesizer(voicePreset("neutral")).render(statB.phonemes, 8000));
        for (const formant::GeneratedWord &word : statA.words) {
            std::vector<std::string> atoms;
            {
                std::istringstream spaced(word.ipa);
                std::string atom;
                while (spaced >> atom) {
                    atoms.push_back(atom);
                }
            }
            CHECK(!atoms.empty());
            // Validate generated sequences against the same constrained table.
            PhonotacticNgramTable constrained = enTable;
            constrained.starts.clear();
            constrained.ends.clear();
            constrained.unigrams.clear();
            constrained.bigrams.clear();
            constrained.trigrams.clear();
            constrained.lengths = enTable.lengths;
            // Rebuild constrained view through generation contract by checking
            // each adjacent transition exists in bigram table or unigram
            // fallback.
            for (size_t i = 0; i + 1 < atoms.size(); ++i) {
                auto bit = enTable.bigrams.find(atoms[i]);
                bool ok = false;
                if (bit != enTable.bigrams.end()) {
                    for (const auto &next : bit->second) {
                        if (next.first == atoms[i + 1]) {
                            ok = true;
                            break;
                        }
                    }
                }
                CHECK(ok);
                if (i + 2 < atoms.size()) {
                    auto tit =
                        enTable.trigrams.find(atoms[i] + " " + atoms[i + 1]);
                    if (tit != enTable.trigrams.end()) {
                        bool tok = false;
                        for (const auto &next : tit->second) {
                            if (next.first == atoms[i + 2]) {
                                tok = true;
                                break;
                            }
                        }
                        CHECK(tok);
                    }
                }
            }
        }

        stat.seed = 43;
        CHECK(
            formant::generateGibberish(formant::PatternGrammar{}, builtin, stat)
                .transcript != statA.transcript);

        GibberishSettings comb = stat;
        comb.seed = 42;
        comb.style = formant::GibberishStyle::Combined;
        const GibberishResult combA = formant::generateGibberish(
            formant::parsePatternGrammar("CVC"), builtin, comb);
        const GibberishResult combB = formant::generateGibberish(
            formant::parsePatternGrammar("CVC"), builtin, comb);
        CHECK(combA.transcript == combB.transcript);
        CHECK(combA.phonemes == combB.phonemes);
        for (const formant::GeneratedWord &word : combA.words) {
            CHECK(names(word.phonemes).size() == 3);
        }
        CHECK(throwsInvalid([&] {
            GibberishSettings bad = stat;
            bad.styleTag = "nope-lang";
            formant::generateGibberish(formant::PatternGrammar{}, builtin, bad);
        }));
    }

    // #105: Modified Rhyme Test battery generation.
    {
        const auto ensembles = formant::mrtEnsembles();
        CHECK(ensembles.size() == 50);
        for (const formant::MrtEnsemble &e : ensembles) {
            CHECK(!e.id.empty());
            CHECK(e.focus == "initial" || e.focus == "final");
            for (const std::string &w : e.words) {
                CHECK(!w.empty());
            }
        }
        const std::filesystem::path dir =
            std::filesystem::temp_directory_path() / "formant-mrt-test";
        std::filesystem::remove_all(dir);
        formant::MrtBatteryOptions opt;
        opt.outDir = dir.string();
        opt.voice = formant::voicePreset("neutral");
        opt.language = formant::Language::English;
        opt.sampleRate = 22050;
        const formant::MrtBatteryResult r = formant::writeMrtBattery(opt);
        CHECK(r.filesWritten == 303);
        CHECK(std::filesystem::exists(dir / "response_sheet.csv"));
        CHECK(std::filesystem::exists(dir / "answer_key.csv"));
        CHECK(std::filesystem::exists(dir / "README.txt"));
        CHECK(std::filesystem::exists(dir / "mrt_001_e01_initial_went.wav"));
        CHECK(std::filesystem::exists(dir / "mrt_300_e50_initial_nun.wav"));
        std::filesystem::remove_all(dir);
    }

    // #109: formant frequencies glide in log2 space, the resonance total has
    // a C1 soft knee instead of a hard clamp, and non-plosive amplitude ramps
    // use smoothstep while plosives keep their documented linear bursts.
    {
        const FormantBand low{200.0, 1.0, 100.0};
        const FormantBand high{800.0, 0.5, 50.0};
        const FormantBand half = detail::blendBand(low, high, 0.5);
        // The geometric mean is 400 Hz; a linear blend would say 500 Hz.
        CHECK(std::abs(half.frequency - 400.0) < 1e-3);
        CHECK(std::abs(half.amplitude - 0.75) < 1e-12);
        CHECK(std::abs(half.bandwidth - 75.0) < 1e-12);
        CHECK(detail::blendBand(low, high, 0.0).frequency == low.frequency);
        CHECK(detail::blendBand(low, high, 1.0).frequency == high.frequency);
        // A zero inherited band still fades in linearly; log2(0) is invalid.
        CHECK(std::abs(
                  detail::blendBand(detail::zeroBand(), high, 0.5).frequency -
                  400.0) < 1e-12);

        CHECK(detail::softResonanceLimit(1.5) == 1.5);
        CHECK(detail::softResonanceLimit(detail::kResonanceClamp) ==
              detail::kResonanceClamp);
        double previousLimit = detail::softResonanceLimit(2.0);
        for (double total = 2.25; total < 20.0; total += 0.25) {
            const double limited = detail::softResonanceLimit(total);
            CHECK(limited >= previousLimit - 1e-12);
            CHECK(limited <= detail::kResonanceClamp +
                                 detail::kResonanceKneeHeadroom + 1e-12);
            previousLimit = limited;
        }
        // Slope is 1 on both sides of the corner; the old min() fell to 0.
        const double epsilon = 1e-6;
        const double slopeBelow = (detail::softResonanceLimit(2.0) -
                                   detail::softResonanceLimit(2.0 - epsilon)) /
                                  epsilon;
        const double slopeAbove = (detail::softResonanceLimit(2.0 + epsilon) -
                                   detail::softResonanceLimit(2.0)) /
                                  epsilon;
        CHECK(std::abs(slopeBelow - 1.0) < 1e-4);
        CHECK(std::abs(slopeAbove - 1.0) < 1e-4);

        std::array<FormantBand, 4> hot{};
        hot[0] = FormantBand{1000.0, 5.0, 100.0};
        CHECK(detail::formantResponse(1000.0, hot, 1.0) > 2.0);
        CHECK(detail::formantResponse(1000.0, hot, 1.0) <= 2.25);

        // Derived bound: smoothstep's maximum slope is 1.5 / ramp length, so
        // no per-sample amplitude step may exceed that. Entering and leaving
        // the sustain has a near-zero delta, i.e. no C0 corner at the join.
        const int length = 4410;
        const int attack = 441;
        const int release = 441;
        double maxDelta = 0.0;
        for (int offset = 0; offset < length; ++offset) {
            const double current = detail::amplitudeEnvelope(
                offset, length, attack, release, false);
            const double next =
                offset + 1 < length
                    ? detail::amplitudeEnvelope(offset + 1, length, attack,
                                                release, false)
                    : 0.0;
            CHECK(current >= 0.0 && current <= 1.0);
            maxDelta = std::max(maxDelta, std::abs(next - current));
        }
        CHECK(maxDelta <= 1.5 / static_cast<double>(attack) + 1e-12);
        const auto deltaAt = [&](int offset) {
            return std::abs(detail::amplitudeEnvelope(offset + 1, length,
                                                      attack, release, false) -
                            detail::amplitudeEnvelope(offset, length, attack,
                                                      release, false));
        };
        // The first step is 3 / attack^2, not zero: smoothstep has zero
        // derivative at t=0 but a finite discrete difference. It is still far
        // below the 1.5 / attack maximum slope.
        CHECK(deltaAt(0) < 3.0 / (attack * attack) + 1e-12);
        CHECK(deltaAt(0) < 1.5 / attack);
        CHECK(deltaAt(attack - 1) < 3.0 / (attack * attack) + 1e-12);
        CHECK(deltaAt(length - release) < 3.0 / (release * release) + 1e-12);
        CHECK(detail::amplitudeEnvelope(attack, length, attack, release,
                                        false) == 1.0);

        // Plosives deliberately keep the linear 3/4 ms burst: its first step
        // is 1/attack, far above the smoothstep ramp's zero first step.
        const double plosiveFirst =
            detail::amplitudeEnvelope(1, length, attack, release, true);
        const double smoothFirst =
            detail::amplitudeEnvelope(1, length, attack, release, false);
        CHECK(plosiveFirst > smoothFirst);
        CHECK(std::abs(plosiveFirst - 1.0 / attack) < 1e-12);
    }

    // #100: nasal phonemes carry spectral zeros (anti-formants) beside their
    // formants, and the IPA nasalization diacritic reaches the renderer.
    {
        for (const char *name : {"m", "n", "ng"}) {
            const Phoneme *nasal = findPhoneme(name);
            CHECK(nasal != nullptr);
            if (nasal != nullptr) {
                CHECK(nasal->zeros[0].frequency > 0.0);
                CHECK(nasal->zeros[0].bandwidth > 0.0);
                CHECK(nasal->zeros[0].depth > 0.0);
                CHECK(nasal->zeros[1].depth > 0.0);
            }
        }
        CHECK(findPhoneme("a")->zeros[0].depth == 0.0);

        const Phoneme oral = parseIpa("a").front();
        const Phoneme nasalized = parseIpa("a\u0303").front();
        CHECK(oral.zeros[0].depth == 0.0);
        CHECK(nasalized.zeros[0].depth > 0.0);
        CHECK(nasalized.zeros[0].depth <= 0.95);
        CHECK(parseIpa("\u014B").front().zeros[0].depth > 0.0);

        // The zero is a notch in the same response the synthesiser evaluates.
        const detail::FormantEnvelope oralEnvelope =
            detail::makeFormantEnvelope(oral.bands, oral.zeros, 1.0);
        const detail::FormantEnvelope nasalEnvelope =
            detail::makeFormantEnvelope(nasalized.bands, nasalized.zeros, 1.0);
        const double zeroFrequency = nasalized.zeros[0].frequency;
        CHECK(detail::formantResponse(zeroFrequency, nasalEnvelope) <
              detail::formantResponse(zeroFrequency, oralEnvelope));

        // Removing the table data must change the rendered m.
        Voice flat = voicePreset("robot");
        flat.vibratoDepth = 0.0;
        flat.breathiness = 0.0;
        const Phoneme nasal = *findPhoneme("m");
        Phoneme oralStop = nasal;
        oralStop.zeros = {};
        const std::vector<float> notched =
            Synthesizer(flat).render({nasal}, 22050);
        const std::vector<float> unnotched =
            Synthesizer(flat).render({oralStop}, 22050);
        CHECK(notched != unnotched);

        // Voice-wide nasality is a separate, opt-in notch.
        Voice twang = flat;
        twang.nasality = 0.4;
        CHECK(Synthesizer(twang).render({oral}, 22050) !=
              Synthesizer(flat).render({oral}, 22050));
        Voice zeroNasality = flat;
        zeroNasality.nasality = 0.0;
        CHECK(Synthesizer(zeroNasality).render({oral}, 22050) ==
              Synthesizer(flat).render({oral}, 22050));
    }

    {
        // #177: the growing network is deterministic, bounded and opt-in.
        const auto makeSample = [](std::array<double, 4> input,
                                   std::array<double, 2> target) {
            NeuralSample sample;
            sample.input = input;
            sample.target = target;
            return sample;
        };

        GrowingNeuralNetwork empty;
        CHECK(empty.units().empty());
        CHECK((empty.predict({0.5, 0.5, 0.5, 0.5}) ==
               std::array<double, 2>{1.0, 1.0}));
        const std::string emptyState = empty.serialize();
        CHECK(GrowingNeuralNetwork::parse(emptyState).serialize() ==
              emptyState);

        NeuralConfig growthConfig;
        growthConfig.maxUnits = 4;
        growthConfig.growthInterval = 1;
        growthConfig.insertionError = 0.001;
        std::vector<NeuralSample> training;
        for (int i = 0; i < 12; ++i) {
            const double corner = (i % 2 == 0) ? 0.0 : 1.0;
            const double pitch = (i % 2 == 0) ? 0.65 : 1.55;
            training.push_back(makeSample({corner, corner, corner, corner},
                                          {pitch, 1.0 / pitch}));
        }

        GrowingNeuralNetwork first(growthConfig);
        GrowingNeuralNetwork second(growthConfig);
        first.train(training);
        second.train(training);
        CHECK(first.serialize() == second.serialize());
        CHECK(first.units() == second.units());
        CHECK(first.edges() == second.edges());
        CHECK(first.summary().observations == training.size());
        CHECK(first.units().size() == growthConfig.maxUnits);
        CHECK(first.growthCount() == growthConfig.maxUnits - 1);

        // Repeated explained samples never grow the topology.
        NeuralConfig stableConfig;
        stableConfig.maxUnits = 2;
        stableConfig.growthInterval = 1;
        GrowingNeuralNetwork stable(stableConfig);
        const NeuralSample repeated =
            makeSample({0.4, 0.2, 0.5, 1.0}, {1.0, 1.0});
        for (int i = 0; i < 20; ++i) {
            CHECK(!stable.observe(repeated));
        }
        CHECK(stable.units().size() == 1);
        CHECK(stable.growthCount() == 0);

        // Loading and continuing training replays the earlier state exactly.
        NeuralConfig learningConfig = growthConfig;
        learningConfig.maxUnits = 8;
        GrowingNeuralNetwork original(learningConfig);
        original.train({training.begin(), training.begin() + 4});
        const std::string state = original.serialize();
        const GrowingNeuralNetwork restored =
            GrowingNeuralNetwork::parse(state);
        CHECK(restored.serialize() == state);
        CHECK(restored.units().size() >= original.units().size());

        const std::vector<NeuralSample> laterSamples = {
            makeSample({0.0, 0.0, 1.0, 0.0}, {0.7, 1.2}),
            makeSample({1.0, 1.0, 0.0, 1.0}, {1.4, 0.8}),
            makeSample({0.5, 0.5, 0.5, 1.0}, {1.1, 0.95}),
            makeSample({0.0, 1.0, 0.0, 1.0}, {0.8, 1.3}),
            makeSample({1.0, 0.0, 1.0, 0.0}, {1.3, 0.75}),
            makeSample({0.25, 0.75, 0.5, 0.25}, {0.9, 1.15}),
        };
        GrowingNeuralNetwork continued = restored;
        continued.train(laterSamples);
        GrowingNeuralNetwork replay = GrowingNeuralNetwork::parse(state);
        replay.train(laterSamples);
        CHECK(continued.serialize() == replay.serialize());
        CHECK(continued.units().size() >= restored.units().size());
        CHECK(continued.units().size() <= continued.config().maxUnits);

        bool sampleRejected = false;
        std::string sampleError;
        try {
            (void)parseNeuralSamples("sample 0 0 0 0 1 1\nbogus\n");
        } catch (const std::invalid_argument &error) {
            sampleRejected = true;
            sampleError = error.what();
        }
        CHECK(sampleRejected);
        CHECK(sampleError.find("neural line 2") != std::string::npos);

        bool stateRejected = false;
        std::string stateError;
        try {
            (void)GrowingNeuralNetwork::parse("formant-neural 1\nbogus\n");
        } catch (const std::invalid_argument &error) {
            stateRejected = true;
            stateError = error.what();
        }
        CHECK(stateRejected);
        CHECK(stateError.find("neural line 2") != std::string::npos);

        Phoneme h;
        h.name = "h";
        h.kind = PhonemeKind::Fricative;
        Phoneme ee;
        ee.name = "ee";
        ee.kind = PhonemeKind::Voiced;
        Phoneme comma;
        comma.name = ",";
        comma.kind = PhonemeKind::Silence;
        std::vector<Phoneme> context{h, ee, comma, h};
        for (std::size_t i = 0; i < context.size(); ++i) {
            const std::array<double, 4> features = neuralFeatures(context, i);
            for (const double value : features) {
                CHECK(value >= 0.0 && value <= 1.0);
            }
        }
        CHECK(neuralFeatures(context, 0)[0] == 0.0);
        CHECK(neuralFeatures(context, 3)[0] == 1.0);
        CHECK(neuralFeatures(context, 1)[2] == 0.5);

        GrowingNeuralNetwork learned;
        learned.observe(makeSample({0.0, 0.0, 0.0, 0.0}, {1.2, 1.4}));
        std::vector<Phoneme> applied{
            h,
            h,
            comma,
            h,
        };
        applied[1].source = "breath:auto";
        applied[3].f0StartHz = 120.0;
        const std::vector<Phoneme> beforeApply = applied;
        applyNeuralProsody(applied, learned, 0.0);
        CHECK(applied == beforeApply);
        applyNeuralProsody(applied, learned, 0.5);
        CHECK(std::abs(applied[0].pitchStart - 1.1) < 1e-12);
        CHECK(std::abs(applied[0].pitchEnd - 1.1) < 1e-12);
        CHECK(std::abs(applied[0].duration - 0.12) < 1e-12);
        CHECK(applied[1] == beforeApply[1]);
        CHECK(applied[2] == beforeApply[2]);
        CHECK(applied[3] == beforeApply[3]);
    }

    {
        // #125: affect is data, opt-in and deterministic. The built-in table
        // exposes named PAD presets and low-level levers; no style means no
        // waveform change.
        const AffectData data = parseAffectData(builtinAffectData());
        CHECK(data.schema == kCurrentAffectSchema);
        CHECK(data.styles.size() == 6);
        CHECK(data.styles.count("neutral") == 1);
        CHECK(data.styles.count("calm") == 1);
        CHECK(data.styles.count("excited") == 1);
        CHECK(data.styles.count("sad") == 1);
        CHECK(data.styles.count("whispering") == 1);
        CHECK(data.styles.count("newsreader") == 1);
        CHECK(data.emphasis.count("strong") == 1);
        CHECK(data.emphasis.count("moderate") == 1);
        CHECK(data.emphasis.count("reduced") == 1);

        const AffectProfile neutral = resolveAffectProfile(data, "neutral");
        CHECK(neutral.axes == AffectAxes{});
        CHECK(std::abs(neutral.levers.pitch - 1.0) < 1e-12);
        CHECK(std::abs(neutral.levers.range - 1.0) < 1e-12);
        CHECK(std::abs(neutral.levers.rate - 1.0) < 1e-12);
        CHECK(std::abs(neutral.levers.intensity - 1.0) < 1e-12);
        CHECK(neutral.levers.jitterCents == 0.0);
        CHECK(neutral.levers.shimmer == 0.0);

        const AffectProfile calm = resolveAffectProfile(data, "calm");
        const AffectProfile excited = resolveAffectProfile(data, "excited");
        const AffectProfile whispering =
            resolveAffectProfile(data, "whispering");
        CHECK(calm.levers.rate < neutral.levers.rate);
        CHECK(excited.levers.rate > neutral.levers.rate);
        CHECK(excited.levers.pitch > calm.levers.pitch);
        CHECK(excited.levers.intensity > calm.levers.intensity);
        CHECK(whispering.levers.rate < neutral.levers.rate);

        // A coordinate halfway between two presets interpolates their levers:
        // the mapping is an affine function of the three axes.
        const AffectProfile midpoint = resolveAffectProfile(
            data, "", (calm.axes.arousal + excited.axes.arousal) / 2.0,
            (calm.axes.valence + excited.axes.valence) / 2.0,
            (calm.axes.dominance + excited.axes.dominance) / 2.0);
        const auto near = [](double a, double b) {
            return std::abs(a - b) < 1e-12;
        };
        CHECK(near(midpoint.axes.arousal,
                   (calm.axes.arousal + excited.axes.arousal) / 2.0));
        CHECK(near(midpoint.levers.pitch,
                   (calm.levers.pitch + excited.levers.pitch) / 2.0));
        CHECK(near(midpoint.levers.range,
                   (calm.levers.range + excited.levers.range) / 2.0));
        CHECK(near(midpoint.levers.rate,
                   (calm.levers.rate + excited.levers.rate) / 2.0));
        CHECK(near(midpoint.levers.intensity,
                   (calm.levers.intensity + excited.levers.intensity) / 2.0));
        CHECK(near(midpoint.levers.adduction,
                   (calm.levers.adduction + excited.levers.adduction) / 2.0));

        bool unknownRejected = false;
        try {
            (void)resolveAffectProfile(data, "manic");
        } catch (const std::invalid_argument &error) {
            unknownRejected =
                std::string(error.what()).find("unknown affect style") !=
                std::string::npos;
        }
        CHECK(unknownRejected);

        // A shareable overlay keeps the built-in table and adds new data. Its
        // checksum covers every non-checksum byte with LF line endings.
        std::string overlay =
            "schema 1\nname Bright\nauthor Test\ndescription Test overlay\n"
            "license AGPL-3.0-or-later\nversion 1.0.0\n"
            "map pitch 0.20 0.00 0.00\n"
            "style bright 0.5 0.6 0.7\n"
            "emphasis soft 0.80 0.90 0.95\n";
        overlay += "checksum " + affectFileChecksum(overlay) + "\n";
        const AffectData overlaid =
            parseAffectData(builtinAffectData(), overlay);
        CHECK(overlaid.name == "Bright");
        CHECK(overlaid.styles.count("bright") == 1);
        CHECK(overlaid.styles.count("calm") == 1);
        CHECK(overlaid.emphasis.count("soft") == 1);
        CHECK(std::abs(overlaid.mapping.pitch[0] - 0.2) < 1e-12);
        const AffectProfile bright = resolveAffectProfile(overlaid, "bright");
        CHECK(std::abs(bright.levers.pitch - 1.1) < 1e-12);

        bool checksumRejected = false;
        try {
            (void)parseAffectData(builtinAffectData(), overlay + "# tamper\n");
        } catch (const std::invalid_argument &error) {
            checksumRejected =
                std::string(error.what()).find("affect checksum mismatch") !=
                std::string::npos;
        }
        CHECK(checksumRejected);

        bool lineRejected = false;
        try {
            (void)parseAffectData(builtinAffectData(),
                                  "schema 1\nbogus directive\n");
        } catch (const std::invalid_argument &error) {
            lineRejected = std::string(error.what()).find("affect line 2") !=
                           std::string::npos;
        }
        CHECK(lineRejected);

        const std::string dumped = writeAffectData(data, "calm");
        const AffectData reloaded =
            parseAffectData(builtinAffectData(), dumped);
        CHECK(reloaded.checksum.has_value());
        CHECK(writeAffectData(reloaded, "calm") == dumped);

        // Voice perturbation levers are sweepable data. Their neutral values
        // leave rendering byte-identical; active values stay finite and
        // bounded.
        const Voice base = voicePreset("neutral");
        CHECK(base.jitterCents == 0.0);
        CHECK(base.shimmerDepth == 0.0);
        CHECK(std::abs(voiceParameter(base, "jitter-cents")) < 1e-12);
        CHECK(std::abs(voiceParameter(base, "shimmer")) < 1e-12);
        Voice neutralPerturbation = base;
        setVoiceParameter(neutralPerturbation, "jitter-cents", 0.0);
        setVoiceParameter(neutralPerturbation, "shimmer", 0.0);
        const std::vector<Phoneme> shortPhrase{*findPhoneme("h"),
                                               *findPhoneme("ee")};
        CHECK(Synthesizer(base).render(shortPhrase, 22050) ==
              Synthesizer(neutralPerturbation).render(shortPhrase, 22050));

        Voice perturbed = voicePreset("alto");
        applyAffectStyle(perturbed, excited);
        CHECK(perturbed.pitch > voicePreset("alto").pitch);
        CHECK(perturbed.rate > voicePreset("alto").rate);
        CHECK(perturbed.gain > voicePreset("alto").gain);
        CHECK(perturbed.jitterCents > 0.0);
        CHECK(perturbed.shimmerDepth > 0.0);
        const std::vector<float> excitedAudio =
            Synthesizer(perturbed).render(shortPhrase, 22050);
        CHECK(excitedAudio != Synthesizer(base).render(shortPhrase, 22050));
        CHECK(excitedAudio ==
              Synthesizer(perturbed).render(shortPhrase, 22050));
        for (float sample : excitedAudio) {
            CHECK(std::isfinite(sample));
            CHECK(std::abs(sample) <= 0.9f + 1e-6f);
        }

        // Every corner of the declared PAD range must remain finite and clipped
        // by the existing peak normaliser, including intensity stacking.
        for (double a : {-1.0, 1.0}) {
            for (double v : {-1.0, 1.0}) {
                for (double d : {-1.0, 1.0}) {
                    Voice extreme = voicePreset("neutral");
                    applyAffectStyle(extreme,
                                     resolveAffectProfile(data, "", a, v, d));
                    CHECK(std::isfinite(extreme.pitch));
                    CHECK(std::isfinite(extreme.rate));
                    CHECK(std::isfinite(extreme.gain));
                    CHECK(extreme.rate >= 0.01 && extreme.rate <= 5.0);
                    CHECK(extreme.gain >= 0.001 && extreme.gain <= 4.0);
                    CHECK(extreme.jitterCents <= 50.0);
                    CHECK(extreme.shimmerDepth <= 0.25);
                    const std::vector<float> audio =
                        Synthesizer(extreme).render(shortPhrase, 22050);
                    for (float sample : audio) {
                        CHECK(std::isfinite(sample));
                        CHECK(std::abs(sample) <= 0.9f + 1e-6f);
                    }
                }
            }
        }
    }

    {
        // Word emphasis changes only the marked word. Gaps and surrounding
        // <phoneme> elements keep their exact prosody and provenance.
        const auto frontEnd = [](std::string_view text) {
            std::vector<Phoneme> result;
            if (text == "word") {
                Phoneme word;
                word.name = "word";
                word.kind = PhonemeKind::Voiced;
                word.duration = 0.1;
                word.amplitude = 1.0;
                word.pitchStart = 1.0;
                word.pitchEnd = 1.0;
                Phoneme gap;
                gap.name = "|";
                gap.kind = PhonemeKind::Silence;
                gap.duration = 0.05;
                result = {word, gap};
            }
            return result;
        };
        const auto findWord = [](const std::vector<Phoneme> &all,
                                 std::string_view name) {
            for (const Phoneme &phoneme : all) {
                if (phoneme.name == name) {
                    return phoneme;
                }
            }
            return Phoneme{};
        };
        const std::string document =
            "<speak><phoneme ph=\"h\"/>"
            "<emphasis level=\"strong\">word</emphasis>"
            "<phoneme ph=\"ee\"/></speak>";
        const std::vector<Phoneme> plain =
            phonemizeSsml("<speak><phoneme ph=\"h\"/>word"
                          "<phoneme ph=\"ee\"/></speak>",
                          frontEnd);
        const std::vector<Phoneme> emphasized =
            phonemizeSsml(document, frontEnd);
        const Phoneme plainWord = findWord(plain, "word");
        const Phoneme plainGap = findWord(plain, "|");
        const Phoneme strongWord = findWord(emphasized, "word");
        const Phoneme strongGap = findWord(emphasized, "|");
        CHECK(strongWord.duration > plainWord.duration);
        CHECK(strongWord.pitchStart > plainWord.pitchStart);
        CHECK(strongWord.pitchEnd > plainWord.pitchEnd);
        CHECK(strongWord.amplitude > plainWord.amplitude);
        CHECK(strongWord.source == "style:emphasis");
        CHECK(strongGap == plainGap);
        CHECK(emphasized.front() == plain.front());
        CHECK(emphasized.back() == plain.back());

        const SsmlEmphasisLevels custom = {{"strong", {1.5, 1.6, 1.7}}};
        const Phoneme customWord =
            findWord(phonemizeSsml(document, frontEnd, custom), "word");
        CHECK(customWord.duration > strongWord.duration);
        CHECK(customWord.pitchStart > strongWord.pitchStart);
        CHECK(customWord.amplitude > strongWord.amplitude);

        bool levelRejected = false;
        try {
            (void)phonemizeSsml("<speak><emphasis level="
                                "\"shout\">word</emphasis></speak>",
                                frontEnd);
        } catch (const std::invalid_argument &error) {
            levelRejected =
                std::string(error.what()).find("unsupported emphasis level") !=
                std::string::npos;
        }
        CHECK(levelRejected);
    }

    // #123: built-in song and scale data are versioned, checksummed and
    // loadable without any runtime file dependency.
    const std::string twinkleText(builtinSongData("twinkle"));
    const SongFile twinkle = parseSongFile(twinkleText);
    CHECK(twinkle.schema == kCurrentSongSchema);
    CHECK(twinkle.notes.size() == 14);
    CHECK(twinkle.checksum.has_value());
    CHECK(songFileChecksum(twinkleText) == *twinkle.checksum);
    CHECK(builtinSongData("missing").empty());

    const std::string chromaticText(builtinScaleData("chromatic"));
    const SongScale chromatic = parseSongScale(chromaticText);
    CHECK(chromatic.schema == kCurrentSongScaleSchema);
    CHECK(chromatic.degrees.size() == 12);
    CHECK(chromatic.referenceCents == 900.0);
    CHECK(songScaleChecksum(chromaticText) == *chromatic.checksum);

    const std::string quarterText(builtinScaleData("quarter"));
    const SongScale quarter = parseSongScale(quarterText);
    CHECK(quarter.degrees.size() == 24);
    CHECK(songScaleChecksum(quarterText) == *quarter.checksum);

    // Notes may be named, MIDI, bare MIDI, absolute cents, scale degrees or
    // any of those with an optional +/- cents suffix.
    const SongFile forms = parseSongFile(R"SONG(
schema 1
name pitch-forms
a4 440
scale chromatic
note C4 0.20 ah
note midi:69 0.20 ah
note 69 0.20 ah
note cents:0 0.20 ah
note degree:9@4 0.20 ah
note C4+50c 0.20 ah
)SONG");
    const std::vector<Phoneme> formPhonemes = phonemizeSong(forms, chromatic);
    CHECK(formPhonemes.size() >= 6);
    auto firstOfNote = [](const std::vector<Phoneme> &sequence,
                          std::size_t number) -> const Phoneme * {
        const std::string tag = "song:note:" + std::to_string(number);
        for (const Phoneme &phoneme : sequence) {
            if (phoneme.source == tag) {
                return &phoneme;
            }
        }
        return nullptr;
    };
    // Notes after the first open with a short glide, so the settled target is
    // the last segment carrying that note's tag.
    auto targetOfNote = [](const std::vector<Phoneme> &sequence,
                           std::size_t number) -> const Phoneme * {
        const std::string tag = "song:note:" + std::to_string(number);
        const Phoneme *found = nullptr;
        for (const Phoneme &phoneme : sequence) {
            if (phoneme.source == tag) {
                found = &phoneme;
            }
        }
        return found;
    };
    for (std::size_t i = 2; i <= 5; ++i) {
        const Phoneme *phoneme = targetOfNote(formPhonemes, i);
        CHECK(phoneme != nullptr);
        CHECK(phoneme && phoneme->f0StartHz.has_value());
        CHECK(phoneme && std::abs(*phoneme->f0StartHz - 440.0) < 0.01);
    }
    const Phoneme *c4 = targetOfNote(formPhonemes, 1);
    const Phoneme *semitoneUp = targetOfNote(formPhonemes, 6);
    CHECK(c4 && std::abs(*c4->f0StartHz - 261.6255653) < 0.01);
    CHECK(semitoneUp && std::abs(*semitoneUp->f0StartHz - 269.2961810) < 0.01);

    // A4 override and a data-loaded non-12-tone scale both resolve in the
    // same absolute-Hz parameter domain.
    const std::vector<Phoneme> retunedPhonemes =
        phonemizeSong(twinkle, chromatic, 432.0);
    const Phoneme *retuned = firstOfNote(retunedPhonemes, 1);
    CHECK(retuned && std::abs(*retuned->f0StartHz - 256.8695363) < 0.01);
    const SongFile quarterSong = parseSongFile(R"SONG(
schema 1
name quarter-demo
a4 440
scale quarter
note Aq4 0.20 ah
note degree:18@4 0.20 ah
)SONG");
    const std::vector<Phoneme> quarterPhonemes =
        phonemizeSong(quarterSong, quarter);
    const Phoneme *quarterFlat = firstOfNote(quarterPhonemes, 1);
    const Phoneme *quarterA = targetOfNote(quarterPhonemes, 2);
    CHECK(quarterFlat && std::abs(*quarterFlat->f0StartHz - 427.474) < 0.01);
    CHECK(quarterA && std::abs(*quarterA->f0StartHz - 440.0) < 0.01);

    // Per-note vibrato is exactly zero through the delay, then fades in. A
    // note-controlled segment suppresses the voice's global vibrato/jitter so
    // no modulation can leak into the onset.
    NoteF0Control control;
    control.noteOffsetSeconds = 0.0;
    control.vibratoRateHz = 5.5;
    control.vibratoExtentCents = 12.0;
    control.vibratoDelaySeconds = 0.20;
    control.vibratoFadeSeconds = 0.25;
    CHECK(songVibratoCents(control, 0.0) == 0.0);
    CHECK(songVibratoCents(control, 0.10) == 0.0);
    CHECK(songVibratoCents(control, 0.20) == 0.0);
    CHECK(songVibratoCents(control, 0.21) != 0.0);

    Voice ornate = voicePreset("neutral");
    ornate.vibratoRate = 7.0;
    ornate.vibratoDepth = 100.0;
    ornate.jitterCents = 40.0;
    Phoneme note = *findPhoneme("ah");
    note.duration = 1.0;
    note.f0StartHz = 440.0;
    note.f0EndHz = 440.0;
    note.noteF0 = control;
    const double beforeDelay =
        detail::fundamentalAt(ornate, note, 100, 0, 1000, 1000, 1000, false);
    const double afterDelay =
        detail::fundamentalAt(ornate, note, 300, 0, 1000, 1000, 1000, false);
    CHECK(std::abs(beforeDelay - 440.0) < 1e-9);
    CHECK(std::abs(afterDelay - 440.0) > 1e-6);

    // A legato knee is short and a portamento is an audible log-domain glide.
    const SongFile transitions = parseSongFile(R"SONG(
schema 1
name transitions
a4 440
scale chromatic
note C4 0.35 ah transition=legato glide=0.004s
note G4 0.35 ah transition=portamento glide=0.120s
)SONG");
    const std::vector<Phoneme> transitionPhonemes =
        phonemizeSong(transitions, chromatic);
    const Phoneme *legato = firstOfNote(transitionPhonemes, 1);
    const Phoneme *glide = firstOfNote(transitionPhonemes, 2);
    CHECK(legato && legato->f0StartHz.has_value());
    CHECK(glide && glide->f0StartHz.has_value() && glide->f0EndHz.has_value());
    CHECK(glide && std::abs(*glide->f0StartHz - 261.6255653) < 0.01);
    CHECK(glide && std::abs(*glide->f0EndHz - 391.9954360) < 0.01);
    CHECK(glide->duration >= 0.10);

    // One phase accumulator carries the waveform through note boundaries: a
    // continued render split at a transition is byte-identical to a whole one.
    std::size_t boundary = 0;
    while (boundary < transitionPhonemes.size() &&
           transitionPhonemes[boundary].source != "song:note:2") {
        ++boundary;
    }
    CHECK(boundary > 0 && boundary < transitionPhonemes.size());
    if (boundary > 0 && boundary < transitionPhonemes.size()) {
        const std::vector<Phoneme> head(transitionPhonemes.begin(),
                                        transitionPhonemes.begin() + boundary);
        const std::vector<Phoneme> tail(transitionPhonemes.begin() + boundary,
                                        transitionPhonemes.end());
        const Voice voice = voicePreset("neutral");
        const Synthesizer synthesizer(voice);
        const RenderResult first = synthesizer.renderContinued(head, 8000, {});
        const RenderResult joined =
            synthesizer.renderContinued(tail, 8000, first.state);
        const std::vector<float> whole =
            synthesizer.render(transitionPhonemes, 8000);
        CHECK(joined.samples.size() == whole.size());
        CHECK(std::equal(joined.samples.begin(), joined.samples.end(),
                         whole.begin(), [](float a, float b) {
                             return std::memcmp(&a, &b, sizeof(float)) == 0;
                         }));
        const RenderResult reference =
            synthesizer.renderContinued(transitionPhonemes, 8000, {});
        CHECK(joined.state.phase == reference.state.phase);
        CHECK(joined.state.totalSamples == reference.state.totalSamples);
        CHECK(joined.state.phonemes.size() == reference.state.phonemes.size());
    }

    // Same data, same bytes: no clock, PRNG or global state enters a song.
    const std::vector<float> songRender =
        Synthesizer(voicePreset("neutral")).render(twinklePhonemes(), 22050);
    const std::vector<float> songRenderAgain =
        Synthesizer(voicePreset("neutral")).render(twinklePhonemes(), 22050);
    CHECK(songRender.size() == songRenderAgain.size());
    CHECK(std::equal(songRender.begin(), songRender.end(),
                     songRenderAgain.begin(), [](float a, float b) {
                         return std::memcmp(&a, &b, sizeof(float)) == 0;
                     }));

    // Parser failures are instructive and never accept malformed versions.
    bool badSchema = false;
    try {
        (void)parseSongFile("schema 9\nname bad\nnote C4 0.1 ah\n");
    } catch (const std::invalid_argument &error) {
        badSchema = std::string(error.what()).find("unsupported song schema") !=
                    std::string::npos;
    }
    CHECK(badSchema);
    bool badDegree = false;
    try {
        (void)parseSongScale("schema 1\nname bad\ndegree C not_a_number\n");
    } catch (const std::invalid_argument &error) {
        badDegree = std::string(error.what()).find("invalid number") !=
                    std::string::npos;
    }
    CHECK(badDegree);

    // Ordinary phonemes still carry no note control, so the speech path is
    // untouched. The checksum corpus guards the exact default PCM bytes.
    CHECK(!findPhoneme("ah")->noteF0.has_value());

    // The English paths remain byte-for-byte unchanged.
    CHECK(names(phonemize("Hello, world.")) == hello);

    // ---- #101: data-defined frication envelopes -------------------------
    const FricationData builtin = parseFricationData(builtinFricationData());
    CHECK(builtin.schema == kCurrentFricationSchema);
    CHECK(builtin.checksum.has_value());
    CHECK(*builtin.checksum == fricationFileChecksum(builtinFricationData()));
    CHECK(builtin.profiles.size() >= 18);
    CHECK(builtin.defaultPartials >= 12);
    CHECK(builtin.defaultPartials <= kMaxFricationPartials);
    const FricationProfile *sProfile = findFricationProfile(builtin, "s");
    const FricationProfile *shProfile = findFricationProfile(builtin, "sh");
    const FricationProfile *fProfile = findFricationProfile(builtin, "f");
    CHECK(sProfile != nullptr);
    CHECK(shProfile != nullptr);
    CHECK(fProfile != nullptr);
    // Place-specific peaks really are data, and distinct from each other.
    CHECK(sProfile->centerHz > shProfile->centerHz);
    CHECK(fProfile->bandwidthHz > sProfile->bandwidthHz);
    CHECK(findFricationProfile(builtin, "S") == nullptr);
    CHECK(!fricationProfileNames(builtin).empty());

    // Serialisation round-trips through the documented checksum convention.
    const std::string dumped = writeFricationData(builtin, "s");
    const FricationData roundTrip = parseFricationData(dumped);
    CHECK(roundTrip.profiles.size() == 1);
    CHECK(roundTrip.profiles.at("s") == *sProfile);
    CHECK(roundTrip.defaultPartials == builtin.defaultPartials);

    // A user file layers over the built-in table without replacing it.
    const std::string overlay =
        "schema 1\nname unit-overlay\nprofile s 96 6000 2000 5000 1800\n" +
        std::string("checksum ") +
        fricationFileChecksum(
            "schema 1\nname unit-overlay\nprofile s 96 6000 2000 5000 1800\n") +
        "\n";
    const FricationData layered =
        parseFricationData(builtinFricationData(), overlay);
    CHECK(layered.profiles.size() == builtin.profiles.size());
    CHECK(layered.profiles.at("s").partials == 96);
    CHECK(layered.profiles.at("sh").partials == shProfile->partials);
    CHECK(layered.name == "unit-overlay");

    bool badFricationSchema = false;
    try {
        (void)parseFricationData("schema 99\n");
    } catch (const std::invalid_argument &error) {
        badFricationSchema =
            std::string(error.what()).find("unsupported frication schema") !=
            std::string::npos;
    }
    CHECK(badFricationSchema);
    bool badFricationChecksum = false;
    try {
        (void)parseFricationData(builtinFricationData(),
                                 "profile s 64 5400 2400 4400 3200\nchecksum "
                                 "000000000000000000000000000000000000000000000"
                                 "0000000000000000000\n");
    } catch (const std::invalid_argument &error) {
        badFricationChecksum =
            std::string(error.what()).find("checksum mismatch") !=
            std::string::npos;
    }
    CHECK(badFricationChecksum);

    // Controls attach only to fricatives with a matching profile.
    std::vector<Phoneme> fricatives = {*findPhoneme("s"), *findPhoneme("ee"),
                                       *findPhoneme("sh")};
    CHECK(!fricatives[0].frication.has_value());
    CHECK(applyFrication(fricatives, builtin) == 2);
    CHECK(fricatives[0].frication.has_value());
    CHECK(!fricatives[1].frication.has_value());
    CHECK(fricatives[2].frication.has_value());
    CHECK(fricatives[0].frication->partials == sProfile->partials);
    CHECK(fricatives[0].frication->centerHz == sProfile->centerHz);

    // Same data, same bytes: the shaped path is still pure deterministic sines.
    const Voice fricVoice = voicePreset("neutral");
    const Synthesizer synth(fricVoice);
    const auto renderFricative = [&](std::string_view name) {
        std::vector<Phoneme> phrase = {*findPhoneme(name), *findPhoneme("ee")};
        CHECK(applyFrication(phrase, builtin) == 1);
        return synth.render(phrase, 22050);
    };
    const std::vector<float> sRender = renderFricative("s");
    const std::vector<float> sRenderAgain = renderFricative("s");
    const std::vector<float> shRender = renderFricative("sh");
    const std::vector<float> fRender = renderFricative("f");
    CHECK(sRender.size() == sRenderAgain.size());
    CHECK(std::equal(sRender.begin(), sRender.end(), sRenderAgain.begin(),
                     [](float a, float b) {
                         return std::memcmp(&a, &b, sizeof(float)) == 0;
                     }));
    CHECK(!std::equal(sRender.begin(), sRender.end(), shRender.begin()));
    CHECK(!std::equal(sRender.begin(), sRender.end(), fRender.begin()));
    CHECK(!std::equal(shRender.begin(), shRender.end(), fRender.begin()));
    // No control means the fixed legacy stack, so the speech path is identical.
    const std::vector<Phoneme> plain = {*findPhoneme("s"), *findPhoneme("ee")};
    CHECK(synth.render(plain, 22050) != sRender);
    CHECK(names(phonemize("Hello, world.")) == hello);

    if (failures == 0) {
        std::cout << "All formant tests passed\n";
    }
    return failures == 0 ? 0 : 1;
}
