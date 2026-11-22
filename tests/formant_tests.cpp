#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
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

uint32_t littleEndian32(const std::vector<uint8_t> &bytes, size_t offset) {
    return static_cast<uint32_t>(bytes[offset]) |
           (static_cast<uint32_t>(bytes[offset + 1]) << 8) |
           (static_cast<uint32_t>(bytes[offset + 2]) << 16) |
           (static_cast<uint32_t>(bytes[offset + 3]) << 24);
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
          std::vector<std::string>({"b", "o", "m", "s"}));
    // A medial mb cluster keeps its /b/ (number, timber, combat).
    CHECK(contains(names(phonemize("number")), "b"));
    CHECK(contains(names(phonemize("timber")), "b"));
    CHECK(contains(names(phonemize("combat")), "b"));

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
          std::vector<std::string>({"s", "i", "n", "s"}));
    CHECK(names(phonemize("design")).back() == "n");
    CHECK(names(phonemize("campaign")).back() == "n");
    CHECK(names(phonemize("campaigns")).back() == "s");
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

    // The English paths remain byte-for-byte unchanged.
    CHECK(names(phonemize("Hello, world.")) == hello);

    if (failures == 0) {
        std::cout << "All formant tests passed\n";
    }
    return failures == 0 ? 0 : 1;
}
