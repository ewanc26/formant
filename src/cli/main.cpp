#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <functional>
#include <iostream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "formant/formant.h"
#include "formant/version.h"

namespace {

constexpr const char *kVersion = FORMANT_VERSION;

using formant::Language;
using formant::languageFromTag;
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
  --ssml-file FILE        Speak an SSML document (a supported subset)
  --phonemes "[h ee l o]" Speak a bracketed phoneme sequence instead of text
  --ipa "ˈhəˈloʊ"         Speak a UTF-8 IPA transcription instead of text
  --xsampa "h@"           Speak an X-SAMPA transcription instead of text
  --lang TAG              Text language: en (English), es (Spanish)
  --lang-file FILE        Speak text with a user-defined language definition
  --lexicon FILE          Apply user pronunciation entries and homograph rules
  --trace                 Print rewrite-rule applications (with --lang-file)
  --voice NAME            Built-in voice: neutral, alto, bass, child, robot, whisper
  --list-voices           List available voices and exit
  --list-phonemes         List available phonemes and exit
  --list-ipa              List recognised IPA symbols and exit
  --list-languages        List available text languages and exit
  --output FILE           Audio output path, or - for stdout (default: output.wav)
  --format NAME           wav16 (default), wav24, wav32f, raw16, raw24, raw32f
  --dither SEED           Deterministic TPDF dither for 16/24-bit integers
  --timing FILE           Write word/phoneme timing (.json, .lrc, .srt, .vtt)
  --visemes FILE          Write a lip-sync viseme track as JSON
  --sample-rate HZ        PCM sample rate (default: 44100, minimum: 8000)
  --pitch HZ              Fundamental frequency; 0 disables voicing
  --pitch-shift CENTS     Relative F0 shift in cents (1200 = one octave)
  --rate FACTOR           Speaking-rate multiplier (default: 1)
  --formant-scale FACTOR  Formant frequency/bandwidth multiplier (default: 1)
  --formant-ratio RATIO   Relative formant multiplier, parameter-domain shift
  --vocal-tract-scale N   Physical tract length; formants divide by N
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
deterministically as floating-point PCM. Use --output - to stream audio to
stdout; status and trace messages then go to stderr so stdout stays pure PCM.

Example:
  formant --text "Hello, world." --voice alto --pitch 190 --output hello.wav
  formant --ipa "ˈhəˈloʊ" --format raw16 --output - | aplay -f S16_LE -r 44100
)";
}

void listVoices() {
    for (const std::string &name : voicePresetNames()) {
        const Voice voice = voicePreset(name);
        std::cout << name << "\t" << voice.pitch << " Hz\tformants x"
                  << voice.formantScale << "\n";
    }
}

void listLanguages() {
    for (const std::string &tag : formant::languageTags()) {
        const Language language = languageFromTag(tag);
        std::cout << tag << "\t" << formant::languageName(language) << "\n";
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
    // Use strtod rather than stod: stod throws out_of_range whenever errno is
    // ERANGE, which strtod also sets for underflow. Finite subnormal doubles
    // such as 1e-310 are valid options, so only the parsed value and full
    // consumption decide acceptance. Overflow yields infinity and is rejected
    // by the isfinite check below.
    if (value.empty()) {
        throw std::invalid_argument("invalid number for " + flag + ": " +
                                    value);
    }
    char *end = nullptr;
    const double result = std::strtod(value.c_str(), &end);
    if (end != value.c_str() + value.size() || !std::isfinite(result)) {
        throw std::invalid_argument("invalid number for " + flag + ": " +
                                    value);
    }
    return result;
}

int integer(const std::string &flag, const std::string &value) {
    const double parsed = number(flag, value);
    // Check the range before casting: converting an out-of-range double to int
    // is undefined behaviour, even when the value is later rejected.
    if (parsed < static_cast<double>(std::numeric_limits<int>::min()) ||
        parsed > static_cast<double>(std::numeric_limits<int>::max()) ||
        parsed != std::trunc(parsed)) {
        throw std::invalid_argument("expected an integer for " + flag + ": " +
                                    value);
    }
    return static_cast<int>(parsed);
}

formant::AudioFormat audioFormat(const std::string &name) {
    if (name == "wav16") {
        return formant::AudioFormat::Pcm16;
    }
    if (name == "wav24") {
        return formant::AudioFormat::Pcm24;
    }
    if (name == "wav32f") {
        return formant::AudioFormat::Float32;
    }
    if (name == "raw16") {
        return formant::AudioFormat::RawPcm16;
    }
    if (name == "raw24") {
        return formant::AudioFormat::RawPcm24;
    }
    if (name == "raw32f") {
        return formant::AudioFormat::RawFloat32;
    }
    throw std::invalid_argument("unknown audio format: " + name +
                                " (use wav16, wav24, wav32f, raw16, raw24 or "
                                "raw32f)");
}

uint32_t ditherSeed(const std::string &value) {
    if (value.empty() || value[0] == '-') {
        throw std::invalid_argument("expected an unsigned 32-bit seed for "
                                    "--dither: " +
                                    value);
    }
    char *end = nullptr;
    errno = 0;
    const unsigned long long parsed = std::strtoull(value.c_str(), &end, 10);
    if (errno == ERANGE || end != value.c_str() + value.size() ||
        parsed > std::numeric_limits<uint32_t>::max()) {
        throw std::invalid_argument("expected an unsigned 32-bit seed for "
                                    "--dither: " +
                                    value);
    }
    return static_cast<uint32_t>(parsed);
}

struct Options {
    Voice voice = voicePreset("neutral");
    Language language = Language::English;
    std::string text = "Hello, world.";
    std::string phonemes;
    std::string ipa;
    std::string xsampa;
    std::string languageFile;
    std::string lexiconFile;
    std::string ssmlFile;
    std::string output = "output.wav";
    formant::AudioFormat format = formant::AudioFormat::Pcm16;
    formant::AudioWriteOptions audioOptions;
    std::string timingFile;
    std::string visemesFile;
    int sampleRate = 44100;
    bool usePhonemes = false;
    bool useIpa = false;
    bool useXsampa = false;
    bool useStdin = false;
    bool useLanguage = false;
    bool useLanguageFile = false;
    bool useLexicon = false;
    bool useSsml = false;
    bool trace = false;
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
    // --voice replaces the whole Voice struct, so collect preset selection and
    // numeric overrides separately. Overrides are replayed onto the final
    // preset after parsing, making their effect independent of flag order.
    Voice baseVoice = options.voice;
    std::vector<std::function<void(Voice &)>> overrides;
    const std::string doubleFlags[] = {
        "--pitch",
        "--rate",
        "--formant-scale",
        "--vibrato-rate",
        "--vibrato-depth",
        "--breathiness",
        "--gain",
        "--attack",
        "--release",
        "--declination",
        "--pitch-shift",
        "--formant-ratio",
        "--vocal-tract-scale",
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
        if (arg == "--list-languages") {
            listLanguages();
            std::exit(0);
        }
        if (arg == "--stdin") {
            options.useStdin = true;
            continue;
        }
        if (arg == "--trace") {
            options.trace = true;
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
                overrides.push_back([parsed](Voice &v) { v.pitch = parsed; });
            else if (flag == "--rate")
                overrides.push_back([parsed](Voice &v) { v.rate = parsed; });
            else if (flag == "--formant-scale")
                overrides.push_back(
                    [parsed](Voice &v) { v.formantScale = parsed; });
            else if (flag == "--vibrato-rate")
                overrides.push_back(
                    [parsed](Voice &v) { v.vibratoRate = parsed; });
            else if (flag == "--vibrato-depth")
                overrides.push_back(
                    [parsed](Voice &v) { v.vibratoDepth = parsed; });
            else if (flag == "--breathiness")
                overrides.push_back(
                    [parsed](Voice &v) { v.breathiness = parsed; });
            else if (flag == "--gain")
                overrides.push_back([parsed](Voice &v) { v.gain = parsed; });
            else if (flag == "--attack")
                overrides.push_back([parsed](Voice &v) { v.attack = parsed; });
            else if (flag == "--release")
                overrides.push_back([parsed](Voice &v) { v.release = parsed; });
            else if (flag == "--declination")
                overrides.push_back(
                    [parsed](Voice &v) { v.declination = parsed; });
            else if (flag == "--pitch-shift")
                overrides.push_back(
                    [parsed](Voice &v) { v = formant::shiftPitch(v, parsed); });
            else if (flag == "--formant-ratio")
                overrides.push_back([parsed](Voice &v) {
                    v = formant::scaleFormants(v, parsed);
                });
            else if (flag == "--vocal-tract-scale")
                overrides.push_back([parsed](Voice &v) {
                    v = formant::withVocalTractLength(v, parsed);
                });
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
        } else if (arg == "--lang" || arg.rfind("--lang=", 0) == 0) {
            options.language =
                languageFromTag(takeValue(i, argc, argv, "--lang"));
            options.useLanguage = true;
        } else if (arg == "--lang-file" || arg.rfind("--lang-file=", 0) == 0) {
            options.languageFile = takeValue(i, argc, argv, "--lang-file");
            options.useLanguageFile = true;
        } else if (arg == "--lexicon" || arg.rfind("--lexicon=", 0) == 0) {
            options.lexiconFile = takeValue(i, argc, argv, "--lexicon");
            options.useLexicon = true;
        } else if (arg == "--ssml-file" || arg.rfind("--ssml-file=", 0) == 0) {
            options.ssmlFile = takeValue(i, argc, argv, "--ssml-file");
            options.useSsml = true;
        } else if (arg == "--voice" || arg.rfind("--voice=", 0) == 0) {
            baseVoice = voicePreset(takeValue(i, argc, argv, "--voice"));
        } else if (arg == "--output" || arg.rfind("--output=", 0) == 0) {
            options.output = takeValue(i, argc, argv, "--output");
        } else if (arg == "--format" || arg.rfind("--format=", 0) == 0) {
            options.format = audioFormat(takeValue(i, argc, argv, "--format"));
        } else if (arg == "--dither" || arg.rfind("--dither=", 0) == 0) {
            options.audioOptions.dither = true;
            options.audioOptions.seed =
                ditherSeed(takeValue(i, argc, argv, "--dither"));
        } else if (arg == "--timing" || arg.rfind("--timing=", 0) == 0) {
            options.timingFile = takeValue(i, argc, argv, "--timing");
        } else if (arg == "--visemes" || arg.rfind("--visemes=", 0) == 0) {
            options.visemesFile = takeValue(i, argc, argv, "--visemes");
        } else if (arg == "--sample-rate" ||
                   arg.rfind("--sample-rate=", 0) == 0) {
            options.sampleRate = integer(
                "--sample-rate", takeValue(i, argc, argv, "--sample-rate"));
        } else if (arg == "--harmonics" || arg.rfind("--harmonics=", 0) == 0) {
            const int parsed =
                integer("--harmonics", takeValue(i, argc, argv, "--harmonics"));
            overrides.push_back([parsed](Voice &v) { v.harmonics = parsed; });
        } else {
            throw std::invalid_argument("unknown option: " + arg);
        }
    }

    // Replay every override onto the final preset in argument order, so a
    // value set before --voice is not erased by the preset assignment.
    options.voice = baseVoice;
    for (const auto &apply : overrides) {
        apply(options.voice);
    }
    return options;
}

std::string lowerAscii(std::string_view value) {
    std::string result(value);
    std::transform(
        result.begin(), result.end(), result.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return result;
}

// Timing formats are selected by extension. An unknown extension is an error
// rather than silently producing JSON under the wrong filename.
std::string timingFormatFor(std::string_view path) {
    const size_t dot = path.find_last_of('.');
    const std::string extension =
        dot == std::string_view::npos ? "" : lowerAscii(path.substr(dot));
    if (extension == ".json" || extension == ".lrc" || extension == ".srt" ||
        extension == ".vtt") {
        return extension.substr(1);
    }
    throw std::invalid_argument(
        "unsupported timing file extension: " + std::string(path) +
        " (use .json, .lrc, .srt or .vtt)");
}

std::string formatTiming(const formant::TimingPlan &plan,
                         const std::vector<formant::VisemeFrame> &visemes,
                         std::string_view path) {
    const std::string format = timingFormatFor(path);
    if (format == "lrc") {
        return formant::timingLrc(plan);
    }
    if (format == "srt") {
        return formant::timingSrt(plan);
    }
    if (format == "vtt") {
        return formant::timingVtt(plan);
    }
    return formant::timingJson(plan, visemes);
}

void writeTextFile(const std::string &path, const std::string &contents) {
    // Binary mode keeps the writers' fixed '\n' endings byte-identical on
    // every platform, which is part of the timing export contract.
    std::ofstream file(path, std::ios::binary);
    if (!file) {
        throw std::runtime_error("cannot open output file: " + path);
    }
    file << contents;
    if (!file) {
        throw std::runtime_error("cannot write output file: " + path);
    }
}

} // namespace

int main(int argc, char **argv) {
    try {
        const Options options = parseOptions(argc, argv);

        // Writing audio to stdout must leave stdout byte-clean; every human
        // message (including traces) goes to stderr in that mode.
        const bool audioToStdout = options.output == "-";
        std::ostream &status = audioToStdout ? std::cerr : std::cout;

        if (options.audioOptions.dither &&
            formant::isFloatAudioFormat(options.format)) {
            throw std::invalid_argument(
                "--dither only applies to 16-bit and 24-bit integer formats");
        }

        const int inputModes = static_cast<int>(options.usePhonemes) +
                               static_cast<int>(options.useIpa) +
                               static_cast<int>(options.useXsampa);
        if (inputModes > 1) {
            throw std::invalid_argument("use only one of --phonemes, --ipa or "
                                        "--xsampa (each overrides --text and "
                                        "--stdin)");
        }
        if (options.useSsml && inputModes > 0) {
            throw std::invalid_argument(
                "--ssml-file cannot be combined with --phonemes, --ipa or "
                "--xsampa");
        }
        if (options.useSsml && options.useStdin) {
            throw std::invalid_argument(
                "--ssml-file cannot be combined with --stdin");
        }
        if (options.useLanguage && inputModes > 0) {
            throw std::invalid_argument(
                "--lang only applies to text input; --phonemes, --ipa and "
                "--xsampa are language-independent");
        }
        if (options.useLanguage && options.useLanguageFile) {
            throw std::invalid_argument(
                "use only one of --lang and --lang-file");
        }
        if (options.useLanguageFile && inputModes > 0) {
            throw std::invalid_argument(
                "--lang-file only applies to text input; --phonemes, --ipa "
                "and --xsampa are language-independent");
        }
        if (options.useLexicon && inputModes > 0) {
            throw std::invalid_argument(
                "--lexicon only applies to text input; --phonemes, --ipa and "
                "--xsampa are language-independent");
        }
        if (options.trace && !options.useLanguageFile && !options.useLexicon) {
            throw std::invalid_argument("--trace only applies with "
                                        "--lang-file or --lexicon");
        }
        if (!options.timingFile.empty()) {
            // Fail before rendering (and before writing a WAV) on a typo in
            // the timing filename extension.
            (void)timingFormatFor(options.timingFile);
        }

        formant::LanguageDefinition languageDefinition;
        if (options.useLanguageFile) {
            std::ifstream file(options.languageFile);
            if (!file) {
                throw std::runtime_error("cannot open language file: " +
                                         options.languageFile);
            }
            std::ostringstream source;
            source << file.rdbuf();
            if (file.bad()) {
                throw std::runtime_error("cannot read language file: " +
                                         options.languageFile);
            }
            try {
                languageDefinition =
                    formant::parseLanguageDefinition(source.str());
            } catch (const std::invalid_argument &error) {
                throw std::invalid_argument(options.languageFile + ": " +
                                            error.what());
            }
        }

        formant::PronunciationLexicon pronunciationLexicon;
        if (options.useLexicon) {
            std::ifstream file(options.lexiconFile);
            if (!file) {
                throw std::runtime_error("cannot open lexicon file: " +
                                         options.lexiconFile);
            }
            std::ostringstream source;
            source << file.rdbuf();
            if (file.bad()) {
                throw std::runtime_error("cannot read lexicon file: " +
                                         options.lexiconFile);
            }
            try {
                pronunciationLexicon =
                    formant::parsePronunciationLexicon(source.str());
            } catch (const std::invalid_argument &error) {
                throw std::invalid_argument(options.lexiconFile + ": " +
                                            error.what());
            }
        }

        std::string ssmlSource;
        if (options.useSsml) {
            std::ifstream file(options.ssmlFile);
            if (!file) {
                throw std::runtime_error("cannot open SSML file: " +
                                         options.ssmlFile);
            }
            std::ostringstream source;
            source << file.rdbuf();
            if (file.bad()) {
                throw std::runtime_error("cannot read SSML file: " +
                                         options.ssmlFile);
            }
            ssmlSource = source.str();
        }

        // A transcription mode replaces the text source, so never wait on
        // stdin in that case even if --stdin was also supplied.
        std::string text = options.text;
        if (options.useStdin && inputModes == 0) {
            std::ostringstream buffer;
            buffer << std::cin.rdbuf();
            text = buffer.str();
        }

        if (options.voice.rate <= 0.0 || options.voice.gain <= 0.0 ||
            options.voice.pitch < 0.0 || options.voice.harmonics < 0) {
            throw std::invalid_argument("rate and gain must be positive; pitch "
                                        "and harmonics must not be negative");
        }

        std::vector<std::string> trace;
        std::vector<formant::Phoneme> phonemes;
        if (options.useSsml) {
            // SSML composes with the selected text layer: the same front end
            // that converts --text drives its text runs, while <phoneme> maps
            // straight to IPA. Precedence is documented in docs/ssml.md.
            formant::SsmlTextFrontEnd frontEnd;
            if (options.useLanguageFile) {
                if (options.useLexicon) {
                    frontEnd = [&](std::string_view run) {
                        return formant::phonemize(
                            run, languageDefinition, pronunciationLexicon,
                            options.trace ? &trace : nullptr);
                    };
                } else {
                    frontEnd = [&](std::string_view run) {
                        return formant::phonemize(run, languageDefinition,
                                                  options.trace ? &trace
                                                                : nullptr);
                    };
                }
            } else if (options.useLexicon) {
                frontEnd = [&](std::string_view run) {
                    return formant::phonemize(run, options.language,
                                              pronunciationLexicon,
                                              options.trace ? &trace : nullptr);
                };
            } else {
                frontEnd = [&](std::string_view run) {
                    return formant::phonemize(run, options.language);
                };
            }
            phonemes = formant::phonemizeSsml(ssmlSource, frontEnd);
            for (const std::string &line : trace) {
                status << "trace: " << line << "\n";
            }
        } else if (options.usePhonemes) {
            phonemes = formant::parsePhonemes(options.phonemes);
        } else if (options.useIpa) {
            phonemes = formant::parseIpa(options.ipa);
        } else if (options.useXsampa) {
            phonemes = formant::parseXsampa(options.xsampa);
        } else if (options.useLanguageFile) {
            if (options.useLexicon) {
                phonemes = formant::phonemize(text, languageDefinition,
                                              pronunciationLexicon,
                                              options.trace ? &trace : nullptr);
            } else {
                phonemes = formant::phonemize(text, languageDefinition,
                                              options.trace ? &trace : nullptr);
            }
            for (const std::string &line : trace) {
                status << "trace: " << line << "\n";
            }
        } else if (options.useLexicon) {
            phonemes =
                formant::phonemize(text, options.language, pronunciationLexicon,
                                   options.trace ? &trace : nullptr);
            for (const std::string &line : trace) {
                status << "trace: " << line << "\n";
            }
        } else {
            phonemes = formant::phonemize(text, options.language);
        }
        const std::vector<float> samples =
            formant::Synthesizer(options.voice)
                .render(phonemes, options.sampleRate);
        if (audioToStdout) {
            formant::writeAudio(std::cout, samples, options.sampleRate,
                                options.format, options.audioOptions);
        } else {
            formant::writeAudioFile(options.output, samples, options.sampleRate,
                                    options.format, options.audioOptions);
        }

        status << "Wrote " << samples.size() << " samples to "
               << (audioToStdout ? "stdout" : options.output) << " ("
               << options.voice.name << ", " << options.sampleRate << " Hz)\n";

        if (!options.timingFile.empty() || !options.visemesFile.empty()) {
            // Text modes supply their source words; transcription modes use
            // deterministic phoneme-derived labels. The same scheduler drives
            // the plan and render(), so these boundaries describe this WAV.
            const std::vector<std::string> wordLabels =
                inputModes == 0 && !options.useSsml
                    ? formant::splitWordsForTiming(text)
                    : std::vector<std::string>{};
            const formant::TimingPlan plan = formant::planTiming(
                phonemes, options.voice, options.sampleRate, wordLabels);
            if (plan.totalSamples != static_cast<int>(samples.size())) {
                throw std::runtime_error(
                    "timing plan does not match the rendered audio");
            }
            const std::vector<formant::VisemeFrame> visemes =
                formant::planVisemes(plan);

            if (!options.timingFile.empty()) {
                writeTextFile(options.timingFile,
                              formatTiming(plan, visemes, options.timingFile));
                status << "Wrote timing to " << options.timingFile << "\n";
            }
            if (!options.visemesFile.empty()) {
                writeTextFile(options.visemesFile,
                              formant::timingVisemesJson(plan, visemes));
                status << "Wrote visemes to " << options.visemesFile << "\n";
            }
        }
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "formant: " << error.what() << "\n";
        return 1;
    }
}
