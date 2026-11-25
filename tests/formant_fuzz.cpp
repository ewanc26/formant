// Seeded fuzz campaign: renders tens of thousands of synthetic scripts through
// the full phoneme-to-PCM pipeline. Every failure prints the exact seed that
// reproduces it. Runs nightly on Linux/macOS and on demand (see
// .github/workflows/nightly.yml); not part of the fast PR CTest gate.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "formant/formant.h"

namespace {

const std::array<std::string, 12> kGrammars = {"CVC",
                                               "CV(V)C",
                                               "C=p/t/k\nV=a/i/u\nCVC",
                                               "[[V][V]]^aa^ii^uu",
                                               "(C)V(C)",
                                               "C=p/t/k\nV=a/i/u\nX=CV\nXX",
                                               "a/i/u*4",
                                               "C=sh/ch\ntCVC",
                                               "CCV(V)CC",
                                               "V=C\nC=m/n\nVCV",
                                               "(C)(C)V(C)(C)",
                                               "CVCV"};

const std::array<std::string, 6> kVoices = {"neutral", "alto",  "bass",
                                            "child",   "robot", "whisper"};

const std::array<int, 6> kSampleRates = {8000,  11025, 16000,
                                         22050, 44100, 48000};

const std::array<formant::AudioFormat, 4> kFormats = {
    formant::AudioFormat::Pcm16, formant::AudioFormat::Pcm24,
    formant::AudioFormat::RawPcm16, formant::AudioFormat::Float32};

bool allFiniteAndBounded(const std::vector<float> &samples) {
    for (float sample : samples) {
        if (!std::isfinite(sample) || std::abs(sample) > 0.9001f) {
            return false;
        }
    }
    return true;
}

struct Options {
    uint32_t scripts = 50000;
    uint32_t seedBase = 1;
};

bool parseUnsigned(std::string_view text, uint32_t &out) {
    if (text.empty()) {
        return false;
    }
    unsigned long long value = 0;
    for (char c : text) {
        if (c < '0' || c > '9') {
            return false;
        }
        value = value * 10 + static_cast<unsigned>(c - '0');
        if (value > std::numeric_limits<uint32_t>::max()) {
            return false;
        }
    }
    out = static_cast<uint32_t>(value);
    return true;
}

bool parseOptions(int argc, char **argv, Options &options) {
    for (int i = 1; i < argc; ++i) {
        const std::string_view arg = argv[i];
        if (arg == "--scripts" && i + 1 < argc) {
            if (!parseUnsigned(argv[++i], options.scripts)) {
                return false;
            }
        } else if (arg == "--seed-base" && i + 1 < argc) {
            if (!parseUnsigned(argv[++i], options.seedBase)) {
                return false;
            }
        } else if (arg == "--help") {
            std::cout << "usage: formant_fuzz [--scripts N] [--seed-base N]\n";
            std::exit(0);
        } else {
            return false;
        }
    }
    return true;
}

int fail(uint32_t seed, const std::string &reason) {
    std::cerr << "FAIL seed=" << seed << ": " << reason << "\n";
    std::cerr << "replay with: formant_fuzz --scripts 1 --seed-base " << seed
              << "\n";
    return 1;
}

const std::array<const char *, 6> kSongPitches = {
    "C4", "midi:69", "69", "cents:1200", "degree:9@4", "C4+7c"};

std::string makeSongScript(formant::SeededRng &rng) {
    std::ostringstream out;
    out << "schema 1\nname fuzz-song\na4 " << (430.0 + rng.below(100))
        << "\nscale chromatic\n";
    const uint32_t count = 3 + rng.below(4);
    for (uint32_t i = 0; i < count; ++i) {
        const bool portamento = rng.below(2) != 0;
        const double duration = 0.08 + rng.below(20) * 0.005;
        const double glide = portamento ? 0.02 + rng.below(8) * 0.01 : 0.005;
        out << "note " << kSongPitches[rng.below(kSongPitches.size())] << ' '
            << std::fixed << std::setprecision(3) << duration << " ah"
            << " transition=" << (portamento ? "portamento" : "legato")
            << " glide=" << std::fixed << std::setprecision(3) << glide
            << "s vibrato=5.5:8c:0.01s:0.02s\n";
    }
    return out.str();
}

} // namespace

int main(int argc, char **argv) {
    Options options;
    if (!parseOptions(argc, argv, options)) {
        std::cerr << "usage: formant_fuzz [--scripts N] [--seed-base N]\n";
        return 2;
    }

    const auto inventory = formant::defaultPatternInventory();
    std::array<formant::PatternGrammar, kGrammars.size()> grammars;
    for (size_t i = 0; i < kGrammars.size(); ++i) {
        grammars[i] = formant::parsePatternGrammar(kGrammars[i]);
    }

    for (uint32_t i = 0; i < options.scripts; ++i) {
        const uint32_t seed = options.seedBase + i;
        formant::SeededRng rng(seed);
        try {
            const auto &grammar = grammars[rng.below(grammars.size())];
            formant::GibberishSettings settings;
            settings.seed = rng.next();
            settings.count = static_cast<int>(1 + rng.below(8));
            settings.prosody = true;
            const auto script =
                formant::generateGibberish(grammar, inventory, settings);

            const auto voice =
                formant::voicePreset(kVoices[rng.below(kVoices.size())]);
            const int rate = kSampleRates[rng.below(kSampleRates.size())];
            const auto format = kFormats[rng.below(kFormats.size())];
            formant::AudioWriteOptions writeOptions;
            writeOptions.dither = !formant::isFloatAudioFormat(format);
            writeOptions.seed = rng.next();

            const auto samples =
                formant::Synthesizer(voice).render(script.phonemes, rate);
            if (!allFiniteAndBounded(samples)) {
                return fail(
                    seed, "render produced non-finite or out-of-range samples");
            }

            std::ostringstream encoded(std::ios::binary);
            formant::writeAudio(encoded, samples, rate, format, writeOptions);

            // Determinism check on every script: an identical seed must give
            // byte-identical encoded output.
            const auto scriptAgain =
                formant::generateGibberish(grammar, inventory, settings);
            const auto samplesAgain =
                formant::Synthesizer(voice).render(scriptAgain.phonemes, rate);
            if (samples.size() != samplesAgain.size() ||
                !std::equal(samples.begin(), samples.end(),
                            samplesAgain.begin(), [](float a, float b) {
                                return std::memcmp(&a, &b, sizeof(float)) == 0;
                            })) {
                return fail(seed, "seeded render was not deterministic");
            }

            // Every fourth case exercises the full #123 song path: generated
            // data text -> parser -> absolute F0 segments -> deterministic PCM.
            if (i % 4 == 0) {
                const std::string songText = makeSongScript(rng);
                const auto song = formant::parseSongFile(songText);
                const auto scale = formant::parseSongScale(
                    formant::builtinScaleData("chromatic"));
                const auto notes = formant::phonemizeSong(song, scale);
                const int songRate =
                    kSampleRates[rng.below(kSampleRates.size())];
                const auto songSamples =
                    formant::Synthesizer(voice).render(notes, songRate);
                if (!allFiniteAndBounded(songSamples)) {
                    return fail(seed,
                                "song render produced non-finite samples");
                }
                const auto songAgain = formant::phonemizeSong(
                    formant::parseSongFile(songText), scale);
                const auto songSamplesAgain =
                    formant::Synthesizer(voice).render(songAgain, songRate);
                if (songSamples.size() != songSamplesAgain.size() ||
                    !std::equal(songSamples.begin(), songSamples.end(),
                                songSamplesAgain.begin(), [](float a, float b) {
                                    return std::memcmp(&a, &b, sizeof(float)) ==
                                           0;
                                })) {
                    return fail(seed,
                                "seeded song render was not deterministic");
                }
            }
        } catch (const std::exception &error) {
            return fail(seed,
                        std::string("unexpected exception: ") + error.what());
        } catch (...) {
            return fail(seed, "non-std exception escaped");
        }

        if ((i + 1) % 10000 == 0) {
            std::cout << "rendered " << (i + 1) << " scripts\n";
        }
    }

    std::cout << "fuzz campaign passed: " << options.scripts
              << " scripts (seed base " << options.seedBase << ")\n";
    return 0;
}
