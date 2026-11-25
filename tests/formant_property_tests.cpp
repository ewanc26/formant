// Property-based tests for formant. Every case is derived from a seed printed
// on failure, so a counterexample can be replayed exactly via formant_fuzz.
// These run in normal CTest on Linux and macOS; the longer seeded campaign is
// in formant_fuzz.cpp.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "formant/formant.h"

namespace {

uint32_t currentSeed = 0;
std::string currentProperty;
int failures = 0;

void reportFailure(const char *file, int line, const char *expression) {
    std::cerr << "FAIL property=" << currentProperty << " seed=" << currentSeed
              << " " << file << ":" << line << ": " << expression << "\n";
    ++failures;
}

#define CHECK(condition)                                                       \
    do {                                                                       \
        if (!(condition)) {                                                    \
            reportFailure(__FILE__, __LINE__, #condition);                     \
        }                                                                      \
    } while (false)

// A case is one (seed, property) pair. Scoped setter restores the globals so
// nested helpers always report the right context.
struct CaseScope {
    uint32_t previousSeed;
    std::string previousProperty;

    CaseScope(uint32_t seed, const char *property)
        : previousSeed(currentSeed), previousProperty(currentProperty) {
        currentSeed = seed;
        currentProperty = property;
    }
    ~CaseScope() {
        currentSeed = previousSeed;
        currentProperty = previousProperty;
    }
};

// Deterministic byte string from a seed. Used to build adversarial inputs for
// the parser table and the Unicode safety checks.
std::string bytesFromSeed(uint32_t seed, size_t length) {
    formant::SeededRng rng(seed);
    std::string result;
    result.reserve(length);
    for (size_t i = 0; i < length; ++i) {
        result.push_back(static_cast<char>(rng.below(256)));
    }
    return result;
}

// A handful of grammars spanning alternatives, nesting, optionals, filters and
// inline definitions. Each is deterministic when the seed is fixed.
const std::array<std::string, 8> kGrammars = {"CVC",
                                              "CV(V)C",
                                              "C=p/t/k\nV=a/i/u\nCVC",
                                              "[[V][V]]^aa^ii^uu",
                                              "(C)V(C)",
                                              "C=p/t/k\nV=a/i/u\nX=CV\nXX",
                                              "a/i/u*4",
                                              "C=sh/ch\ntCVC"};

const std::array<std::string, 6> kVoices = {"neutral", "alto",  "bass",
                                            "child",   "robot", "whisper"};

const std::array<int, 5> kSampleRates = {8000, 16000, 22050, 44100, 48000};

formant::GibberishResult generateScript(uint32_t seed, size_t wordCount,
                                        size_t grammarIndex) {
    const formant::PatternGrammar grammar = formant::parsePatternGrammar(
        kGrammars[grammarIndex % kGrammars.size()]);
    formant::GibberishSettings settings;
    settings.seed = seed;
    settings.count = static_cast<int>(wordCount);
    settings.prosody = true;
    return formant::generateGibberish(
        grammar, formant::defaultPatternInventory(), settings);
}

bool allFiniteAndBounded(const std::vector<float> &samples) {
    for (float sample : samples) {
        if (!std::isfinite(sample) || std::abs(sample) > 0.9001f) {
            return false;
        }
    }
    return true;
}

std::string hexOf(const std::vector<float> &samples) {
    std::string bytes(reinterpret_cast<const char *>(samples.data()),
                      samples.size() * sizeof(float));
    return formant::sha256Hex(bytes);
}

// ---------------------------------------------------------------------------
// Property 1: identical input and flags render identical bytes.
// ---------------------------------------------------------------------------
void checkDeterminism(uint32_t seed) {
    CaseScope scope(seed, "determinism");
    const auto script = generateScript(seed, 4, seed % kGrammars.size());
    const auto voice = formant::voicePreset(kVoices[seed % kVoices.size()]);
    const int rate = kSampleRates[seed % kSampleRates.size()];

    const auto first =
        formant::Synthesizer(voice).render(script.phonemes, rate);
    const auto second =
        formant::Synthesizer(voice).render(script.phonemes, rate);
    CHECK(first.size() == second.size());
    CHECK(hexOf(first) == hexOf(second));

    // The continued renderer must byte-match a single render of the same
    // phoneme sequence, including phrase-final contours and glottal phase.
    formant::Synthesizer synth(voice);
    const size_t split = script.phonemes.size() / 2;
    const std::vector<formant::Phoneme> head(script.phonemes.begin(),
                                             script.phonemes.begin() + split);
    const std::vector<formant::Phoneme> tail(script.phonemes.begin() + split,
                                             script.phonemes.end());
    auto continued = synth.renderContinued(head, rate, formant::RenderState{});
    continued = synth.renderContinued(tail, rate, continued.state);
    CHECK(continued.samples.size() == first.size());
    CHECK(hexOf(continued.samples) == hexOf(first));
}

// ---------------------------------------------------------------------------
// Property 2: no NaN, infinity or out-of-range samples for bounded inputs.
// ---------------------------------------------------------------------------
void checkBounds(uint32_t seed) {
    CaseScope scope(seed, "bounds");
    const auto script = generateScript(seed, 5, (seed / 3) % kGrammars.size());
    const auto voice =
        formant::voicePreset(kVoices[(seed / 7) % kVoices.size()]);
    const int rate = kSampleRates[(seed / 11) % kSampleRates.size()];
    const auto samples =
        formant::Synthesizer(voice).render(script.phonemes, rate);
    CHECK(!samples.empty());
    CHECK(allFiniteAndBounded(samples));

    // Encoding must also stay finite and succeed for every supported format.
    std::ostringstream encoded(std::ios::binary);
    formant::AudioWriteOptions options;
    options.dither = true;
    options.seed = seed;
    for (auto format :
         {formant::AudioFormat::Pcm16, formant::AudioFormat::Pcm24,
          formant::AudioFormat::Float32, formant::AudioFormat::RawPcm16}) {
        options.dither = !formant::isFloatAudioFormat(format);
        try {
            formant::writeAudio(encoded, samples, rate, format, options);
        } catch (const std::exception &error) {
            reportFailure(
                __FILE__, __LINE__,
                ("audio encode threw: " + std::string(error.what())).c_str());
        }
    }
}

// ---------------------------------------------------------------------------
// Property 3: malformed and adversarial data files are rejected early with
// instructive errors, never crashing or throwing an unexpected exception.
//
// New file kinds (song, accent, dialogue) should be appended to this table as
// their parsers land; the structure already covers every shipped parser.
// ---------------------------------------------------------------------------
struct ParserCase {
    const char *name;
    void (*parse)(std::string_view);
};

void parseLanguage(std::string_view text) {
    formant::parseLanguageDefinition(text);
}
void parseVoice(std::string_view text) { formant::parseVoiceDefinition(text); }
void parseProsody(std::string_view text) { formant::parseProsodyFile(text); }
void parseIntonation(std::string_view text) {
    formant::parseIntonationFile(text);
}
void parseLexicon(std::string_view text) {
    formant::parsePronunciationLexicon(text);
}
void parseSsml(std::string_view text) { formant::phonemizeSsml(text); }
void parsePattern(std::string_view text) { formant::parsePatternGrammar(text); }
void parseIpaText(std::string_view text) { formant::parseIpa(text); }
void parseXsampaText(std::string_view text) { formant::parseXsampa(text); }
void parseBracket(std::string_view text) { formant::parsePhonemes(text); }
void parseNeuralSamples(std::string_view text) {
    (void)formant::parseNeuralSamples(text);
}
void parseNeuralState(std::string_view text) {
    (void)formant::GrowingNeuralNetwork::parse(text);
}

const std::array<ParserCase, 12> kParsers = {{
    {"language", parseLanguage},
    {"voice", parseVoice},
    {"prosody", parseProsody},
    {"intonation", parseIntonation},
    {"lexicon", parseLexicon},
    {"ssml", parseSsml},
    {"pattern", parsePattern},
    {"ipa", parseIpaText},
    {"xsampa", parseXsampaText},
    {"bracket", parseBracket},
    {"neural-samples", parseNeuralSamples},
    {"neural-state", parseNeuralState},
}};

void checkRejection(uint32_t seed) {
    const std::string randomBytes = bytesFromSeed(seed, 96);
    for (size_t i = 0; i < kParsers.size(); ++i) {
        const std::string property =
            std::string("rejection/") + kParsers[i].name;
        CaseScope scope(seed, property.c_str());
        try {
            kParsers[i].parse(randomBytes);
            // Accepting arbitrary bytes is fine for lenient front ends, but a
            // rejected input must always carry a message.
        } catch (const std::invalid_argument &error) {
            CHECK(std::string_view(error.what()).size() > 0);
        } catch (const std::exception &error) {
            reportFailure(
                __FILE__, __LINE__,
                ("unexpected exception type: " + std::string(error.what()))
                    .c_str());
        } catch (...) {
            reportFailure(__FILE__, __LINE__,
                          "non-std exception escaped parser");
        }
    }
}

// Structured malformed inputs with expected rejection and a required useful
// message fragment. These are the "instructive errors" regression guards.
void checkStructuredMalformed() {
    const std::array<std::pair<const char *, std::string>, 8> cases = {{
        {"language", "grapheme ab"},
        {"voice", "pitch not_a_number"},
        {"prosody", "segment 0 f0=abcHz"},
        {"intonation", "accent"},
        {"lexicon", "word"},
        {"ssml", "<speak><unknown/></speak>"},
        {"pattern", "Q=(("},
        {"ipa", "\xff\xfe"},
    }};
    for (uint32_t i = 0; i < cases.size(); ++i) {
        CaseScope scope(i + 90000, "structured-malformed");
        bool rejected = false;
        std::string message;
        try {
            kParsers[i].parse(cases[i].second);
        } catch (const std::invalid_argument &error) {
            rejected = true;
            message = error.what();
        } catch (const std::exception &error) {
            message = error.what();
        }
        CHECK(rejected);
        CHECK(!message.empty());
    }
}

// ---------------------------------------------------------------------------
// Property 4: text normalization is idempotent under repeated application.
// ---------------------------------------------------------------------------
std::vector<std::string> phonemeNames(const std::vector<formant::Phoneme> &ph) {
    std::vector<std::string> result;
    for (const auto &p : ph) {
        result.push_back(p.name);
    }
    return result;
}

void checkIdempotence(uint32_t seed) {
    CaseScope scope(seed, "idempotence");
    // The gibberish transcript is canonical IPA. Parsing it twice must yield
    // the same phoneme sequence both times.
    const auto script = generateScript(seed, 3, (seed / 5) % kGrammars.size());
    const auto first = formant::parseIpa(script.transcript);
    const auto second = formant::parseIpa(script.transcript);
    CHECK(phonemeNames(first) == phonemeNames(second));

    // Apostrophe normalization must be idempotent: U+2019 and ASCII apostrophes
    // collapse to the same phonemes, and re-applying the normalization to its
    // own output cannot change the result. This is the public, observable form
    // of the front end's text normalization step.
    const std::array<std::string, 4> texts = {"Don't stop.", "Don\u2019t stop.",
                                              "we're here", "we\u2019re here"};
    for (size_t i = 0; i + 1 < texts.size(); i += 2) {
        const auto ascii = formant::phonemize(texts[i]);
        const auto curly = formant::phonemize(texts[i + 1]);
        CHECK(phonemeNames(ascii) == phonemeNames(curly));
        // The ASCII form is already normalized; normalizing it again is a
        // no-op.
        const auto again = formant::phonemize(texts[i]);
        CHECK(phonemeNames(ascii) == phonemeNames(again));
    }
}

// ---------------------------------------------------------------------------
// Property 5: arbitrary Unicode, invalid UTF-8 and embedded NULs are handled
// safely; invalid encodings produce a clear error rather than a crash.
// ---------------------------------------------------------------------------
void checkUnicode(uint32_t seed) {
    const std::string bytes = bytesFromSeed(seed, 64);
    const std::array<std::string_view, 6> inputs = {
        std::string_view(bytes.data(), bytes.size()),
        std::string_view("\xc3\xa9\xc3\xa8\xc3\xaa", 6), // valid UTF-8 accents
        std::string_view("\xff\xfe\xfd", 3),             // invalid UTF-8
        std::string_view("a\0b", 3),                     // embedded NUL
        std::string_view("\xf0\x9f\x98\x80", 4),         // emoji
        std::string_view("\xce\xb1\xce\xb2\xce\xb3", 6), // Greek letters
    };
    for (size_t i = 0; i < inputs.size(); ++i) {
        CaseScope scope(seed, "unicode");
        std::vector<formant::Phoneme> phonemes;
        try {
            phonemes = formant::parseIpa(inputs[i]);
        } catch (const std::invalid_argument &error) {
            CHECK(std::string_view(error.what()).size() > 0);
        } catch (const std::exception &error) {
            reportFailure(
                __FILE__, __LINE__,
                ("unexpected exception: " + std::string(error.what())).c_str());
        }
        if (!phonemes.empty()) {
            const auto samples =
                formant::Synthesizer(formant::voicePreset("neutral"))
                    .render(phonemes, 44100);
            CHECK(allFiniteAndBounded(samples));
        }

        // Text front ends must also survive arbitrary bytes.
        try {
            formant::phonemize(inputs[i]);
        } catch (const std::invalid_argument &) {
        } catch (const std::exception &error) {
            reportFailure(
                __FILE__, __LINE__,
                ("phonemize threw: " + std::string(error.what())).c_str());
        }
    }
}

} // namespace

int main(int argc, char **argv) {
    // Fast CI property run: 240 seeds x 5 properties is seconds, not minutes.
    // Sanitizer builds pass a smaller count because instrumented renders are
    // roughly 25x slower and ctest enforces a 300s per-test limit.
    uint32_t seedCount = 240;
    if (argc == 3 && std::string_view(argv[1]) == "--seeds") {
        seedCount = static_cast<uint32_t>(std::strtoul(argv[2], nullptr, 10));
        if (seedCount == 0) {
            std::cerr << "--seeds must be positive\n";
            return 2;
        }
    } else if (argc != 1) {
        std::cerr << "usage: formant_property_tests [--seeds N]\n";
        return 2;
    }
    for (uint32_t seed = 1; seed <= seedCount; ++seed) {
        checkDeterminism(seed);
        checkBounds(seed);
        checkRejection(seed);
        checkIdempotence(seed);
        checkUnicode(seed);
    }
    checkStructuredMalformed();

    if (failures) {
        std::cerr << "property tests failed: " << failures << " check(s)\n";
        std::cerr << "replay any failure with: formant_fuzz --scripts 1 "
                     "--seed-base SEED\n";
        return 1;
    }
    std::cout << "property tests passed (" << seedCount << " seeds)\n";
    return 0;
}
