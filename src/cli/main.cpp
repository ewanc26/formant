#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <cstdlib>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <limits>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "benchmark/benchmark.h"

#include "formant/formant.h"
#include "formant/affect.h"
#include "formant/breath.h"
#include "formant/frication.h"
#include "formant/register.h"
#include "formant/song.h"
#include "formant/nvv.h"
#include "formant/version.h"

namespace {

constexpr const char *kVersion = FORMANT_VERSION;

using formant::Language;
using formant::languageFromTag;
using formant::setVoiceParameter;
using formant::Voice;
using formant::voiceParameter;
using formant::voiceParameters;
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
  --prosody-file FILE     Layer exact F0, duration and amplitude overrides
  --intonation-file FILE  Expand symbolic ToBI tones into F0 contours
  --intonation-tune NAME  Built-in tune: statement, yesno, wh, list, exclamation
  --song NAME             Sing a built-in song (twinkle) instead of text
  --song-file FILE        Sing a versioned .song file
  --scale NAME            Song scale: chromatic (default) or quarter
  --scale-file FILE       Load a non-12-tone .scl scale table
  --a4 HZ                 Reference pitch for song mode (song default: 440)
  --list-songs            List built-in songs and exit
  --list-scales           List built-in scales and exit
  --dump-song NAME        Print a built-in song as versioned data
  --inspect-song FILE     Inspect a .song file's schema and checksum
  --dump-scale NAME       Print a built-in scale as versioned data
  --inspect-scale FILE    Inspect a .scl file's schema and checksum
  --phonemes "[h ee l o]" Speak a bracketed phoneme sequence instead of text
  --ipa "ˈhəˈloʊ"         Speak a UTF-8 IPA transcription instead of text
  --xsampa "h@"           Speak an X-SAMPA transcription instead of text
  --gibberish "PATTERN"   Generate seeded phonotactic words (Awkwords grammar)
  --gibberish-style MODE  pattern (default), statistical, or combined
  --gibberish-style-file FILE  Custom .ngram table for statistical/combined
  --seed N                Unsigned 32-bit seed for --gibberish (default: 1)
  --count N               Words to generate per render (default: 5)
  --transcript FILE       Save the generated IPA transcript (one word per line)
  --list-pattern-help     Print the pattern grammar and exit
  --lang TAG              Text language: en (English), es (Spanish)
  --lang-file FILE        Speak text with a user-defined language definition
  --lexicon FILE          Apply user pronunciation entries and homograph rules
  --trace                 Print rewrite-rule applications (with --lang-file)
  --voice NAME            Built-in voice: neutral, alto, bass, child, robot, whisper
  --voice-file FILE       Load a declarative voice definition over a base preset
  --list-voices           List available voices and exit
  --list-voice-parameters List tunable parameters, safe ranges and exit
  --dump-voice NAME       Print a voice definition for a preset or voice file
  --inspect-voice FILE    Print a package's metadata, schema and checksum status
  --package-voice NAME    Print a distributable schema 2 package (preset or file)
  --voice-name TEXT       Name for --package-voice (default: source name)
  --voice-author TEXT     Author metadata for --package-voice
  --voice-description TEXT Description metadata for --package-voice
  --voice-license TEXT    License metadata for --package-voice
  --voice-version TEXT    Package version for --package-voice
  --sweep SPEC            Parameter lab: param=start:end:steps[,param=...]
  --pair "A,B"            Render A/B voices on identical text for comparison
  --lab-dir DIR           Output directory for sweeps and pairs (default: lab)
  --lab-index FILE        Machine-readable lab index (default: DIR/index.json)
  --list-phonemes         List available phonemes and exit
  --list-ipa              List recognised IPA symbols and exit
  --list-languages        List available text languages and exit
  --list-nvv              List nonverbal vocalizations and styles and exit
  --nvv-file FILE         Load custom/override nonverbal vocalizations
  --nvv-style NAME        Style for nonverbal events (neutral, nervous, warm)
  --breathing MODE        Breath groups: off (default), silent, or auto
  --breath-file FILE      Load custom/override breath-group policy data
  --breath-style NAME     Breath policy style: narrative, formal, brisk
  --list-breath           List breath-group policies and exit
  --style NAME            Affect style: neutral, calm, excited, sad, whispering, newsreader
  --style-file FILE       Load custom/override affect style data
  --arousal -1..1         Continuous arousal override (PAD dimension)
  --valence -1..1         Continuous valence override (PAD dimension)
  --dominance -1..1       Continuous dominance override (PAD dimension)
  --list-styles           List affect styles, mapping and emphasis levels
  --dump-style NAME       Print a versioned affect data file for a style
  --inspect-style FILE    Print an affect file's version and checksum status
  --frication NAME        Use a built-in frication profile (per-place envelope)
  --frication-file FILE   Layer a versioned .frication table over the built-in one
  --list-frication        List built-in frication profiles and exit
  --dump-frication NAME   Print a versioned frication table for a profile
  --inspect-frication FILE  Inspect a frication file's schema and checksum
  --register NAME         Use a built-in overtone/subharmonic register
  --register-file FILE    Apply a versioned .txt register preset file
  --list-registers        List built-in register presets and exit
  --dump-register NAME    Print a versioned register table for a preset
  --inspect-register FILE Inspect a register file's schema and checksum
  --neural-model FILE     Load a saved neural prosody state
  --neural-train FILE     Train from line-oriented sample records
  --neural-save FILE      Save the trained neural state
  --neural-apply          Apply learned pitch/duration corrections
  --neural-strength 0..1  Blend learned corrections (default: 1)
  --neural-max-units N    Unit cap for a new model (default: 64)
  --list-neural           Print neural state summary and exit
  --benchmark             Benchmark the built-in corpus and exit
  --benchmark-repeats N   Timed renders per benchmark case (default: 5)
  --benchmark-report FILE Write a machine-readable benchmark report (TSV)
  --benchmark-expected FILE Write the checked-in expected-results data file
  --mrt DIR               Render the standard MRT intelligibility battery to DIR
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
  --nasality 0..1         Pervasive nasal anti-formant depth
  --harmonics N           Number of sine partials in the glottal source
  --gain FACTOR           Output gain before normalisation
  --jitter CENTS          Deterministic pitch modulation depth
  --shimmer DEPTH         Deterministic amplitude modulation depth (0..0.25)
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

void listVoiceParameters() {
    std::cout << "name\thard-min\thard-max\tsafe-min\tsafe-max\ttype\t"
                 "description\n";
    for (const formant::VoiceParameter &parameter : voiceParameters()) {
        std::cout << parameter.name << '\t' << parameter.minimum << '\t'
                  << parameter.maximum << '\t' << parameter.safeMinimum << '\t'
                  << parameter.safeMaximum << '\t'
                  << (parameter.integer ? "integer" : "real") << '\t'
                  << parameter.description << "\n";
    }
}

void listAffectStyles(const formant::AffectData &data) {
    std::cout << "schema\t" << data.schema << "\n";
    std::cout << "name\t" << data.name << "\n";
    std::cout << "version\t" << data.version << "\n";
    std::cout << "Styles (arousal\tvalence\tdominance -> pitch\trange\trate\t"
                 "intensity\tjitter\tshimmer\tadduction\teffort):\n";
    for (const auto &entry : data.styles) {
        const formant::AffectProfile profile =
            formant::resolveAffectProfile(data, entry.first);
        const formant::AffectAxes &a = profile.axes;
        const formant::AffectLevers &l = profile.levers;
        std::cout << "  " << entry.first << '\t' << a.arousal << '\t'
                  << a.valence << '\t' << a.dominance << " ->\t" << l.pitch
                  << '\t' << l.range << '\t' << l.rate << '\t' << l.intensity
                  << '\t' << l.jitterCents << '\t' << l.shimmer << '\t'
                  << l.adduction << '\t' << l.effort << "\n";
    }
    std::cout << "Emphasis (pitch\tduration\tamplitude):\n";
    for (const auto &entry : data.emphasis) {
        std::cout << "  " << entry.first << '\t' << entry.second.pitch << '\t'
                  << entry.second.duration << '\t' << entry.second.amplitude
                  << "\n";
    }
}

void inspectVoice(const std::string &path) {
    std::ifstream file(path);
    if (!file) {
        throw std::runtime_error("cannot open voice file: " + path);
    }
    std::ostringstream source;
    source << file.rdbuf();
    const std::string text = source.str();
    const formant::VoiceDefinition definition =
        formant::parseVoiceDefinition(text, false);
    const std::string expected = formant::voiceFileChecksum(text);
    std::cout << "path\t" << path << "\n"
              << "schema\t" << definition.schema << "\n"
              << "name\t" << definition.name << "\n"
              << "base\t" << definition.base << "\n"
              << "author\t" << definition.author << "\n"
              << "description\t" << definition.description << "\n"
              << "license\t" << definition.license << "\n"
              << "version\t" << definition.version << "\n"
              << "parameters\t" << definition.values.size() << "\n"
              << "checksum\t"
              << (definition.checksum ? *definition.checksum : "none") << "\n"
              << "computed\t" << expected << "\n"
              << "status\t"
              << (!definition.checksum
                      ? "unsigned"
                      : (*definition.checksum == expected ? "valid"
                                                          : "invalid"))
              << "\n";
}

void listSongs() {
    std::cout << "Built-in songs:\n";
    for (const std::string &name : formant::builtinSongNames()) {
        const formant::SongFile song =
            formant::parseSongFile(formant::builtinSongData(name));
        std::cout << "  " << name << '\t' << song.notes.size() << " notes\t"
                  << song.name << "\n";
    }
}

void listScales() {
    std::cout << "Built-in scales:\n";
    for (const std::string &name : formant::builtinScaleNames()) {
        const formant::SongScale scale =
            formant::parseSongScale(formant::builtinScaleData(name));
        std::cout << "  " << name << '\t' << scale.degrees.size()
                  << " degrees\t" << scale.periodCents << " cents\n";
    }
}

std::string_view builtinSongOrThrow(const std::string &name) {
    const std::string_view data = formant::builtinSongData(name);
    if (data.empty()) {
        throw std::invalid_argument("unknown built-in song '" + name +
                                    "' (use --list-songs)");
    }
    return data;
}

std::string_view builtinScaleOrThrow(const std::string &name) {
    const std::string_view data = formant::builtinScaleData(name);
    if (data.empty()) {
        throw std::invalid_argument("unknown built-in scale '" + name +
                                    "' (use --list-scales)");
    }
    return data;
}

void inspectSong(const std::string &path) {
    std::ifstream file(path);
    if (!file) {
        throw std::runtime_error("cannot open song file: " + path);
    }
    std::ostringstream source;
    source << file.rdbuf();
    if (file.bad()) {
        throw std::runtime_error("cannot read song file: " + path);
    }
    const std::string text = source.str();
    const formant::SongFile song = formant::parseSongFile(text, false);
    const std::string expected = formant::songFileChecksum(text);
    std::cout << "path\t" << path << "\n"
              << "schema\t" << song.schema << "\n"
              << "name\t" << song.name << "\n"
              << "a4\t" << song.a4Hz << "\n"
              << "scale\t" << song.scale << "\n"
              << "notes\t" << song.notes.size() << "\n"
              << "checksum\t" << (song.checksum ? *song.checksum : "none")
              << "\n"
              << "computed\t" << expected << "\n"
              << "status\t"
              << (!song.checksum
                      ? "unsigned"
                      : (*song.checksum == expected ? "valid" : "invalid"))
              << "\n";
}

void inspectScale(const std::string &path) {
    std::ifstream file(path);
    if (!file) {
        throw std::runtime_error("cannot open scale file: " + path);
    }
    std::ostringstream source;
    source << file.rdbuf();
    if (file.bad()) {
        throw std::runtime_error("cannot read scale file: " + path);
    }
    const std::string text = source.str();
    const formant::SongScale scale = formant::parseSongScale(text, false);
    const std::string expected = formant::songScaleChecksum(text);
    std::cout << "path\t" << path << "\n"
              << "schema\t" << scale.schema << "\n"
              << "name\t" << scale.name << "\n"
              << "period\t" << scale.periodCents << "\n"
              << "reference\t" << scale.referenceCents << "\n"
              << "degrees\t" << scale.degrees.size() << "\n"
              << "checksum\t" << (scale.checksum ? *scale.checksum : "none")
              << "\n"
              << "computed\t" << expected << "\n"
              << "status\t"
              << (!scale.checksum
                      ? "unsigned"
                      : (*scale.checksum == expected ? "valid" : "invalid"))
              << "\n";
}

void listFrication(const formant::FricationData &data) {
    std::cout << "Frication profiles (" << data.profiles.size() << "):\n";
    for (const std::string &name : formant::fricationProfileNames(data)) {
        const formant::FricationProfile *profile =
            formant::findFricationProfile(data, name);
        std::cout << "  " << name << '\t' << profile->partials << " partials\t"
                  << profile->centerHz << " Hz / " << profile->bandwidthHz
                  << " Hz\n";
    }
    std::cout << "defaults\t" << data.defaultPartials << " partials\t"
              << data.floor << " floor\t" << data.attackSeconds
              << " s attack\n";
}

void listRegisters(const formant::RegisterData &data) {
    std::cout << "Registers (" << data.registers.size() << "):\n";
    for (const std::string &name : formant::registerProfileNames(data)) {
        const formant::RegisterProfile *profile =
            formant::findRegisterProfile(data, name);
        std::cout << "  " << name << '\t' << "partial "
                  << profile->emphasisPartial << " x" << profile->emphasisGain
                  << " / " << profile->emphasisBandwidthHz << " Hz\t" << "sub "
                  << profile->subharmonicMix << " x"
                  << profile->subharmonicPartials;
        if (!profile->voqs.empty()) {
            std::cout << '\t' << profile->voqs;
        }
        std::cout << '\n';
    }
}

void inspectRegister(const std::string &path) {
    std::ifstream file(path);
    if (!file) {
        throw std::runtime_error("cannot open register file: " + path);
    }
    std::ostringstream source;
    source << file.rdbuf();
    if (file.bad()) {
        throw std::runtime_error("cannot read register file: " + path);
    }
    const std::string text = source.str();
    const formant::RegisterData data =
        formant::parseRegisterData(formant::builtinRegisterData(), text, false);
    const std::string expected = formant::registerFileChecksum(text);
    std::cout << "path\t" << path << "\n"
              << "schema\t" << data.schema << "\n"
              << "name\t" << data.name << "\n"
              << "registers\t" << data.registers.size() << "\n"
              << "checksum\t" << (data.checksum ? *data.checksum : "none")
              << "\n"
              << "computed\t" << expected << "\n"
              << "status\t"
              << (!data.checksum
                      ? "unsigned"
                      : (*data.checksum == expected ? "valid" : "invalid"))
              << "\n";
}

void inspectFrication(const std::string &path) {
    std::ifstream file(path);
    if (!file) {
        throw std::runtime_error("cannot open frication file: " + path);
    }
    std::ostringstream source;
    source << file.rdbuf();
    if (file.bad()) {
        throw std::runtime_error("cannot read frication file: " + path);
    }
    const std::string text = source.str();
    const formant::FricationData data = formant::parseFricationData(
        formant::builtinFricationData(), text, false);
    const std::string expected = formant::fricationFileChecksum(text);
    std::cout << "path\t" << path << "\n"
              << "schema\t" << data.schema << "\n"
              << "name\t" << data.name << "\n"
              << "profiles\t" << data.profiles.size() << "\n"
              << "partials\t" << data.defaultPartials << "\n"
              << "checksum\t" << (data.checksum ? *data.checksum : "none")
              << "\n"
              << "computed\t" << expected << "\n"
              << "status\t"
              << (!data.checksum
                      ? "unsigned"
                      : (*data.checksum == expected ? "valid" : "invalid"))
              << "\n";
}

void listLanguages() {
    for (const std::string &tag : formant::languageTags()) {
        const Language language = languageFromTag(tag);
        std::cout << tag << "\t" << formant::languageName(language) << "\n";
    }
}

void listPhonemes(const formant::LanguageDefinition *language = nullptr) {
    if (language != nullptr) {
        // A conlang's inventory is its phoneme declarations. Print the
        // symbols in sorted order so the listing is byte-stable.
        std::vector<std::string> names;
        names.reserve(language->phonemes.size());
        for (const auto &entry : language->phonemes) {
            names.push_back(entry.first);
        }
        std::sort(names.begin(), names.end());
        for (const std::string &name : names) {
            std::cout << name << "\n";
        }
        return;
    }
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

uint32_t unsignedSeed(const std::string &flag, const std::string &value) {
    if (value.empty() || value[0] == '-') {
        throw std::invalid_argument("expected an unsigned 32-bit seed for " +
                                    flag + ": " + value);
    }
    char *end = nullptr;
    errno = 0;
    const unsigned long long parsed = std::strtoull(value.c_str(), &end, 10);
    if (errno == ERANGE || end != value.c_str() + value.size() ||
        parsed > std::numeric_limits<uint32_t>::max()) {
        throw std::invalid_argument("expected an unsigned 32-bit seed for " +
                                    flag + ": " + value);
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
    std::string gibberish;
    uint32_t seed = 1;
    int gibberishCount = 5;
    std::string gibberishStyle = "pattern";
    std::string gibberishStyleFile;
    std::string transcriptFile;
    std::string languageFile;
    std::string nvvFile;
    std::string nvvStyle = "neutral";
    bool useNvvFile = false;
    bool listNvv = false;
    std::string breathing = "off";
    std::string breathFile;
    std::string breathStyle = "neutral";
    bool useBreathFile = false;
    bool listBreath = false;
    std::string styleName;
    std::string styleFile;
    bool useStyleFile = false;
    std::optional<double> arousal;
    std::optional<double> valence;
    std::optional<double> dominance;
    bool listStyles = false;
    std::string dumpStyle;
    std::string inspectStyleFile;
    std::string fricationName;
    std::string fricationFile;
    bool useFrication = false;
    bool useFricationFile = false;
    bool listFrication = false;
    std::string dumpFrication;
    std::string inspectFricationFile;
    std::string registerName;
    std::string registerFile;
    bool useRegister = false;
    bool useRegisterFile = false;
    bool listRegisters = false;
    std::string dumpRegister;
    std::string inspectRegisterFile;
    // Kept so an affect profile can be applied beneath explicit voice flags.
    Voice baseVoice = voicePreset("neutral");
    std::vector<std::function<void(Voice &)>> voiceOverrides;
    std::string neuralModelFile;
    std::string neuralTrainFile;
    std::string neuralSaveFile;
    double neuralStrength = 1.0;
    std::size_t neuralMaxUnits = 0;
    bool useNeuralModel = false;
    bool useNeuralTrain = false;
    bool neuralApply = false;
    bool listNeural = false;
    std::string lexiconFile;
    std::string ssmlFile;
    std::string prosodyFile;
    std::string intonationFile;
    std::string intonationTune;
    std::string songName;
    std::string songFile;
    std::string scaleName = "chromatic";
    std::string scaleFile;
    std::optional<double> a4Override;
    bool useSong = false;
    bool useScaleName = false;
    bool useScaleFile = false;
    bool listSongs = false;
    bool listScales = false;
    std::string dumpSong;
    std::string inspectSongFile;
    std::string dumpScale;
    std::string inspectScaleFile;
    std::string voiceFile;
    formant::VoiceDefinition voiceDefinition;
    bool useVoiceFile = false;
    std::string packageVoice;
    std::string packageName;
    std::string packageAuthor = "Formant contributors";
    std::string packageDescription = "A Formant voice package.";
    std::string packageLicense = "AGPL-3.0-or-later";
    std::string packageVersion = "1.0.0";
    std::string sweepSpec;
    std::string pairSpec;
    std::string labDir = "lab";
    std::string labIndex;
    bool benchmark = false;
    int benchmarkRepeats = 5;
    std::string benchmarkReport;
    std::string benchmarkExpected;
    std::string mrtOutDir;
    std::string output = "output.wav";
    formant::AudioFormat format = formant::AudioFormat::Pcm16;
    formant::AudioWriteOptions audioOptions;
    std::string timingFile;
    std::string visemesFile;
    int sampleRate = 44100;
    bool useText = false;
    bool usePhonemes = false;
    bool useIpa = false;
    bool useXsampa = false;
    bool useGibberish = false;
    bool useStdin = false;
    bool useLanguage = false;
    bool useLanguageFile = false;
    bool useLexicon = false;
    bool useSsml = false;
    bool useProsody = false;
    bool trace = false;
    bool listPhonemes = false;
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
        "--pitch",         "--rate",
        "--formant-scale", "--vibrato-rate",
        "--vibrato-depth", "--breathiness",
        "--nasality",      "--gain",
        "--jitter",        "--shimmer",
        "--attack",        "--release",
        "--declination",   "--pitch-shift",
        "--formant-ratio", "--vocal-tract-scale",
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
        if (arg == "--list-voice-parameters") {
            listVoiceParameters();
            std::exit(0);
        }
        if (arg == "--dump-voice" || arg.rfind("--dump-voice=", 0) == 0) {
            const std::string name = takeValue(i, argc, argv, "--dump-voice");
            bool preset = false;
            for (const std::string &known : voicePresetNames()) {
                if (known == name) {
                    preset = true;
                    break;
                }
            }
            const Voice voice =
                preset
                    ? voicePreset(name)
                    : formant::resolveVoice(formant::loadVoiceDefinition(name));
            std::cout << formant::writeVoiceDefinition(voice);
            std::exit(0);
        }
        if (arg == "--inspect-voice" || arg.rfind("--inspect-voice=", 0) == 0) {
            inspectVoice(takeValue(i, argc, argv, "--inspect-voice"));
            std::exit(0);
        }
        if (arg == "--list-phonemes") {
            // Deferred: a --lang-file later on the command line must be able
            // to supply the inventory that this listing reports.
            options.listPhonemes = true;
            continue;
        }
        if (arg == "--list-ipa") {
            listIpa();
            std::exit(0);
        }
        if (arg == "--list-languages") {
            listLanguages();
            std::exit(0);
        }
        if (arg == "--list-nvv") {
            options.listNvv = true;
            continue;
        }
        if (arg == "--list-breath") {
            options.listBreath = true;
            continue;
        }
        if (arg == "--list-styles") {
            options.listStyles = true;
            continue;
        }
        if (arg == "--list-frication") {
            options.listFrication = true;
            continue;
        }
        if (arg == "--list-registers") {
            options.listRegisters = true;
            continue;
        }
        if (arg == "--dump-register" || arg.rfind("--dump-register=", 0) == 0) {
            options.dumpRegister = takeValue(i, argc, argv, "--dump-register");
            continue;
        }
        if (arg == "--inspect-register" ||
            arg.rfind("--inspect-register=", 0) == 0) {
            options.inspectRegisterFile =
                takeValue(i, argc, argv, "--inspect-register");
            continue;
        }
        if (arg == "--dump-frication" ||
            arg.rfind("--dump-frication=", 0) == 0) {
            options.dumpFrication =
                takeValue(i, argc, argv, "--dump-frication");
            continue;
        }
        if (arg == "--inspect-frication" ||
            arg.rfind("--inspect-frication=", 0) == 0) {
            options.inspectFricationFile =
                takeValue(i, argc, argv, "--inspect-frication");
            continue;
        }
        if (arg == "--list-songs") {
            options.listSongs = true;
            continue;
        }
        if (arg == "--list-scales") {
            options.listScales = true;
            continue;
        }
        if (arg == "--dump-style" || arg.rfind("--dump-style=", 0) == 0) {
            options.dumpStyle = takeValue(i, argc, argv, "--dump-style");
            continue;
        }
        if (arg == "--inspect-style" || arg.rfind("--inspect-style=", 0) == 0) {
            options.inspectStyleFile =
                takeValue(i, argc, argv, "--inspect-style");
            continue;
        }
        if (arg == "--list-neural") {
            options.listNeural = true;
            continue;
        }
        if (arg == "--neural-apply") {
            options.neuralApply = true;
            continue;
        }
        if (arg == "--list-pattern-help") {
            std::cout << formant::patternGrammarHelp();
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
        if (arg == "--benchmark") {
            options.benchmark = true;
            continue;
        }
        if (arg == "--benchmark-repeats" ||
            arg.rfind("--benchmark-repeats=", 0) == 0) {
            options.benchmarkRepeats =
                integer("--benchmark-repeats",
                        takeValue(i, argc, argv, "--benchmark-repeats"));
            continue;
        }
        if (arg == "--benchmark-report" ||
            arg.rfind("--benchmark-report=", 0) == 0) {
            options.benchmarkReport =
                takeValue(i, argc, argv, "--benchmark-report");
            continue;
        }
        if (arg == "--benchmark-expected" ||
            arg.rfind("--benchmark-expected=", 0) == 0) {
            options.benchmarkExpected =
                takeValue(i, argc, argv, "--benchmark-expected");
            continue;
        }
        if (arg == "--mrt" || arg.rfind("--mrt=", 0) == 0) {
            options.mrtOutDir = takeValue(i, argc, argv, "--mrt");
            continue;
        }
        // Affect coordinates resolve into levers rather than overriding a
        // Voice field, so they are parsed here instead of in doubleFlags.
        if (arg == "--arousal" || arg.rfind("--arousal=", 0) == 0) {
            options.arousal =
                number("--arousal", takeValue(i, argc, argv, "--arousal"));
            continue;
        }
        if (arg == "--valence" || arg.rfind("--valence=", 0) == 0) {
            options.valence =
                number("--valence", takeValue(i, argc, argv, "--valence"));
            continue;
        }
        if (arg == "--dominance" || arg.rfind("--dominance=", 0) == 0) {
            options.dominance =
                number("--dominance", takeValue(i, argc, argv, "--dominance"));
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
            else if (flag == "--nasality")
                overrides.push_back(
                    [parsed](Voice &v) { v.nasality = parsed; });
            else if (flag == "--gain")
                overrides.push_back([parsed](Voice &v) { v.gain = parsed; });
            else if (flag == "--jitter")
                overrides.push_back(
                    [parsed](Voice &v) { v.jitterCents = parsed; });
            else if (flag == "--shimmer")
                overrides.push_back(
                    [parsed](Voice &v) { v.shimmerDepth = parsed; });
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
            options.useText = true;
        } else if (arg == "--phonemes" || arg.rfind("--phonemes=", 0) == 0) {
            options.phonemes = takeValue(i, argc, argv, "--phonemes");
            options.usePhonemes = true;
        } else if (arg == "--ipa" || arg.rfind("--ipa=", 0) == 0) {
            options.ipa = takeValue(i, argc, argv, "--ipa");
            options.useIpa = true;
        } else if (arg == "--xsampa" || arg.rfind("--xsampa=", 0) == 0) {
            options.xsampa = takeValue(i, argc, argv, "--xsampa");
            options.useXsampa = true;
        } else if (arg == "--gibberish" || arg.rfind("--gibberish=", 0) == 0) {
            options.gibberish = takeValue(i, argc, argv, "--gibberish");
            options.useGibberish = true;
        } else if (arg == "--gibberish-style" ||
                   arg.rfind("--gibberish-style=", 0) == 0) {
            options.gibberishStyle =
                takeValue(i, argc, argv, "--gibberish-style");
        } else if (arg == "--gibberish-style-file" ||
                   arg.rfind("--gibberish-style-file=", 0) == 0) {
            options.gibberishStyleFile =
                takeValue(i, argc, argv, "--gibberish-style-file");
        } else if (arg == "--seed" || arg.rfind("--seed=", 0) == 0) {
            options.seed =
                unsignedSeed("--seed", takeValue(i, argc, argv, "--seed"));
        } else if (arg == "--count" || arg.rfind("--count=", 0) == 0) {
            options.gibberishCount =
                integer("--count", takeValue(i, argc, argv, "--count"));
        } else if (arg == "--transcript" ||
                   arg.rfind("--transcript=", 0) == 0) {
            options.transcriptFile = takeValue(i, argc, argv, "--transcript");
        } else if (arg == "--lang" || arg.rfind("--lang=", 0) == 0) {
            options.language =
                languageFromTag(takeValue(i, argc, argv, "--lang"));
            options.useLanguage = true;
        } else if (arg == "--lang-file" || arg.rfind("--lang-file=", 0) == 0) {
            options.languageFile = takeValue(i, argc, argv, "--lang-file");
            options.useLanguageFile = true;
        } else if (arg == "--nvv-file" || arg.rfind("--nvv-file=", 0) == 0) {
            options.nvvFile = takeValue(i, argc, argv, "--nvv-file");
            options.useNvvFile = true;
        } else if (arg == "--nvv-style" || arg.rfind("--nvv-style=", 0) == 0) {
            options.nvvStyle = takeValue(i, argc, argv, "--nvv-style");
        } else if (arg == "--breathing" || arg.rfind("--breathing=", 0) == 0) {
            options.breathing = takeValue(i, argc, argv, "--breathing");
        } else if (arg == "--breath-file" ||
                   arg.rfind("--breath-file=", 0) == 0) {
            options.breathFile = takeValue(i, argc, argv, "--breath-file");
            options.useBreathFile = true;
        } else if (arg == "--breath-style" ||
                   arg.rfind("--breath-style=", 0) == 0) {
            options.breathStyle = takeValue(i, argc, argv, "--breath-style");
        } else if (arg == "--style" || arg.rfind("--style=", 0) == 0) {
            options.styleName = takeValue(i, argc, argv, "--style");
        } else if (arg == "--style-file" ||
                   arg.rfind("--style-file=", 0) == 0) {
            options.styleFile = takeValue(i, argc, argv, "--style-file");
            options.useStyleFile = true;
        } else if (arg == "--frication" || arg.rfind("--frication=", 0) == 0) {
            options.fricationName = takeValue(i, argc, argv, "--frication");
            options.useFrication = true;
        } else if (arg == "--frication-file" ||
                   arg.rfind("--frication-file=", 0) == 0) {
            options.fricationFile =
                takeValue(i, argc, argv, "--frication-file");
            options.useFricationFile = true;
        } else if (arg == "--register" || arg.rfind("--register=", 0) == 0) {
            options.registerName = takeValue(i, argc, argv, "--register");
            options.useRegister = true;
        } else if (arg == "--register-file" ||
                   arg.rfind("--register-file=", 0) == 0) {
            options.registerFile = takeValue(i, argc, argv, "--register-file");
            options.useRegisterFile = true;
        } else if (arg == "--neural-model" ||
                   arg.rfind("--neural-model=", 0) == 0) {
            options.neuralModelFile =
                takeValue(i, argc, argv, "--neural-model");
            options.useNeuralModel = true;
        } else if (arg == "--neural-train" ||
                   arg.rfind("--neural-train=", 0) == 0) {
            options.neuralTrainFile =
                takeValue(i, argc, argv, "--neural-train");
            options.useNeuralTrain = true;
        } else if (arg == "--neural-save" ||
                   arg.rfind("--neural-save=", 0) == 0) {
            options.neuralSaveFile = takeValue(i, argc, argv, "--neural-save");
        } else if (arg == "--neural-strength" ||
                   arg.rfind("--neural-strength=", 0) == 0) {
            options.neuralStrength =
                number("--neural-strength",
                       takeValue(i, argc, argv, "--neural-strength"));
        } else if (arg == "--neural-max-units" ||
                   arg.rfind("--neural-max-units=", 0) == 0) {
            const int parsed =
                integer("--neural-max-units",
                        takeValue(i, argc, argv, "--neural-max-units"));
            if (parsed <= 0) {
                throw std::invalid_argument(
                    "--neural-max-units must be a positive integer");
            }
            options.neuralMaxUnits = static_cast<std::size_t>(parsed);
        } else if (arg == "--lexicon" || arg.rfind("--lexicon=", 0) == 0) {
            options.lexiconFile = takeValue(i, argc, argv, "--lexicon");
            options.useLexicon = true;
        } else if (arg == "--ssml-file" || arg.rfind("--ssml-file=", 0) == 0) {
            options.ssmlFile = takeValue(i, argc, argv, "--ssml-file");
            options.useSsml = true;
        } else if (arg == "--prosody-file" ||
                   arg.rfind("--prosody-file=", 0) == 0) {
            options.prosodyFile = takeValue(i, argc, argv, "--prosody-file");
            options.useProsody = true;
        } else if (arg == "--intonation-file" ||
                   arg.rfind("--intonation-file=", 0) == 0) {
            options.intonationFile =
                takeValue(i, argc, argv, "--intonation-file");
        } else if (arg == "--intonation-tune" ||
                   arg.rfind("--intonation-tune=", 0) == 0) {
            options.intonationTune =
                takeValue(i, argc, argv, "--intonation-tune");
        } else if (arg == "--song" || arg.rfind("--song=", 0) == 0) {
            options.songName = takeValue(i, argc, argv, "--song");
            options.useSong = true;
        } else if (arg == "--song-file" || arg.rfind("--song-file=", 0) == 0) {
            options.songFile = takeValue(i, argc, argv, "--song-file");
            options.useSong = true;
        } else if (arg == "--scale" || arg.rfind("--scale=", 0) == 0) {
            options.scaleName = takeValue(i, argc, argv, "--scale");
            options.useScaleName = true;
        } else if (arg == "--scale-file" ||
                   arg.rfind("--scale-file=", 0) == 0) {
            options.scaleFile = takeValue(i, argc, argv, "--scale-file");
            options.useScaleFile = true;
        } else if (arg == "--a4" || arg.rfind("--a4=", 0) == 0) {
            options.a4Override =
                number("--a4", takeValue(i, argc, argv, "--a4"));
        } else if (arg == "--dump-song" || arg.rfind("--dump-song=", 0) == 0) {
            options.dumpSong = takeValue(i, argc, argv, "--dump-song");
        } else if (arg == "--inspect-song" ||
                   arg.rfind("--inspect-song=", 0) == 0) {
            options.inspectSongFile =
                takeValue(i, argc, argv, "--inspect-song");
        } else if (arg == "--dump-scale" ||
                   arg.rfind("--dump-scale=", 0) == 0) {
            options.dumpScale = takeValue(i, argc, argv, "--dump-scale");
        } else if (arg == "--inspect-scale" ||
                   arg.rfind("--inspect-scale=", 0) == 0) {
            options.inspectScaleFile =
                takeValue(i, argc, argv, "--inspect-scale");
        } else if (arg == "--package-voice" ||
                   arg.rfind("--package-voice=", 0) == 0) {
            options.packageVoice = takeValue(i, argc, argv, "--package-voice");
        } else if (arg == "--voice-name" ||
                   arg.rfind("--voice-name=", 0) == 0) {
            options.packageName = takeValue(i, argc, argv, "--voice-name");
        } else if (arg == "--voice-author" ||
                   arg.rfind("--voice-author=", 0) == 0) {
            options.packageAuthor = takeValue(i, argc, argv, "--voice-author");
        } else if (arg == "--voice-description" ||
                   arg.rfind("--voice-description=", 0) == 0) {
            options.packageDescription =
                takeValue(i, argc, argv, "--voice-description");
        } else if (arg == "--voice-license" ||
                   arg.rfind("--voice-license=", 0) == 0) {
            options.packageLicense =
                takeValue(i, argc, argv, "--voice-license");
        } else if (arg == "--voice-version" ||
                   arg.rfind("--voice-version=", 0) == 0) {
            options.packageVersion =
                takeValue(i, argc, argv, "--voice-version");
        } else if (arg == "--voice" || arg.rfind("--voice=", 0) == 0) {
            baseVoice = voicePreset(takeValue(i, argc, argv, "--voice"));
            options.useVoiceFile = false;
        } else if (arg == "--voice-file" ||
                   arg.rfind("--voice-file=", 0) == 0) {
            options.voiceFile = takeValue(i, argc, argv, "--voice-file");
            options.voiceDefinition =
                formant::loadVoiceDefinition(options.voiceFile);
            baseVoice = formant::resolveVoice(options.voiceDefinition);
            options.useVoiceFile = true;
        } else if (arg == "--sweep" || arg.rfind("--sweep=", 0) == 0) {
            options.sweepSpec = takeValue(i, argc, argv, "--sweep");
        } else if (arg == "--pair" || arg.rfind("--pair=", 0) == 0) {
            options.pairSpec = takeValue(i, argc, argv, "--pair");
        } else if (arg == "--lab-dir" || arg.rfind("--lab-dir=", 0) == 0) {
            options.labDir = takeValue(i, argc, argv, "--lab-dir");
        } else if (arg == "--lab-index" || arg.rfind("--lab-index=", 0) == 0) {
            options.labIndex = takeValue(i, argc, argv, "--lab-index");
        } else if (arg == "--output" || arg.rfind("--output=", 0) == 0) {
            options.output = takeValue(i, argc, argv, "--output");
        } else if (arg == "--format" || arg.rfind("--format=", 0) == 0) {
            options.format = audioFormat(takeValue(i, argc, argv, "--format"));
        } else if (arg == "--dither" || arg.rfind("--dither=", 0) == 0) {
            options.audioOptions.dither = true;
            options.audioOptions.seed =
                unsignedSeed("--dither", takeValue(i, argc, argv, "--dither"));
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
    // value set before --voice is not erased by the preset assignment. Keep the
    // unstyled base and overrides too: an affect profile is applied beneath
    // explicit voice flags so `--pitch 220 --style calm` still honours 220 Hz.
    options.baseVoice = baseVoice;
    options.voiceOverrides = overrides;
    options.voice = baseVoice;
    for (const auto &apply : overrides) {
        apply(options.voice);
    }
    if (!options.packageVoice.empty()) {
        bool preset = false;
        for (const std::string &known : voicePresetNames()) {
            if (known == options.packageVoice) {
                preset = true;
                break;
            }
        }
        const Voice voice =
            preset ? voicePreset(options.packageVoice)
                   : formant::resolveVoice(
                         formant::loadVoiceDefinition(options.packageVoice));
        const std::string name = options.packageName.empty()
                                     ? options.packageVoice
                                     : options.packageName;
        std::cout << formant::writeVoicePackage(
            voice, {name, options.packageAuthor, options.packageDescription,
                    options.packageLicense, options.packageVersion});
        std::exit(0);
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

std::string readTextFile(const std::string &path, std::string_view kind) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        throw std::runtime_error("cannot open " + std::string(kind) +
                                 " file: " + path);
    }
    std::ostringstream source;
    source << file.rdbuf();
    if (file.bad()) {
        throw std::runtime_error("cannot read " + std::string(kind) +
                                 " file: " + path);
    }
    return source.str();
}

void inspectAffect(const std::string &path) {
    const std::string text = readTextFile(path, "affect");
    const formant::AffectData data = formant::parseAffectData(
        formant::builtinAffectData(), text, /*verifyChecksum=*/false);
    const std::string computed = formant::affectFileChecksum(text);
    std::cout << "path\t" << path << "\n"
              << "schema\t" << data.schema << "\n"
              << "name\t" << data.name << "\n"
              << "author\t" << data.author << "\n"
              << "description\t" << data.description << "\n"
              << "license\t" << data.license << "\n"
              << "version\t" << data.version << "\n"
              << "styles\t" << data.styles.size() << "\n"
              << "emphasis\t" << data.emphasis.size() << "\n"
              << "checksum\t" << (data.checksum ? *data.checksum : "none")
              << "\n"
              << "computed\t" << computed << "\n"
              << "status\t"
              << (!data.checksum
                      ? "unsigned"
                      : (*data.checksum == computed ? "valid" : "invalid"))
              << "\n";
}

std::string trimmed(std::string_view value) {
    const std::size_t first = value.find_first_not_of(" \t");
    if (first == std::string_view::npos) {
        return "";
    }
    const std::size_t last = value.find_last_not_of(" \t");
    return std::string(value.substr(first, last - first + 1));
}

void warnUnsafeParameter(std::string_view name, double value) {
    for (const formant::VoiceParameter &parameter : voiceParameters()) {
        if (parameter.name != name) {
            continue;
        }
        if (value < parameter.safeMinimum || value > parameter.safeMaximum) {
            std::cerr << "warning: " << parameter.name << " " << value
                      << " is outside the safe range " << parameter.safeMinimum
                      << ".." << parameter.safeMaximum << "\n";
        }
        return;
    }
}

// One side of --pair: a preset name, a voice file path, and optionally
// inline parameter assignments separated by ';'. For example
// "neutral:pitch=180;rate=1.1" or "voices/warm.voice".
Voice voiceFromSpec(const std::string &spec) {
    const std::string value = trimmed(spec);
    if (value.empty()) {
        throw std::invalid_argument("empty voice specification");
    }
    const std::size_t colon = value.find(':');
    const std::string target = trimmed(value.substr(0, colon));
    bool preset = false;
    for (const std::string &name : voicePresetNames()) {
        if (name == target) {
            preset = true;
            break;
        }
    }
    Voice voice =
        preset ? voicePreset(target)
               : formant::resolveVoice(formant::loadVoiceDefinition(target));
    if (colon != std::string::npos) {
        std::string assignments = value.substr(colon + 1);
        std::size_t start = 0;
        while (start <= assignments.size()) {
            const std::size_t end = assignments.find(';', start);
            const std::string item = trimmed(assignments.substr(
                start,
                end == std::string::npos ? std::string::npos : end - start));
            const std::size_t equals = item.find('=');
            if (item.empty() || equals == std::string::npos) {
                throw std::invalid_argument("expected param=value in voice "
                                            "specification: " +
                                            item);
            }
            const std::string name = trimmed(item.substr(0, equals));
            const double parsed =
                number("--pair", trimmed(item.substr(equals + 1)));
            setVoiceParameter(voice, name, parsed);
            warnUnsafeParameter(name, parsed);
            if (end == std::string::npos) {
                break;
            }
            start = end + 1;
        }
    }
    return voice;
}

struct SweepAxis {
    std::string parameter;
    double start = 0.0;
    double end = 0.0;
    int steps = 0;
};

double sweepValue(const SweepAxis &axis, int index) {
    if (index == axis.steps - 1) {
        return axis.end;
    }
    return axis.start + (axis.end - axis.start) * index / (axis.steps - 1);
}

std::vector<SweepAxis> parseSweepSpec(const std::string &spec) {
    std::vector<SweepAxis> axes;
    std::size_t start = 0;
    while (start <= spec.size()) {
        const std::size_t comma = spec.find(',', start);
        const std::string part = trimmed(
            spec.substr(start, comma == std::string::npos ? std::string::npos
                                                          : comma - start));
        const std::size_t equals = part.find('=');
        const std::size_t firstColon = part.find(':', equals);
        const std::size_t secondColon = firstColon == std::string::npos
                                            ? std::string::npos
                                            : part.find(':', firstColon + 1);
        if (equals == std::string::npos || firstColon == std::string::npos ||
            secondColon == std::string::npos) {
            throw std::invalid_argument(
                "invalid --sweep spec '" + part +
                "' (use param=start:end:steps[,param=start:end:steps])");
        }
        SweepAxis axis;
        axis.parameter = trimmed(part.substr(0, equals));
        axis.start =
            number("--sweep",
                   trimmed(part.substr(equals + 1, firstColon - equals - 1)));
        axis.end = number(
            "--sweep",
            trimmed(part.substr(firstColon + 1, secondColon - firstColon - 1)));
        axis.steps = integer("--sweep", trimmed(part.substr(secondColon + 1)));
        if (axis.steps < 2) {
            throw std::invalid_argument("--sweep steps must be at least 2: " +
                                        part);
        }
        if (axis.start > axis.end) {
            throw std::invalid_argument("--sweep start must not exceed end: " +
                                        part);
        }
        // Validate endpoints against the hard intervals now, before any file
        // is created, and report values that leave the documented safe band.
        Voice scratch;
        for (int step = 0; step < axis.steps; ++step) {
            // Intermediate values can violate the integer constraint even
            // when both endpoints are valid, so reject the whole spec before
            // writing any cell.
            setVoiceParameter(scratch, axis.parameter, sweepValue(axis, step));
        }
        warnUnsafeParameter(axis.parameter, axis.start);
        warnUnsafeParameter(axis.parameter, axis.end);
        for (const SweepAxis &existing : axes) {
            if (existing.parameter == axis.parameter) {
                throw std::invalid_argument("duplicate --sweep parameter: " +
                                            axis.parameter);
            }
        }
        axes.push_back(axis);
        if (axes.size() > 2) {
            throw std::invalid_argument(
                "--sweep supports at most two parameters");
        }
        if (comma == std::string::npos) {
            break;
        }
        start = comma + 1;
    }
    if (axes.empty()) {
        throw std::invalid_argument("--sweep needs at least one parameter");
    }
    long long cells = 1;
    for (const SweepAxis &axis : axes) {
        cells *= axis.steps;
        if (cells > 10000) {
            throw std::invalid_argument(
                "--sweep produces too many cells (limit 10000)");
        }
    }
    return axes;
}

std::string jsonString(std::string_view text) {
    std::string out = "\"";
    for (unsigned char c : text) {
        switch (c) {
        case '"':
            out += "\\\"";
            break;
        case '\\':
            out += "\\\\";
            break;
        case '\n':
            out += "\\n";
            break;
        case '\r':
            out += "\\r";
            break;
        case '\t':
            out += "\\t";
            break;
        default:
            if (c < 0x20) {
                std::ostringstream escape;
                escape << "\\u00" << std::hex << std::setw(2)
                       << std::setfill('0') << static_cast<int>(c);
                out += escape.str();
            } else {
                out += static_cast<char>(c);
            }
        }
    }
    out += '"';
    return out;
}

std::string jsonNumber(double value) {
    std::ostringstream out;
    out << std::fixed << std::setprecision(6) << value;
    std::string text = out.str();
    if (text == "-0.000000") {
        text = "0.000000";
    }
    return text;
}

std::string labelNumber(double value) {
    std::ostringstream out;
    out << std::fixed << std::setprecision(6) << value;
    std::string text = out.str();
    std::replace(text.begin(), text.end(), '.', 'p');
    std::replace(text.begin(), text.end(), '-', 'm');
    return text;
}

std::string cellLabel(const std::vector<SweepAxis> &axes,
                      const std::vector<double> &values) {
    std::string label;
    for (std::size_t i = 0; i < axes.size(); ++i) {
        if (!label.empty()) {
            label += "_";
        }
        label += axes[i].parameter + "-" + labelNumber(values[i]);
    }
    return label;
}

} // namespace

int main(int argc, char **argv) {
    try {
        Options options = parseOptions(argc, argv);

        // Writing audio to stdout must leave stdout byte-clean; every human
        // message (including traces) goes to stderr in that mode.
        const bool audioToStdout = options.output == "-";
        std::ostream &status = audioToStdout ? std::cerr : std::cout;

        if (!options.benchmarkExpected.empty()) {
            writeTextFile(options.benchmarkExpected,
                          formant::benchmarkExpectedReport());
            return 0;
        }
        if (options.benchmark) {
            if (options.benchmarkRepeats < 1) {
                throw std::invalid_argument(
                    "--benchmark-repeats must be positive");
            }
            bool allOk = true;
            std::string report = formant::benchmarkReportHeader();
            for (const formant::BenchmarkCase &testCase :
                 formant::benchmarkCorpus()) {
                const formant::BenchmarkResult result =
                    formant::runBenchmark(testCase, options.benchmarkRepeats);
                report += formant::benchmarkReportLine(result);
                status << testCase.name << "\tRTF " << result.realTimeFactor
                       << "\t" << result.samplesPerSecond << " samples/s\tF0 "
                       << result.f0Hz << " Hz\t" << result.harmonics
                       << " harmonics\t" << result.noisePartials
                       << " noise partials\t"
                       << (result.checksumOk ? "ok" : "FAIL") << "\n";
                allOk = allOk && result.checksumOk;
            }
            if (!options.benchmarkReport.empty()) {
                writeTextFile(options.benchmarkReport, report);
            }
            if (!allOk) {
                status << "benchmark checksum mismatch\n";
                return 1;
            }
            return 0;
        }

        if (!options.mrtOutDir.empty()) {
            if (options.useLanguageFile) {
                throw std::invalid_argument(
                    "--mrt currently supports built-in text languages; "
                    "--lang-file is not supported");
            }
            formant::MrtBatteryOptions mrt;
            mrt.outDir = options.mrtOutDir;
            mrt.voice = options.voice;
            mrt.language = options.language;
            mrt.sampleRate = options.sampleRate;
            const formant::MrtBatteryResult result =
                formant::writeMrtBattery(mrt);
            status << "Wrote MRT battery (" << result.filesWritten
                   << " files) to " << result.outDir << "\n";
            return 0;
        }

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
        if (options.useSong && !options.songName.empty() &&
            !options.songFile.empty()) {
            throw std::invalid_argument(
                "use only one of --song and --song-file");
        }
        if (options.useScaleName && options.useScaleFile) {
            throw std::invalid_argument(
                "use only one of --scale and --scale-file");
        }
        if (options.useFrication && options.useFricationFile) {
            throw std::invalid_argument(
                "use only one of --frication and --frication-file");
        }
        if (options.useRegister && options.useRegisterFile) {
            throw std::invalid_argument(
                "use only one of --register and --register-file");
        }
        if (!options.useSong &&
            (options.useScaleName || options.useScaleFile)) {
            throw std::invalid_argument(
                "--scale and --scale-file only apply to song mode");
        }
        if (options.a4Override &&
            (!std::isfinite(*options.a4Override) ||
             *options.a4Override < 200.0 || *options.a4Override > 1000.0)) {
            throw std::invalid_argument("--a4 must be in [200, 1000] Hz");
        }
        if (options.a4Override && !options.useSong) {
            throw std::invalid_argument("--a4 only applies to song mode");
        }
        if (options.useSong &&
            (options.useText || options.useStdin || inputModes > 0 ||
             options.useGibberish || options.useSsml || options.useLanguage ||
             options.useLanguageFile || options.useLexicon)) {
            throw std::invalid_argument(
                "--song and --song-file cannot be combined with text, "
                "phoneme, IPA, X-SAMPA, SSML, gibberish or language inputs");
        }
        if (options.useSong &&
            (!options.intonationFile.empty() ||
             !options.intonationTune.empty() || options.useProsody ||
             options.neuralApply || options.breathing != "off")) {
            throw std::invalid_argument(
                "song mode owns its F0 contour and cannot be combined with "
                "intonation, prosody, neural or breathing layers");
        }
        if (options.useGibberish && inputModes > 0) {
            throw std::invalid_argument(
                "use only one of --gibberish, --phonemes, --ipa or --xsampa");
        }
        if (options.useGibberish && options.useSsml) {
            throw std::invalid_argument(
                "--ssml-file cannot be combined with --gibberish");
        }
        if (options.gibberishCount < 1) {
            throw std::invalid_argument("--count must be positive");
        }
        if (!options.transcriptFile.empty() && !options.useGibberish) {
            throw std::invalid_argument(
                "--transcript only applies to --gibberish");
        }
        if (!options.gibberishStyleFile.empty() && !options.useGibberish) {
            throw std::invalid_argument(
                "--gibberish-style-file only applies to --gibberish");
        }
        if (options.gibberishStyle != "pattern" && !options.useGibberish) {
            throw std::invalid_argument(
                "--gibberish-style only applies to --gibberish");
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
        for (const auto *axis :
             {&options.arousal, &options.valence, &options.dominance}) {
            if (*axis && !std::isfinite(**axis)) {
                throw std::invalid_argument(
                    "affect axes must be finite numbers in [-1, 1]");
            }
            if (*axis && (std::abs(**axis) > formant::kAffectAxisLimit)) {
                throw std::invalid_argument("affect axes must be in [-1, 1]");
            }
        }
        if (options.neuralStrength < 0.0 || options.neuralStrength > 1.0) {
            throw std::invalid_argument("--neural-strength must be in [0, 1]");
        }
        if (options.neuralMaxUnits != 0 && options.useNeuralModel) {
            throw std::invalid_argument(
                "--neural-max-units applies only to a new model (without "
                "--neural-model)");
        }
        if (options.neuralApply && !options.useNeuralModel &&
            !options.useNeuralTrain) {
            throw std::invalid_argument(
                "--neural-apply requires --neural-model or --neural-train");
        }
        if (!options.timingFile.empty()) {
            // Fail before rendering (and before writing a WAV) on a typo in
            // the timing filename extension.
            (void)timingFormatFor(options.timingFile);
        }

        const bool labMode =
            !options.sweepSpec.empty() || !options.pairSpec.empty();
        if (!options.sweepSpec.empty() && !options.pairSpec.empty()) {
            throw std::invalid_argument("use only one of --sweep and --pair");
        }
        if (labMode && options.output == "-") {
            throw std::invalid_argument(
                "--sweep and --pair write multiple files and cannot use "
                "--output -");
        }
        if (labMode &&
            (!options.timingFile.empty() || !options.visemesFile.empty())) {
            throw std::invalid_argument(
                "--timing and --visemes cannot be combined with --sweep or "
                "--pair (each render would overwrite the same file)");
        }
        if (labMode && options.labDir.empty()) {
            throw std::invalid_argument("--lab-dir cannot be empty");
        }
        if (options.useVoiceFile) {
            // Warn only about values this file actually changes: the built-in
            // presets deliberately use some extremes and round-trip quietly.
            const Voice base = voicePreset(options.voiceDefinition.base);
            for (const auto &entry : options.voiceDefinition.values) {
                if (entry.second != voiceParameter(base, entry.first)) {
                    warnUnsafeParameter(entry.first, entry.second);
                }
            }
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

        // #124: build the nonverbal-vocalization library once. Built-ins are
        // embedded in the binary; user files only add or override definitions.
        std::string nvvUser;
        if (options.useNvvFile) {
            std::ifstream file(options.nvvFile);
            if (!file) {
                throw std::runtime_error("cannot open NVV file: " +
                                         options.nvvFile);
            }
            std::ostringstream source;
            source << file.rdbuf();
            if (file.bad()) {
                throw std::runtime_error("cannot read NVV file: " +
                                         options.nvvFile);
            }
            nvvUser = source.str();
        }
        formant::NvvLibrary nvv(formant::builtinNvvData(), nvvUser);
        nvv.setStyle(options.nvvStyle);
        const auto withNvv = [&nvv](formant::SsmlTextFrontEnd base) {
            return formant::SsmlTextFrontEnd(
                [&nvv, base](std::string_view run) {
                    return nvv.expand(run, base);
                });
        };

        // #143: breath policy is data too. The built-in table is embedded; a
        // user table layers over it field by field without replacing the rest.
        const formant::BreathMode breathing =
            formant::breathModeFromName(options.breathing);
        std::string breathUser;
        if (options.useBreathFile) {
            std::ifstream file(options.breathFile);
            if (!file) {
                throw std::runtime_error("cannot open breath file: " +
                                         options.breathFile);
            }
            std::ostringstream source;
            source << file.rdbuf();
            if (file.bad()) {
                throw std::runtime_error("cannot read breath file: " +
                                         options.breathFile);
            }
            breathUser = source.str();
        }
        const formant::BreathPolicySet breathPolicies =
            formant::parseBreathData(formant::builtinBreathData(), breathUser);
        if (!options.breathStyle.empty() && options.breathStyle != "neutral" &&
            breathPolicies.styleOverlays.find(options.breathStyle) ==
                breathPolicies.styleOverlays.end()) {
            throw std::invalid_argument("unknown breath style: '" +
                                        options.breathStyle + "'");
        }

        // #125: affect styles are data too. The built-in PAD mapping and named
        // presets are embedded; a user file layers new styles or coefficients
        // over them. Resolution is shared by CLI flags and file coordinates.
        std::string styleUser;
        if (options.useStyleFile) {
            std::ifstream file(options.styleFile);
            if (!file) {
                throw std::runtime_error("cannot open style file: " +
                                         options.styleFile);
            }
            std::ostringstream source;
            source << file.rdbuf();
            if (file.bad()) {
                throw std::runtime_error("cannot read style file: " +
                                         options.styleFile);
            }
            styleUser = source.str();
        }
        const formant::AffectData affectData =
            formant::parseAffectData(formant::builtinAffectData(), styleUser);
        const bool affectActive = !options.styleName.empty() ||
                                  options.useStyleFile || options.arousal ||
                                  options.valence || options.dominance;
        const formant::AffectProfile affectProfile =
            affectActive ? formant::resolveAffectProfile(
                               affectData, options.styleName, options.arousal,
                               options.valence, options.dominance)
                         : formant::AffectProfile{};
        formant::SsmlEmphasisLevels emphasisLevels;
        for (const auto &entry : affectProfile.emphasis) {
            emphasisLevels[entry.first] = {entry.second.pitch,
                                           entry.second.duration,
                                           entry.second.amplitude};
        }
        if (affectActive) {
            // Apply beneath explicit voice flags so authored --pitch/--gain or
            // a voice file still has the final word over the style baseline.
            options.voice = options.baseVoice;
            formant::applyAffectStyle(options.voice, affectProfile);
            for (const auto &apply : options.voiceOverrides) {
                apply(options.voice);
            }
            status << "Applied affect style '" << affectProfile.name << "'\n";
        }

        // #101: frication envelopes are data too. The built-in per-place table
        // is embedded; a user file layers individual profiles over it.
        std::string fricationUser;
        if (options.useFricationFile) {
            std::ifstream file(options.fricationFile);
            if (!file) {
                throw std::runtime_error("cannot open frication file: " +
                                         options.fricationFile);
            }
            std::ostringstream source;
            source << file.rdbuf();
            if (file.bad()) {
                throw std::runtime_error("cannot read frication file: " +
                                         options.fricationFile);
            }
            fricationUser = source.str();
        }
        const formant::FricationData fricationData =
            formant::parseFricationData(formant::builtinFricationData(),
                                        fricationUser);

        // #140: register presets are data too. The built-in table is embedded;
        // a user file layers individual presets over it at parse time.
        std::string registerUser;
        if (options.useRegisterFile) {
            std::ifstream file(options.registerFile);
            if (!file) {
                throw std::runtime_error("cannot open register file: " +
                                         options.registerFile);
            }
            std::ostringstream source;
            source << file.rdbuf();
            if (file.bad()) {
                throw std::runtime_error("cannot read register file: " +
                                         options.registerFile);
            }
            registerUser = source.str();
        }
        const formant::RegisterData registerData = formant::parseRegisterData(
            formant::builtinRegisterData(), registerUser);

        // #140: register presets set synthesis levers on the voice itself, so
        // they compose with every front end and with singing mode. Applied
        // after voice overrides because they are an explicit mode selection,
        // and before rendering so the same voice drives every render call.
        if (options.useRegister) {
            if (!formant::applyRegister(options.voice, registerData,
                                        options.registerName)) {
                throw std::invalid_argument("unknown register '" +
                                            options.registerName +
                                            "' (use --list-registers)");
            }
            status << "Applied register '" << options.registerName << "'\n";
        } else if (options.useRegisterFile) {
            // A shared file describes exactly one register: --dump-register
            // emits this shape. The combined table still supplies the values,
            // so a file that retunes a built-in name applies the retuned data.
            const formant::RegisterData fileData =
                formant::parseRegisterData(registerUser, {}, false);
            if (fileData.registers.size() != 1) {
                throw std::invalid_argument(
                    "register file must define exactly one preset "
                    "(use --dump-register NAME)");
            }
            const std::string name = fileData.registers.begin()->first;
            if (!formant::applyRegister(options.voice, registerData, name)) {
                throw std::invalid_argument("unknown register '" + name +
                                            "' in register file");
            }
            status << "Applied register '" << name << "' from register file\n";
        }

        formant::NeuralConfig neuralConfig;
        if (options.neuralMaxUnits != 0) {
            neuralConfig.maxUnits = options.neuralMaxUnits;
        }
        formant::GrowingNeuralNetwork neural(neuralConfig);
        if (options.useNeuralModel) {
            try {
                neural = formant::GrowingNeuralNetwork::parse(
                    readTextFile(options.neuralModelFile, "neural model"));
            } catch (const std::invalid_argument &error) {
                throw std::invalid_argument(options.neuralModelFile + ": " +
                                            error.what());
            }
        }
        if (options.useNeuralTrain) {
            const std::string sampleSource =
                readTextFile(options.neuralTrainFile, "neural samples");
            std::vector<formant::NeuralSample> samples;
            try {
                samples = formant::parseNeuralSamples(sampleSource);
            } catch (const std::invalid_argument &error) {
                throw std::invalid_argument(options.neuralTrainFile + ": " +
                                            error.what());
            }
            neural.train(samples);
            status << "Trained " << samples.size() << " neural sample(s)\n";
        }
        if (!options.neuralSaveFile.empty()) {
            writeTextFile(options.neuralSaveFile, neural.serialize());
            status << "Saved neural state to " << options.neuralSaveFile
                   << "\n";
        }
        if (options.listNeural) {
            const formant::NeuralSummary summary = neural.summary();
            status << "Neural state:\n"
                   << "  units " << summary.units << "\n"
                   << "  edges " << summary.edges << "\n"
                   << "  observations " << summary.observations << "\n"
                   << "  growths " << summary.growths << "\n"
                   << "  mean-error " << summary.meanError << "\n"
                   << "  max-units " << neural.config().maxUnits << "\n";
            return 0;
        }

        if (options.listBreath) {
            status << "Breathing modes:\n  off\n  silent\n  auto\n";
            status << "Languages:\n";
            for (const auto &entry : breathPolicies.languages) {
                status << "  " << entry.first << "\n";
            }
            status << "Styles:\n  neutral\n";
            for (const auto &entry : breathPolicies.styles) {
                if (entry.first != "neutral") {
                    status << "  " << entry.first << "\n";
                }
            }
            const formant::BreathGroupPolicy policy =
                formant::resolveBreathPolicy(breathPolicies, "en",
                                             options.breathStyle);
            status << "Resolved en/" << options.breathStyle << ":\n"
                   << "  min-words " << policy.minWords << "\n"
                   << "  max-words " << policy.maxWords << "\n"
                   << "  max-seconds " << policy.maxSeconds << "\n"
                   << "  declination " << policy.declination << "\n"
                   << "  reset " << policy.reset << "\n"
                   << "  depth " << policy.depthShort << "/"
                   << policy.depthMedium << "/" << policy.depthLong << "\n";
            return 0;
        }

        if (options.listStyles) {
            listAffectStyles(affectData);
            return 0;
        }
        if (!options.dumpStyle.empty()) {
            std::cout << formant::writeAffectData(affectData,
                                                  options.dumpStyle);
            return 0;
        }
        if (!options.inspectStyleFile.empty()) {
            inspectAffect(options.inspectStyleFile);
            return 0;
        }

        if (options.listFrication) {
            listFrication(fricationData);
            return 0;
        }
        if (!options.dumpFrication.empty()) {
            std::cout << formant::writeFricationData(fricationData,
                                                     options.dumpFrication);
            return 0;
        }
        if (!options.inspectFricationFile.empty()) {
            inspectFrication(options.inspectFricationFile);
            return 0;
        }

        if (options.listRegisters) {
            listRegisters(registerData);
            return 0;
        }
        if (!options.dumpRegister.empty()) {
            std::cout << formant::writeRegisterData(registerData,
                                                    options.dumpRegister);
            return 0;
        }
        if (!options.inspectRegisterFile.empty()) {
            inspectRegister(options.inspectRegisterFile);
            return 0;
        }

        if (options.listSongs) {
            listSongs();
            return 0;
        }
        if (options.listScales) {
            listScales();
            return 0;
        }
        if (!options.dumpSong.empty()) {
            std::cout << builtinSongOrThrow(options.dumpSong);
            return 0;
        }
        if (!options.inspectSongFile.empty()) {
            inspectSong(options.inspectSongFile);
            return 0;
        }
        if (!options.dumpScale.empty()) {
            std::cout << builtinScaleOrThrow(options.dumpScale);
            return 0;
        }
        if (!options.inspectScaleFile.empty()) {
            inspectScale(options.inspectScaleFile);
            return 0;
        }

        if (options.listNvv) {
            status << "Nonverbal vocalizations:\n";
            for (const std::string &name : nvv.names()) {
                status << "  " << name << "\n";
            }
            status << "Styles:\n";
            for (const std::string &name : nvv.styleNames()) {
                status << "  " << name << "\n";
            }
            return 0;
        }

        if (options.listPhonemes) {
            listPhonemes(options.useLanguageFile ? &languageDefinition
                                                 : nullptr);
            return 0;
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
        std::vector<std::string> conlangWarnings;
        const auto printConlangWarnings = [&]() {
            for (const std::string &line : conlangWarnings) {
                status << "warning: " << line << "\n";
            }
        };
        formant::SongFile song;
        formant::SongScale scale;
        if (options.useSong) {
            if (!options.songFile.empty()) {
                song = formant::loadSongFile(options.songFile);
            } else {
                song = formant::parseSongFile(
                    builtinSongOrThrow(options.songName));
            }
            if (!options.scaleFile.empty()) {
                scale = formant::loadSongScale(options.scaleFile);
            } else if (options.useScaleName) {
                scale = formant::parseSongScale(
                    builtinScaleOrThrow(options.scaleName));
            } else {
                const std::string requested =
                    song.scale.empty() ? "chromatic" : song.scale;
                const std::string_view data =
                    formant::builtinScaleData(requested);
                if (data.empty()) {
                    throw std::invalid_argument(
                        "song requests scale '" + requested +
                        "' which is not built in (use --scale-file)");
                }
                scale = formant::parseSongScale(data);
            }
            status << "Loaded song '" << song.name << "' (" << song.notes.size()
                   << " notes) on scale '" << scale.name << "'\n";
        }

        std::vector<formant::Phoneme> phonemes;
        std::string generatedTranscript;
        if (options.useSong) {
            phonemes = formant::phonemizeSong(song, scale,
                                              options.a4Override.value_or(0.0));
            status << "Resolved " << phonemes.size()
                   << " note segment(s) to absolute F0\n";
        } else if (options.useGibberish) {
            formant::PatternInventory inventory =
                options.useLanguageFile
                    ? formant::patternInventory(languageDefinition)
                    : formant::defaultPatternInventory();
            formant::GibberishSettings settings;
            settings.seed = options.seed;
            settings.count = options.gibberishCount;
            settings.styleTablePath = options.gibberishStyleFile;
            if (options.gibberishStyle == "pattern") {
                settings.style = formant::GibberishStyle::Pattern;
                settings.styleTag = "en-us";
            } else if (options.gibberishStyle == "statistical") {
                settings.style = formant::GibberishStyle::Statistical;
                settings.styleTag = "en-us";
            } else if (options.gibberishStyle == "combined") {
                settings.style = formant::GibberishStyle::Combined;
                settings.styleTag = "en-us";
            } else if (options.gibberishStyleFile.empty()) {
                settings.style = formant::GibberishStyle::Statistical;
                settings.styleTag = options.gibberishStyle;
            } else {
                settings.style = formant::GibberishStyle::Statistical;
                settings.styleTag = options.gibberishStyle;
            }
            formant::PatternGrammar grammar;
            if (settings.style != formant::GibberishStyle::Statistical) {
                grammar = formant::parsePatternGrammar(options.gibberish);
            }
            const formant::GibberishResult result = formant::generateGibberish(
                grammar, inventory, settings, options.language);
            phonemes = result.phonemes;
            generatedTranscript = result.transcript;
            status << "Generated " << result.words.size() << " word(s) from "
                   << options.gibberishStyle << " gibberish (seed "
                   << options.seed << ")\n";
            status << generatedTranscript << "\n";
            if (!options.transcriptFile.empty()) {
                writeTextFile(options.transcriptFile, generatedTranscript);
                status << "Wrote transcript to " << options.transcriptFile
                       << "\n";
            }
        } else if (options.useSsml) {
            // SSML composes with the selected text layer: the same front end
            // that converts --text drives its text runs, while <phoneme> maps
            // straight to IPA. Precedence is documented in docs/ssml.md.
            formant::SsmlTextFrontEnd frontEnd;
            if (options.useLanguageFile) {
                if (options.useLexicon) {
                    frontEnd = [&](std::string_view run) {
                        return formant::phonemize(
                            run, languageDefinition, pronunciationLexicon,
                            options.trace ? &trace : nullptr, &conlangWarnings);
                    };
                } else {
                    frontEnd = [&](std::string_view run) {
                        return formant::phonemize(
                            run, languageDefinition,
                            options.trace ? &trace : nullptr, &conlangWarnings);
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
            phonemes = formant::phonemizeSsml(
                ssmlSource, withNvv(frontEnd),
                formant::SsmlNvvResolver(
                    [&nvv](std::string_view name, size_t offset) {
                        try {
                            return nvv.get(name);
                        } catch (const std::invalid_argument &error) {
                            throw std::invalid_argument("SSML at byte " +
                                                        std::to_string(offset) +
                                                        ": " + error.what());
                        }
                    }),
                emphasisLevels);
            for (const std::string &line : trace) {
                status << "trace: " << line << "\n";
            }
            printConlangWarnings();
        } else if (options.usePhonemes) {
            phonemes = formant::parsePhonemes(options.phonemes);
        } else if (options.useIpa) {
            phonemes = formant::parseIpa(options.ipa);
        } else if (options.useXsampa) {
            phonemes = formant::parseXsampa(options.xsampa);
        } else if (options.useLanguageFile) {
            if (options.useLexicon) {
                phonemes = nvv.expand(text, [&](std::string_view run) {
                    return formant::phonemize(
                        run, languageDefinition, pronunciationLexicon,
                        options.trace ? &trace : nullptr, &conlangWarnings);
                });
            } else {
                phonemes = nvv.expand(text, [&](std::string_view run) {
                    return formant::phonemize(run, languageDefinition,
                                              options.trace ? &trace : nullptr,
                                              &conlangWarnings);
                });
            }
            for (const std::string &line : trace) {
                status << "trace: " << line << "\n";
            }
            printConlangWarnings();
        } else if (options.useLexicon) {
            phonemes = nvv.expand(text, [&](std::string_view run) {
                return formant::phonemize(run, options.language,
                                          pronunciationLexicon,
                                          options.trace ? &trace : nullptr);
            });
            for (const std::string &line : trace) {
                status << "trace: " << line << "\n";
            }
        } else {
            phonemes = nvv.expand(text, [&](std::string_view run) {
                return formant::phonemize(run, options.language);
            });
        }

        if (options.useFrication || options.useFricationFile) {
            // #101: run after every front end so text, IPA and phoneme inputs
            // all resolve their fricatives through the same versioned table.
            formant::FricationData active = fricationData;
            if (options.useFrication) {
                const auto found = active.profiles.find(options.fricationName);
                if (found == active.profiles.end()) {
                    throw std::invalid_argument("unknown frication profile '" +
                                                options.fricationName +
                                                "' (use --list-frication)");
                }
                const formant::FricationProfile selected = found->second;
                active.profiles.clear();
                active.profiles[options.fricationName] = selected;
            }
            const std::size_t applied =
                formant::applyFrication(phonemes, active);
            status << "Shaped " << applied
                   << " fricative segment(s) from the frication table\n";
        }

        if (breathing != formant::BreathMode::Off) {
            // Breath groups run after every front end but before symbolic
            // intonation and prosody-direct files, so explicit contours win.
            const std::string tag =
                options.useLanguageFile
                    ? languageDefinition.tag
                    : std::string(formant::languageTag(options.language));
            const formant::BreathGroupPolicy policy =
                formant::resolveBreathPolicy(breathPolicies, tag,
                                             options.breathStyle);
            formant::BreathPlanOptions planOptions;
            planOptions.mode = breathing;
            planOptions.languageTag = tag;
            planOptions.style = options.breathStyle;
            const formant::BreathPlan plan = formant::planBreathGroups(
                phonemes, policy,
                [&nvv](std::string_view name) { return nvv.get(name); },
                planOptions);
            phonemes = plan.phonemes;
            status << "Planned " << plan.groups.size() << " breath group(s), "
                   << plan.insertedEvents
                   << (breathing == formant::BreathMode::Silent ? " pause(s)"
                                                                : " breath(es)")
                   << "\n";
        }

        if (options.neuralApply) {
            // #177: learned corrections run after the front end and breath
            // planner but before symbolic intonation and prosody-direct
            // files, so explicit author contours still win.
            formant::applyNeuralProsody(phonemes, neural,
                                        options.neuralStrength);
            const formant::NeuralSummary summary = neural.summary();
            status << "Applied neural prosody from " << summary.units
                   << " unit(s)\n";
        }

        if (!options.intonationFile.empty() ||
            !options.intonationTune.empty()) {
            // Symbolic tones expand in the parameter domain after the front
            // end, then prosody-direct runs below so exact Hz targets win.
            formant::IntonationFile intonation;
            if (!options.intonationFile.empty()) {
                intonation =
                    formant::loadIntonationFile(options.intonationFile);
            }
            if (!options.intonationTune.empty()) {
                intonation.tune = options.intonationTune;
            }
            formant::applyIntonation(
                phonemes, options.language, intonation,
                formant::languageToneInventory(options.language));
            status << "Expanded symbolic intonation\n";
        }

        if (options.useProsody) {
            // Applied after every front end (including SSML) and before the
            // scheduler, so rendering, timing and visemes all see the same
            // overridden phonemes. With no file the default path is untouched.
            const formant::ProsodyFile file =
                formant::loadProsodyFile(options.prosodyFile);
            formant::applyProsodyFile(phonemes, file);
            status << "Applied " << file.directives.size()
                   << " prosody override(s)\n";
        }

        if (labMode) {
            struct LabRender {
                Voice voice;
                std::string file;
                std::string label;
                std::vector<std::pair<std::string, double>> values;
            };
            std::vector<LabRender> renders;
            std::vector<SweepAxis> axes;
            if (!options.sweepSpec.empty()) {
                axes = parseSweepSpec(options.sweepSpec);
                std::vector<int> counters(axes.size(), 0);
                while (true) {
                    Voice voice = options.voice;
                    std::vector<double> values;
                    for (std::size_t i = 0; i < axes.size(); ++i) {
                        const double value = sweepValue(axes[i], counters[i]);
                        setVoiceParameter(voice, axes[i].parameter, value);
                        values.push_back(value);
                    }
                    const std::string label = cellLabel(axes, values);
                    voice.name = label;
                    LabRender render;
                    render.voice = voice;
                    render.label = label;
                    std::ostringstream prefix;
                    prefix << std::setw(3) << std::setfill('0')
                           << (renders.size() + 1);
                    render.file = prefix.str() + "-" + label + ".wav";
                    for (std::size_t i = 0; i < axes.size(); ++i) {
                        render.values.emplace_back(axes[i].parameter,
                                                   values[i]);
                    }
                    renders.push_back(render);
                    std::size_t axis = axes.size();
                    while (axis > 0 &&
                           ++counters[axis - 1] == axes[axis - 1].steps) {
                        counters[axis - 1] = 0;
                        --axis;
                    }
                    if (axis == 0) {
                        break;
                    }
                }
            } else {
                const std::size_t comma = options.pairSpec.find(',');
                if (comma == std::string::npos) {
                    throw std::invalid_argument(
                        "--pair needs two voice specifications separated by "
                        "a comma");
                }
                const std::string first =
                    trimmed(options.pairSpec.substr(0, comma));
                const std::string second =
                    trimmed(options.pairSpec.substr(comma + 1));
                renders.push_back({voiceFromSpec(first), "a.wav", first, {}});
                renders.push_back({voiceFromSpec(second), "b.wav", second, {}});
            }

            const std::filesystem::path directory(options.labDir);
            std::error_code error;
            std::filesystem::create_directories(directory, error);
            if (error) {
                throw std::runtime_error(
                    "cannot create lab directory: " + options.labDir + ": " +
                    error.message());
            }
            const std::filesystem::path indexPath =
                options.labIndex.empty()
                    ? directory / "index.json"
                    : std::filesystem::path(options.labIndex);
            if (indexPath.has_parent_path()) {
                std::filesystem::create_directories(indexPath.parent_path(),
                                                    error);
                if (error) {
                    throw std::runtime_error(
                        "cannot create lab index directory: " +
                        error.message());
                }
            }

            std::vector<std::size_t> sampleCounts;
            for (const LabRender &render : renders) {
                const std::vector<float> rendered =
                    formant::Synthesizer(render.voice)
                        .render(phonemes, options.sampleRate);
                const std::filesystem::path output = directory / render.file;
                formant::writeAudioFile(output.string(), rendered,
                                        options.sampleRate, options.format,
                                        options.audioOptions);
                sampleCounts.push_back(rendered.size());
                status << "Wrote " << output.string() << " (" << render.label
                       << ", " << rendered.size() << " samples)\n";
            }

            std::ostringstream index;
            index << "{\n  \"mode\": "
                  << jsonString(!options.sweepSpec.empty() ? "sweep" : "pair")
                  << ",\n  \"sample-rate\": " << options.sampleRate << ",\n";
            if (!options.sweepSpec.empty()) {
                index << "  \"parameters\": [";
                for (std::size_t i = 0; i < axes.size(); ++i) {
                    if (i != 0) {
                        index << ", ";
                    }
                    index << jsonString(axes[i].parameter);
                }
                index << "],\n  \"cells\": [\n";
                for (std::size_t i = 0; i < renders.size(); ++i) {
                    const LabRender &render = renders[i];
                    index << "    {\"file\": " << jsonString(render.file)
                          << ", \"label\": " << jsonString(render.label)
                          << ", \"samples\": " << sampleCounts[i]
                          << ", \"values\": {";
                    for (std::size_t v = 0; v < render.values.size(); ++v) {
                        if (v != 0) {
                            index << ", ";
                        }
                        index << jsonString(render.values[v].first) << ": "
                              << jsonNumber(render.values[v].second);
                    }
                    index << "}}" << (i + 1 == renders.size() ? "" : ",")
                          << "\n";
                }
                index << "  ]\n}\n";
            } else {
                index << "  \"a\": {\"label\": " << jsonString(renders[0].label)
                      << ", \"file\": " << jsonString(renders[0].file)
                      << ", \"samples\": " << sampleCounts[0] << "},\n"
                      << "  \"b\": {\"label\": " << jsonString(renders[1].label)
                      << ", \"file\": " << jsonString(renders[1].file)
                      << ", \"samples\": " << sampleCounts[1] << "}\n}\n";
            }
            writeTextFile(indexPath.string(), index.str());
            status << "Wrote lab index to " << indexPath.string() << "\n";
            return 0;
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
                inputModes == 0 && !options.useSsml && !options.useSong
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
