#include "benchmark.h"

#include "formant/hash.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <map>
#include <sstream>
#include <vector>

namespace formant {
namespace {

// Fixed, checked-in expectations. `benchmark/expected.txt` is generated from
// this table with --benchmark-expected and committed alongside the harness.
const std::map<std::string, std::string> &expectedChecksums() {
    static const std::map<std::string, std::string> values = {
        {"neutral",
         "f8162e87e86c7520a9c7d8663110011289d3c2238a15af78bb0964b0029d5af3"},
        {"alto",
         "5f6cb15b1a334cef6038d4f3d25573da31d322eb5bc4828b2cdcf651b60ba190"},
        {"bass",
         "c7289efa154a038a2729cba4a3cbe3139b313318bcac34002d0ee4489828028e"},
        {"child",
         "9dee0f51802d490bb62df844da93fd2d63bdeaa4397e3b7aa07e44518589929e"},
        {"robot",
         "6cc88ae19b59919455bd2ed261829f8d9ee6ce2fec6a7dcfe60b60b2220ee753"},
        {"whisper",
         "4adda038c58ae26cd777f7583017f294bf65c7fa9d52b9b64e6b919f3b8d55d9"},
    };
    return values;
}

constexpr const char *kCorpusText =
    "The quick brown fox jumps over the lazy dog. Formant speaks in sine "
    "waves only.";
constexpr int kNoisePartialCount = 12;
constexpr int kHarmonicHardCap = 4096;

// Hash the canonical 16-bit PCM bytes, which are the project's byte-identity
// contract, rather than raw float bits. Alternative compilers and sanitizer
// builds can differ in the last float bits without changing the audible or
// written 16-bit output; hashing the quantised stream keeps the checksum
// stable for every supported build while still catching real render drift.
std::string hashSamples(const std::vector<float> &samples, int sampleRate) {
    std::ostringstream pcm(std::ios::binary);
    writeAudio(pcm, samples, sampleRate, AudioFormat::RawPcm16);
    return sha256Hex(pcm.str());
}

double median(std::vector<double> values) {
    std::sort(values.begin(), values.end());
    const std::size_t middle = values.size() / 2;
    if (values.size() % 2 == 1) {
        return values[middle];
    }
    return 0.5 * (values[middle - 1] + values[middle]);
}

std::string fixed(double value, int precision = 6) {
    std::ostringstream out;
    out << std::fixed << std::setprecision(precision) << value;
    return out.str();
}

} // namespace

std::vector<BenchmarkCase> benchmarkCorpus() {
    std::vector<BenchmarkCase> cases;
    for (const std::string &name : voicePresetNames()) {
        BenchmarkCase testCase;
        testCase.name = name;
        testCase.voice = voicePreset(name);
        testCase.sampleRate = 44100;
        testCase.text = kCorpusText;
        cases.push_back(std::move(testCase));
    }
    return cases;
}

BenchmarkResult runBenchmark(const BenchmarkCase &testCase, int repeats) {
    if (repeats < 1) {
        throw std::invalid_argument("benchmark repeats must be positive");
    }

    const std::vector<Phoneme> phonemes = phonemize(testCase.text);
    BenchmarkResult result;
    result.name = testCase.name;
    result.voiceName = testCase.voice.name;
    result.sampleRate = testCase.sampleRate;
    result.f0Hz = testCase.voice.pitch;

    bool hasVoiced = false;
    bool hasUnvoiced = false;
    for (const Phoneme &phoneme : phonemes) {
        if (phoneme.kind == PhonemeKind::Silence) {
            continue;
        }
        if (phoneme.voiced) {
            hasVoiced = true;
        } else {
            hasUnvoiced = true;
        }
    }

    const double ceilingHz = 0.49 * static_cast<double>(testCase.sampleRate);
    const int ceilingHarmonics =
        result.f0Hz > 0.0
            ? static_cast<int>(std::floor(ceilingHz / result.f0Hz))
            : 0;
    result.harmonics = std::min(
        {testCase.voice.harmonics, kHarmonicHardCap, ceilingHarmonics});
    result.noisePartials =
        ((hasVoiced && testCase.voice.breathiness > 0.0) || hasUnvoiced)
            ? kNoisePartialCount
            : 0;

    // A warmup render primes allocator and code caches; it is never timed.
    const Synthesizer synthesizer(testCase.voice);
    (void)synthesizer.render(phonemes, testCase.sampleRate);

    std::vector<double> cpuSeconds;
    cpuSeconds.reserve(static_cast<std::size_t>(repeats));
    for (int i = 0; i < repeats; ++i) {
        const auto start = std::chrono::steady_clock::now();
        const std::vector<float> samples =
            synthesizer.render(phonemes, testCase.sampleRate);
        const auto end = std::chrono::steady_clock::now();
        cpuSeconds.push_back(
            std::chrono::duration<double>(end - start).count());
        if (i == 0) {
            result.samples = samples.size();
            result.checksum = hashSamples(samples, testCase.sampleRate);
        }
    }

    result.audioSeconds = static_cast<double>(result.samples) /
                          static_cast<double>(testCase.sampleRate);
    result.cpuSeconds = median(cpuSeconds);
    result.samplesPerSecond =
        result.cpuSeconds > 0.0
            ? static_cast<double>(result.samples) / result.cpuSeconds
            : 0.0;
    result.realTimeFactor =
        result.cpuSeconds > 0.0 ? result.audioSeconds / result.cpuSeconds : 0.0;

    const auto expected = expectedChecksums().find(result.name);
    result.checksumOk = expected != expectedChecksums().end() &&
                        !expected->second.empty() &&
                        expected->second == result.checksum;
    return result;
}

std::string benchmarkReportHeader() {
    return "case\tvoice\tsample_rate\tsamples\taudio_s\tcpu_s\trtf\t"
           "samples_s\tf0_hz\tharmonics\tnoise_partials\tchecksum\tstatus\n";
}

std::string benchmarkReportLine(const BenchmarkResult &result) {
    std::ostringstream out;
    out << result.name << '\t' << result.voiceName << '\t' << result.sampleRate
        << '\t' << result.samples << '\t' << fixed(result.audioSeconds) << '\t'
        << fixed(result.cpuSeconds) << '\t' << fixed(result.realTimeFactor)
        << '\t' << fixed(result.samplesPerSecond, 1) << '\t'
        << fixed(result.f0Hz, 1) << '\t' << result.harmonics << '\t'
        << result.noisePartials << '\t' << result.checksum << '\t'
        << (result.checksumOk ? "ok" : "FAIL") << '\n';
    return out.str();
}

std::string benchmarkExpectedReport() {
    std::ostringstream out;
    out << "# Built-in benchmark corpus expectations. Generated by\n"
        << "# formant --benchmark-expected FILE.\n"
        << "# Corpus: " << kCorpusText << "\n"
        << benchmarkReportHeader();
    for (const BenchmarkCase &testCase : benchmarkCorpus()) {
        const auto expected = expectedChecksums().find(testCase.name);
        const std::string checksum =
            expected == expectedChecksums().end() ? "" : expected->second;
        out << testCase.name << '\t' << testCase.voice.name << '\t'
            << testCase.sampleRate << "\t-\t-\t-\t-\t-\t"
            << fixed(testCase.voice.pitch, 1) << "\t-\t-\t" << checksum
            << "\tok\n";
    }
    return out.str();
}

} // namespace formant
