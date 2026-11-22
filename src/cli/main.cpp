#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "formant/formant.h"
#include "formant/version.h"

namespace {

constexpr const char *kVersion = FORMANT_VERSION;

using formant::Voice;
using formant::voicePreset;
using formant::voicePresetNames;

void printHelp() {
    std::cout << R"(formant - a customisable sine-wave voice engine

Usage:
  formant [options]

Options:
  --text "words"          Text to speak (default: "Hello, world.")
  --stdin                 Read the text to speak from standard input
  --phonemes "[h ee l o]" Speak a bracketed phoneme sequence instead of text
  --ipa "ˈhəˈloʊ"         Speak a UTF-8 IPA transcription instead of text
  --xsampa "h@"           Speak an X-SAMPA transcription instead of text
  --voice NAME            Built-in voice: neutral, alto, bass, child, robot, whisper
  --list-voices           List available voices and exit
  --list-phonemes         List available phonemes and exit
  --list-ipa              List recognised IPA symbols and exit
  --output FILE           WAV output path (default: output.wav)
  --sample-rate HZ        PCM sample rate (default: 44100, minimum: 8000)
  --pitch HZ              Fundamental frequency; 0 disables voicing
  --rate FACTOR           Speaking-rate multiplier (default: 1)
  --formant-scale FACTOR  Vocal-tract length multiplier (default: 1)
  --vibrato-rate HZ       Vibrato speed in Hz
  --vibrato-depth CENTS   Vibrato depth in cents
  --breathiness 0..1      Sine-noise mixed into voiced sounds
  --harmonics N           Number of sine partials in the glottal source
  --gain FACTOR           Output gain before normalisation
  --attack SECONDS        Phoneme attack time
  --release SECONDS       Phoneme release time
  --declination 0..1      Pitch drift across a phrase
  --version               Print the version and exit
  --help, -h              Show this help and exit

Every option with a value also accepts --option=value. Audio is rendered
deterministically as floating-point PCM and written as 16-bit mono WAV.

Example:
  formant --text "Hello, world." --voice alto --pitch 190 --output hello.wav
)";
}

void listVoices() {
    for (const std::string &name : voicePresetNames()) {
        const Voice voice = voicePreset(name);
        std::cout << name << "\t" << voice.pitch << " Hz\tformants x"
                  << voice.formantScale << "\n";
    }
}

void listPhonemes() {
    for (const std::string &name : formant::phonemeNames()) {
        std::cout << name << "\n";
    }
}

void listIpa() {
    for (const std::string &symbol : formant::ipaSymbols()) {
        std::cout << symbol << "\n";
    }
}

double number(const std::string &flag, const std::string &value) {
    try {
        size_t used = 0;
        const double result = std::stod(value, &used);
        if (used != value.size()) {
            throw std::invalid_argument("trailing characters");
        }
        if (!std::isfinite(result)) {
            throw std::invalid_argument("non-finite value");
        }
        return result;
    } catch (const std::exception &) {
        throw std::invalid_argument("invalid number for " + flag + ": " +
                                    value);
    }
}

int integer(const std::string &flag, const std::string &value) {
    const double parsed = number(flag, value);
    if (parsed != static_cast<double>(static_cast<int>(parsed))) {
        throw std::invalid_argument("expected an integer for " + flag + ": " +
                                    value);
    }
    return static_cast<int>(parsed);
}

struct Options {
    Voice voice = voicePreset("neutral");
    std::string text = "Hello, world.";
    std::string phonemes;
    std::string ipa;
    std::string xsampa;
    std::string output = "output.wav";
    int sampleRate = 44100;
    bool usePhonemes = false;
    bool useIpa = false;
    bool useXsampa = false;
    bool useStdin = false;
};

// Accepts both "--flag value" and "--flag=value".
std::string takeValue(int &index, int argc, char **argv,
                      const std::string &flag) {
    const std::string prefix = flag + "=";
    const std::string arg = argv[index];
    if (arg.rfind(prefix, 0) == 0) {
        return arg.substr(prefix.size());
    }
    if (index + 1 >= argc) {
        throw std::invalid_argument(flag + " needs a value");
    }
    return argv[++index];
}

Options parseOptions(int argc, char **argv) {
    Options options;
    const std::string doubleFlags[] = {
        "--pitch",         "--rate",        "--formant-scale", "--vibrato-rate",
        "--vibrato-depth", "--breathiness", "--gain",          "--attack",
        "--release",       "--declination",
    };

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];

        if (arg == "--help" || arg == "-h") {
            printHelp();
            std::exit(0);
        }
        if (arg == "--version") {
            std::cout << kVersion << "\n";
            std::exit(0);
        }
        if (arg == "--list-voices") {
            listVoices();
            std::exit(0);
        }
        if (arg == "--list-phonemes") {
            listPhonemes();
            std::exit(0);
        }
        if (arg == "--list-ipa") {
            listIpa();
            std::exit(0);
        }
        if (arg == "--stdin") {
            options.useStdin = true;
            continue;
        }

        bool handled = false;
        for (const std::string &flag : doubleFlags) {
            if (arg != flag && arg.rfind(flag + "=", 0) != 0) {
                continue;
            }
            const std::string value = takeValue(i, argc, argv, flag);
            const double parsed = number(flag, value);
            if (flag == "--pitch")
                options.voice.pitch = parsed;
            else if (flag == "--rate")
                options.voice.rate = parsed;
            else if (flag == "--formant-scale")
                options.voice.formantScale = parsed;
            else if (flag == "--vibrato-rate")
                options.voice.vibratoRate = parsed;
            else if (flag == "--vibrato-depth")
                options.voice.vibratoDepth = parsed;
            else if (flag == "--breathiness")
                options.voice.breathiness = parsed;
            else if (flag == "--gain")
                options.voice.gain = parsed;
            else if (flag == "--attack")
                options.voice.attack = parsed;
            else if (flag == "--release")
                options.voice.release = parsed;
            else if (flag == "--declination")
                options.voice.declination = parsed;
            handled = true;
            break;
        }
        if (handled) {
            continue;
        }

        if (arg == "--text" || arg.rfind("--text=", 0) == 0) {
            options.text = takeValue(i, argc, argv, "--text");
        } else if (arg == "--phonemes" || arg.rfind("--phonemes=", 0) == 0) {
            options.phonemes = takeValue(i, argc, argv, "--phonemes");
            options.usePhonemes = true;
        } else if (arg == "--ipa" || arg.rfind("--ipa=", 0) == 0) {
            options.ipa = takeValue(i, argc, argv, "--ipa");
            options.useIpa = true;
        } else if (arg == "--xsampa" || arg.rfind("--xsampa=", 0) == 0) {
            options.xsampa = takeValue(i, argc, argv, "--xsampa");
            options.useXsampa = true;
        } else if (arg == "--voice" || arg.rfind("--voice=", 0) == 0) {
            options.voice = voicePreset(takeValue(i, argc, argv, "--voice"));
        } else if (arg == "--output" || arg.rfind("--output=", 0) == 0) {
            options.output = takeValue(i, argc, argv, "--output");
        } else if (arg == "--sample-rate" ||
                   arg.rfind("--sample-rate=", 0) == 0) {
            options.sampleRate = integer(
                "--sample-rate", takeValue(i, argc, argv, "--sample-rate"));
        } else if (arg == "--harmonics" || arg.rfind("--harmonics=", 0) == 0) {
            options.voice.harmonics =
                integer("--harmonics", takeValue(i, argc, argv, "--harmonics"));
        } else {
            throw std::invalid_argument("unknown option: " + arg);
        }
    }
    return options;
}

} // namespace

int main(int argc, char **argv) {
    try {
        const Options options = parseOptions(argc, argv);

        std::string text = options.text;
        if (options.useStdin) {
            std::ostringstream buffer;
            buffer << std::cin.rdbuf();
            text = buffer.str();
        }

        if (options.voice.rate <= 0.0 || options.voice.gain <= 0.0 ||
            options.voice.pitch < 0.0 || options.voice.harmonics < 0) {
            throw std::invalid_argument("rate and gain must be positive; pitch "
                                        "and harmonics must not be negative");
        }

        const int inputModes = static_cast<int>(options.usePhonemes) +
                               static_cast<int>(options.useIpa) +
                               static_cast<int>(options.useXsampa);
        if (inputModes > 1) {
            throw std::invalid_argument("use only one of --phonemes, --ipa or "
                                        "--xsampa (each overrides --text and "
                                        "--stdin)");
        }
        const std::vector<formant::Phoneme> phonemes =
            options.usePhonemes ? formant::parsePhonemes(options.phonemes)
            : options.useIpa    ? formant::parseIpa(options.ipa)
            : options.useXsampa ? formant::parseXsampa(options.xsampa)
                                : formant::phonemize(text);
        const std::vector<float> samples =
            formant::Synthesizer(options.voice)
                .render(phonemes, options.sampleRate);
        formant::writeWav(options.output, samples, options.sampleRate);

        std::cout << "Wrote " << samples.size() << " samples to "
                  << options.output << " (" << options.voice.name << ", "
                  << options.sampleRate << " Hz)\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "formant: " << error.what() << "\n";
        return 1;
    }
}
