#pragma once

// Performance benchmark harness. It lives outside src/ and include/ on
// purpose: the engine has no wall-clock dependency, and the byte-identity
// sine-only scan covers those two directories only.

#include "formant/formant.h"

#include <cstddef>
#include <string>
#include <vector>

namespace formant {

struct BenchmarkCase {
    std::string name;
    Voice voice;
    int sampleRate = 44100;
    std::string text;
};

struct BenchmarkResult {
    std::string name;
    std::string voiceName;
    int sampleRate = 0;
    std::size_t samples = 0;
    double audioSeconds = 0.0;
    double cpuSeconds = 0.0;
    double realTimeFactor = 0.0;
    double samplesPerSecond = 0.0;
    double f0Hz = 0.0;
    int harmonics = 0;
    int noisePartials = 0;
    std::string checksum;
    bool checksumOk = false;
};

// A fixed corpus: six presets render the same built-in sentence. Inputs,
// sample rates and voices are constants so results are comparable across
// machines and releases.
std::vector<BenchmarkCase> benchmarkCorpus();

// Renders the case once as a warmup, then `repeats` timed renders and reports
// the median CPU time. The checksum covers the canonical 16-bit PCM bytes of
// the first render, which is the project's byte-identity contract.
BenchmarkResult runBenchmark(const BenchmarkCase &testCase, int repeats);

std::string benchmarkReportHeader();
std::string benchmarkReportLine(const BenchmarkResult &result);

// The checked-in expected-results data file, generated from the compiled-in
// expectations so documentation cannot drift from the harness.
std::string benchmarkExpectedReport();

} // namespace formant
