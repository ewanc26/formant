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

    if (failures == 0) {
        std::cout << "All formant tests passed\n";
    }
    return failures == 0 ? 0 : 1;
}
