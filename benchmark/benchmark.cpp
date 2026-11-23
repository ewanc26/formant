#include "benchmark.h"

#include "formant/hash.h"

#include <algorithm>
#include <bit>
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
         "328b7c5b046864ef693cc8a41e2ae923900968812425b42b6993d344e89ba296"},
        {"alto",
         "1748cfbaa838ebbf419213ed932e3847560a6790b821fd194c6026a5ddad75a6"},
        {"bass",
         "69179226193bdc9d3fa925722744eba305251ebe65bcf85c7f99178465c3d28a"},
        {"child",
         "44271574d4c4ffc0a34b6878b6b81a4b03a013bd38887211210c212feac75240"},
        {"robot",
         "2a90dcf45c151012c75f1a381ed50ab2cfc84f5243fdd62e28643b2454163ebf"},
        {"whisper",
         "809375bd7af807ea30d1dc933f41c5e7ed8d432006432cc3e8af3dcf31a5d405"},
    };
    return values;
}

constexpr const char *kCorpusText =
    "The quick brown fox jumps over the lazy dog. Formant speaks in sine "
    "waves only.";
constexpr int kNoisePartialCount = 12;
constexpr int kHarmonicHardCap = 4096;

std::string hashSamples(const std::vector<float> &samples) {
    std::string bytes;
    bytes.reserve(samples.size() * 4U);
    for (float sample : samples) {
        // Explicit little-endian serialisation keeps the checksum identical on
        // big-endian hosts instead of hashing the native object representation.
        const uint32_t bits = std::bit_cast<uint32_t>(sample);
        for (int shift = 24; shift >= 0; shift -= 8) {
            bytes.push_back(static_cast<char>((bits >> shift) & 0xffU));
        }
    }
    return sha256Hex(bytes);
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
            result.checksum = hashSamples(samples);
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
