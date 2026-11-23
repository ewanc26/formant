// Objective verification for Formant.
//
// The first half renders one steady vowel at five sample rates and checks the
// harmonic spectrum with a Goertzel filter. The second half measures F0,
// spectral-envelope distance and formant position against the intended
// parameter tracks, then compares the numbers with checked-in baselines. Every
// analyzer lives in formant_core, so the report describes the same synthesis
// path that callers use.

#include "formant/formant.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#ifndef FORMANT_METRICS_BASELINE
#define FORMANT_METRICS_BASELINE "tests/metrics_baseline.txt"
#endif

namespace {

int failures = 0;

#define CHECK(condition)                                                       \
    do {                                                                       \
        if (!(condition)) {                                                    \
            std::cerr << "FAIL " << __FILE__ << ":" << __LINE__ << ": "        \
                      << #condition << "\n";                                   \
            ++failures;                                                        \
        }                                                                      \
    } while (false)

struct Thresholds {
    double f0RmseHz = 5.0;
    double f0Correlation = 0.95;
    double envelopeDistance = 0.65;
    double mcdDb = 20.0;
    double formantErrorHz = 150.0;
    double formantContrast = 1.50;

    double f0RmseDrift = 1.0;
    double f0CorrelationDrift = 0.01;
    double envelopeDrift = 0.08;
    double mcdDrift = 2.0;
    double formantDrift = 30.0;
};

constexpr Thresholds kThresholds;

struct Case {
    std::string name;
    std::string source;
    std::vector<formant::Phoneme> phonemes;
    formant::Voice voice;
    int sampleRate = 44100;
    std::array<double, 2> formants{};
    bool checkFormants = false;
};

struct ReportRow {
    std::string name;
    formant::F0Metrics f0;
    formant::EnvelopeMetrics envelope;
    formant::FormantMetrics formants;
    bool checkFormants = false;
};

std::string number(double value) {
    std::ostringstream stream;
    stream << std::fixed << std::setprecision(6) << value;
    return stream.str();
}

std::vector<Case> corpus() {
    std::vector<Case> cases;

    Case vowel;
    vowel.name = "steady-vowel";
    vowel.source = "phoneme ee, 100 Hz, 200 harmonics, 44.1 kHz";
    vowel.voice = formant::Voice();
    vowel.voice.name = "verification";
    vowel.voice.pitch = 100.0;
    vowel.voice.harmonics = 200;
    vowel.voice.vibratoRate = 0.0;
    vowel.voice.vibratoDepth = 0.0;
    vowel.voice.breathiness = 0.0;
    vowel.voice.declination = 0.0;
    vowel.voice.attack = 0.002;
    vowel.voice.release = 0.002;
    vowel.voice.gain = 1.0;
    const formant::Phoneme *ee = formant::findPhoneme("ee");
    if (ee != nullptr) {
        formant::Phoneme sustained = *ee;
        sustained.duration = 1.0;
        vowel.phonemes = {sustained};
        vowel.formants = {ee->bands[0].frequency, ee->bands[1].frequency};
        vowel.checkFormants = true;
    }
    cases.push_back(std::move(vowel));

    Case hello;
    hello.name = "hello-world";
    hello.source = "text: Hello, world.";
    hello.phonemes = formant::phonemize("Hello, world.");
    hello.voice = formant::voicePreset("neutral");
    cases.push_back(std::move(hello));

    Case question;
    question.name = "question";
    question.source = "text: Are you there?";
    question.phonemes = formant::phonemize("Are you there?");
    question.voice = formant::voicePreset("neutral");
    cases.push_back(std::move(question));

    Case alto;
    alto.name = "alto-vibrato";
    alto.source = "text: Formant is a voice engine. (alto preset)";
    alto.phonemes = formant::phonemize("Formant is a voice engine.");
    alto.voice = formant::voicePreset("alto");
    cases.push_back(std::move(alto));

    Case bass;
    bass.name = "bass-vowel";
    bass.source = "phoneme ee, 95 Hz bass preset, 48 kHz";
    bass.voice = formant::voicePreset("bass");
    const formant::Phoneme *bassEe = formant::findPhoneme("ee");
    if (bassEe != nullptr) {
        formant::Phoneme sustained = *bassEe;
        sustained.duration = 1.0;
        bass.phonemes = {sustained};
        bass.formants = {bassEe->bands[0].frequency * bass.voice.formantScale,
                         bassEe->bands[1].frequency * bass.voice.formantScale};
        bass.checkFormants = true;
    }
    bass.sampleRate = 48000;
    cases.push_back(std::move(bass));

    return cases;
}

double meanVoicedF0(const formant::F0Track &track, double fallback) {
    double sum = 0.0;
    std::size_t count = 0;
    for (const formant::F0Frame &frame : track) {
        if (frame.frequency > 0.0) {
            sum += frame.frequency;
            ++count;
        }
    }
    return count > 0 ? sum / static_cast<double>(count) : fallback;
}

ReportRow analyze(const Case &item) {
    formant::AnalyzerOptions options;
    const std::vector<float> samples =
        formant::Synthesizer(item.voice).render(item.phonemes, item.sampleRate);

    ReportRow row;
    row.name = item.name;
    row.checkFormants = item.checkFormants;
    row.f0 = formant::compareF0(
        formant::estimateF0(samples, item.sampleRate, options),
        formant::expectedF0(item.phonemes, item.voice, item.sampleRate,
                            options));
    row.envelope = formant::compareEnvelope(
        formant::estimateEnvelope(samples, item.sampleRate, options),
        formant::expectedEnvelope(item.phonemes, item.voice, item.sampleRate,
                                  options));

    if (item.checkFormants) {
        const formant::F0Track expected = formant::expectedF0(
            item.phonemes, item.voice, item.sampleRate, options);
        const double fundamental = meanVoicedF0(expected, item.voice.pitch);
        const auto peaks =
            formant::estimateFormantPeaks(samples, item.sampleRate, fundamental,
                                          item.voice.harmonics, item.formants);
        row.formants = formant::compareFormants(peaks, item.formants);
    }
    return row;
}

std::map<std::string, ReportRow> readBaseline(const std::string &path) {
    std::ifstream input(path);
    if (!input) {
        std::cerr << "FAIL cannot open baseline: " << path << "\n";
        ++failures;
        return {};
    }

    std::map<std::string, ReportRow> rows;
    std::string line;
    while (std::getline(input, line)) {
        if (line.empty() || line[0] == '#') {
            continue;
        }
        std::istringstream fields(line);
        ReportRow row;
        double rmseCents = 0.0;
        if (!(fields >> row.name >> row.f0.rmseHz >> rmseCents >>
              row.f0.correlation >> row.envelope.logDistance >>
              row.envelope.mcdDb >> row.formants.maxErrorHz >>
              row.formants.minContrast)) {
            std::cerr << "FAIL malformed baseline line: " << line << "\n";
            ++failures;
            continue;
        }
        row.f0.rmseCents = rmseCents;
        rows[row.name] = row;
    }
    return rows;
}

void writeMetrics(std::ostream &output, const std::vector<ReportRow> &rows,
                  const std::vector<Case> &cases) {
    output << "# name f0_rmse_hz f0_rmse_cents f0_correlation "
              "envelope_log_distance mcd_db formant_error_hz "
              "formant_contrast\n";
    for (std::size_t i = 0; i < rows.size(); ++i) {
        const ReportRow &row = rows[i];
        output << row.name << ' ' << number(row.f0.rmseHz) << ' '
               << number(row.f0.rmseCents) << ' ' << number(row.f0.correlation)
               << ' ' << number(row.envelope.logDistance) << ' '
               << number(row.envelope.mcdDb) << ' '
               << number(row.formants.maxErrorHz) << ' '
               << number(row.formants.minContrast) << " # " << cases[i].source
               << "\n";
    }
}

void verifyMetrics(const std::vector<ReportRow> &rows,
                   const std::string &baselinePath) {
    const std::map<std::string, ReportRow> baseline =
        readBaseline(baselinePath);

    for (const ReportRow &row : rows) {
        const auto previous = baseline.find(row.name);
        if (previous == baseline.end()) {
            std::cerr << "FAIL no baseline for corpus case " << row.name
                      << "\n";
            ++failures;
            continue;
        }
        const ReportRow &base = previous->second;
        const bool qualityOkay =
            row.f0.rmseHz <= kThresholds.f0RmseHz &&
            row.f0.correlation >= kThresholds.f0Correlation &&
            row.envelope.logDistance <= kThresholds.envelopeDistance &&
            row.envelope.mcdDb <= kThresholds.mcdDb &&
            (!row.checkFormants ||
             (row.formants.maxErrorHz <= kThresholds.formantErrorHz &&
              row.formants.minContrast >= kThresholds.formantContrast));
        const bool baselineOkay =
            row.f0.rmseHz <= base.f0.rmseHz + kThresholds.f0RmseDrift &&
            row.f0.correlation >=
                base.f0.correlation - kThresholds.f0CorrelationDrift &&
            row.envelope.logDistance <=
                base.envelope.logDistance + kThresholds.envelopeDrift &&
            row.envelope.mcdDb <= base.envelope.mcdDb + kThresholds.mcdDrift &&
            (!row.checkFormants ||
             row.formants.maxErrorHz <=
                 base.formants.maxErrorHz + kThresholds.formantDrift);

        if (!qualityOkay) {
            std::cerr << "FAIL metric threshold exceeded for " << row.name
                      << "\n";
            ++failures;
        }
        if (!baselineOkay) {
            std::cerr << "FAIL baseline drift for " << row.name << "\n";
            ++failures;
        }
    }
}

void verifySampleRates() {
    const formant::Phoneme *source = formant::findPhoneme("ee");
    if (source == nullptr) {
        std::cerr << "FAIL missing test vowel 'ee'\n";
        ++failures;
        return;
    }
    formant::Phoneme vowel = *source;
    vowel.duration = 1.0;

    formant::Voice voice;
    voice.pitch = 100.0;
    voice.harmonics = 200; // reaches beyond Nyquist at 22050 Hz
    voice.vibratoRate = 0.0;
    voice.vibratoDepth = 0.0;
    voice.breathiness = 0.0;
    voice.declination = 0.0;
    voice.attack = 0.002;
    voice.release = 0.002;
    voice.gain = 1.0;

    const std::array<double, 2> expected = {source->bands[0].frequency,
                                            source->bands[1].frequency};
    for (int sampleRate : {22050, 44100, 48000, 96000, 192000}) {
        const std::vector<float> samples =
            formant::Synthesizer(voice).render({vowel}, sampleRate);
        CHECK(samples.size() == static_cast<std::size_t>(sampleRate));
        if (samples.size() != static_cast<std::size_t>(sampleRate)) {
            continue;
        }
        const formant::F0Track target =
            formant::expectedF0({vowel}, voice, sampleRate);
        const double fundamental = meanVoicedF0(target, voice.pitch);
        const auto peaks = formant::estimateFormantPeaks(
            samples, sampleRate, fundamental, voice.harmonics, expected);
        const formant::FormantMetrics metrics =
            formant::compareFormants(peaks, expected);

        CHECK(peaks[0].energy > 0.0);
        CHECK(peaks[1].energy > 0.0);
        CHECK(metrics.maxErrorHz <= kThresholds.formantErrorHz);
        CHECK(metrics.minContrast >= kThresholds.formantContrast);
        std::cout << sampleRate << " Hz F1 error " << number(metrics.errorHz[0])
                  << " Hz, F2 error " << number(metrics.errorHz[1])
                  << " Hz, contrast " << number(metrics.minContrast) << "\n";
    }
}

// Parameter-domain transposition must move the measured quantities, not just
// the voice fields. A steady vowel is rendered at the base pitch, an octave
// up, and with its formants widened; the analyzers then check that F0 and
// resonance positions moved by the requested ratios while the other axis held.
void verifyTransposition() {
    const formant::Phoneme *source = formant::findPhoneme("ee");
    if (source == nullptr) {
        std::cerr << "FAIL missing test vowel 'ee'\n";
        ++failures;
        return;
    }
    formant::Phoneme vowel = *source;
    vowel.duration = 1.0;

    formant::Voice base = formant::voicePreset("robot");
    base.pitch = 100.0;
    base.harmonics = 200;
    base.declination = 0.0;

    formant::AnalyzerOptions options;
    options.maxF0 = 800.0;
    const std::array<double, 2> expected = {source->bands[0].frequency,
                                            source->bands[1].frequency};

    auto measureF0 = [&](const formant::Voice &voice) {
        const std::vector<float> samples =
            formant::Synthesizer(voice).render({vowel}, 44100);
        return meanVoicedF0(formant::estimateF0(samples, 44100, options),
                            voice.pitch);
    };

    const double plainF0 = measureF0(base);
    const formant::Voice octave = formant::shiftPitch(base, 1200.0);
    const double octaveF0 = measureF0(octave);
    CHECK(std::abs(octaveF0 / plainF0 - 2.0) < 0.03);

    const formant::Voice wider = formant::scaleFormants(base, 1.25);
    const double widerF0 = measureF0(wider);
    CHECK(std::abs(widerF0 / plainF0 - 1.0) < 0.03);

    const std::vector<float> plainSamples =
        formant::Synthesizer(base).render({vowel}, 44100);
    const std::vector<float> widerSamples =
        formant::Synthesizer(wider).render({vowel}, 44100);
    const auto plainPeaks = formant::estimateFormantPeaks(
        plainSamples, 44100, plainF0, base.harmonics, expected);
    const std::array<double, 2> widerExpected = {expected[0] * 1.25,
                                                 expected[1] * 1.25};
    const auto widerPeaks = formant::estimateFormantPeaks(
        widerSamples, 44100, widerF0, wider.harmonics, widerExpected);
    CHECK(widerPeaks[0].frequency > plainPeaks[0].frequency);
    CHECK(widerPeaks[1].frequency > plainPeaks[1].frequency);
    CHECK(formant::compareFormants(widerPeaks, widerExpected).maxErrorHz <=
          kThresholds.formantErrorHz);

    // A large upward shift is finite and normalised: the #108 ceiling taper
    // fades harmonics as the stack moves rather than clipping or aliasing.
    const formant::Voice extreme = formant::shiftPitch(base, 3600.0);
    const std::vector<float> extremeSamples =
        formant::Synthesizer(extreme).render({vowel}, 44100);
    CHECK(extremeSamples ==
          formant::Synthesizer(extreme).render({vowel}, 44100));
    CHECK(*std::max_element(extremeSamples.begin(), extremeSamples.end()) <=
          0.9001f);
    CHECK(*std::min_element(extremeSamples.begin(), extremeSamples.end()) >=
          -0.9001f);

    std::cout << "transposition: F0 " << number(plainF0) << " -> "
              << number(octaveF0) << " Hz at +1200 cents, formants x1.25 at "
              << number(widerF0) << " Hz\n";
}

// Renders a deliberately wrong voice but scores it against the intended
// tracks. A CI job passes only when the metrics reject the regression, which
// demonstrates the thresholds are not vacuous.
bool regressionIsDetected() {
    const std::vector<Case> cases = corpus();
    if (cases.empty() || cases[0].phonemes.empty()) {
        return false;
    }
    const Case &item = cases[0];
    formant::Voice broken = item.voice;
    broken.pitch *= 1.25;
    broken.formantScale *= 1.30;

    formant::AnalyzerOptions options;
    const std::vector<float> samples =
        formant::Synthesizer(broken).render(item.phonemes, item.sampleRate);
    const formant::F0Metrics f0 = formant::compareF0(
        formant::estimateF0(samples, item.sampleRate, options),
        formant::expectedF0(item.phonemes, item.voice, item.sampleRate,
                            options));
    const formant::EnvelopeMetrics envelope = formant::compareEnvelope(
        formant::estimateEnvelope(samples, item.sampleRate, options),
        formant::expectedEnvelope(item.phonemes, item.voice, item.sampleRate,
                                  options));
    const bool pitchDetected = f0.rmseHz > kThresholds.f0RmseHz ||
                               f0.correlation < kThresholds.f0Correlation;
    const bool spectralDetected =
        envelope.mcdDb > kThresholds.mcdDb ||
        envelope.logDistance > kThresholds.envelopeDistance;
    std::cout << "deliberate regression: F0 RMSE " << number(f0.rmseHz)
              << " Hz, correlation " << number(f0.correlation) << ", MCD "
              << number(envelope.mcdDb) << " dB, envelope "
              << number(envelope.logDistance) << "\n";
    return pitchDetected && spectralDetected;
}

void printHelp() {
    std::cout << "usage: formant_verification [options]\n"
                 "  --baseline PATH      compare metrics with PATH\n"
                 "  --report PATH        write a metric report\n"
                 "  --update-baseline P  write new baselines and exit\n"
                 "  --regression-check   verify a deliberate regression fails\n"
                 "  --help               show this text\n";
}

} // namespace

int main(int argc, char **argv) {
    std::string baselinePath = FORMANT_METRICS_BASELINE;
    std::string reportPath;
    std::string updatePath;
    bool regressionCheck = false;

    for (int i = 1; i < argc; ++i) {
        const std::string argument = argv[i];
        auto value = [&](const char *flag) -> std::string {
            if (i + 1 >= argc) {
                std::cerr << "missing value for " << flag << "\n";
                std::exit(2);
            }
            return argv[++i];
        };
        if (argument == "--baseline") {
            baselinePath = value("--baseline");
        } else if (argument == "--report") {
            reportPath = value("--report");
        } else if (argument == "--update-baseline") {
            updatePath = value("--update-baseline");
        } else if (argument == "--regression-check") {
            regressionCheck = true;
        } else if (argument == "--help") {
            printHelp();
            return 0;
        } else {
            std::cerr << "unknown option: " << argument << "\n";
            printHelp();
            return 2;
        }
    }

    if (regressionCheck) {
        if (!regressionIsDetected()) {
            std::cerr
                << "FAIL deliberate synthesis regression was not detected\n";
            return 1;
        }
        std::cout << "deliberate regression detected by objective metrics\n";
        return 0;
    }

    verifySampleRates();
    verifyTransposition();
    const std::vector<Case> cases = corpus();
    std::vector<ReportRow> rows;
    rows.reserve(cases.size());
    for (const Case &item : cases) {
        rows.push_back(analyze(item));
        const ReportRow &row = rows.back();
        std::cout << row.name << ": F0 RMSE " << number(row.f0.rmseHz)
                  << " Hz, correlation " << number(row.f0.correlation)
                  << ", envelope " << number(row.envelope.logDistance)
                  << ", MCD " << number(row.envelope.mcdDb) << " dB\n";
    }

    if (!updatePath.empty()) {
        std::ofstream output(updatePath);
        if (!output) {
            std::cerr << "FAIL cannot write baseline: " << updatePath << "\n";
            return 1;
        }
        writeMetrics(output, rows, cases);
        std::cout << "wrote " << rows.size() << " baseline rows to "
                  << updatePath << "\n";
    } else {
        verifyMetrics(rows, baselinePath);
    }

    if (!reportPath.empty()) {
        std::ofstream output(reportPath);
        if (!output) {
            std::cerr << "FAIL cannot write report: " << reportPath << "\n";
            return 1;
        }
        writeMetrics(output, rows, cases);
    }

    if (failures != 0) {
        std::cerr << failures << " verification check(s) failed\n";
        return 1;
    }
    std::cout << "formant and objective-metric verification passed\n";
    return 0;
}
