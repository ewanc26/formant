#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <iostream>
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

    // Suffix rules apply to words outside the small lexicon.
    const std::vector<std::string> nation = names(phonemize("nation"));
    CHECK(contains(nation, "sh"));
    CHECK(contains(nation, "ah"));
    CHECK(contains(nation, "n"));
    const std::vector<std::string> singing = names(phonemize("singing"));
    CHECK(contains(singing, "ng"));
    CHECK(singing.size() >= 3);

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

    const Phoneme *moving = findPhoneme("ay");
    CHECK(moving != nullptr);
    if (moving) {
        Phoneme staticVersion = *moving;
        staticVersion.hasEndBands = false;
        CHECK(Synthesizer(voice).render({*moving}, 22050) !=
              Synthesizer(voice).render({staticVersion}, 22050));
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
    CHECK(parseIpa("p\u02BC").front().kind == PhonemeKind::Plosive);
    CHECK(parseIpa("n\u0303").front().bands[1].amplitude <
          parseIpa("n").front().bands[1].amplitude);
    CHECK(parseIpa("e\u031E").front().bands[0].frequency >
          parseIpa("e").front().bands[0].frequency);

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
