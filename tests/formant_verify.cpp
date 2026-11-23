// Offline formant-trajectory verification (issue #104).
//
// This is an opt-in tool, not part of default ctest: it renders a small corpus,
// samples the *intended* F1-F4 trajectory from the same shared prosody model
// the synthesiser uses, measures F1-F4 from the rendered audio with the
// dependency-free harmonic peak picker, and compares both against a committed
// golden fixture. Two numbers are reported per band:
//
//   intended vs golden  - detects a changed phoneme table deterministically
//   measured vs intended - an acoustic diff of the rendered resonances
//
// A deliberately broken formant table is detected by --self-test-broken, which
// offsets the corpus bands and requires the golden comparison to fire. The
// fixture is only rewritten by an explicit --write-golden, never by a normal
// run. No Python, external tracker or network access is needed for the report.

#include "formant/formant.h"
#include "prosody.h"
#include "schedule.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#ifndef FORMANT_TRAJECTORY_GOLDEN
#define FORMANT_TRAJECTORY_GOLDEN "tests/formant_trajectory_golden.txt"
#endif

namespace {

using formant::FormantBand;
using formant::Phoneme;
using formant::Voice;

constexpr double kWindowSeconds = 0.05;
constexpr double kHopSeconds = 0.01;
// Golden fixtures come from the exact model, so any real difference matters;
// the acoustic measurement is quantised by the harmonic grid (F0 spacing), so
// its allowance is deliberately wider. F4 also has a small table amplitude
// (0.08 for `ee`), so its peak is easily confused with F3's resonance tail;
// the last allowance reflects that documented ambiguity rather than hiding a
// large drift in the bands that carry most of the acoustic energy.
constexpr double kGoldenToleranceHz = 1.0;
constexpr std::array<double, 4> kMeasuredToleranceHz = {150.0, 150.0, 200.0,
                                                        400.0};
constexpr double kBrokenOffsetHz = 400.0;

int failures = 0;

#define CHECK(condition)                                                       \
    do {                                                                       \
        if (!(condition)) {                                                    \
            std::cerr << "FAIL " << __FILE__ << ":" << __LINE__ << ": "        \
                      << #condition << "\n";                                   \
            ++failures;                                                        \
        }                                                                      \
    } while (false)

struct TrajectoryFrame {
    int centre = 0;
    std::array<double, 4> frequencies{}; // intended F1-F4 in Hz
    double fundamental = 0.0;
    bool usable = false;
};

struct Case {
    std::string name;
    std::string source;
    std::vector<Phoneme> phonemes;
    Voice voice;
    int sampleRate = 44100;
    std::vector<TrajectoryFrame> intended;
};

struct BandReport {
    std::array<double, 4> goldenRms{};
    std::array<double, 4> measuredRms{};
    std::size_t frames = 0;
};

std::string number(double value) {
    std::ostringstream stream;
    stream << std::fixed << std::setprecision(6) << value;
    return stream.str();
}

int framesFor(double seconds, int sampleRate) {
    return std::max(2, static_cast<int>(std::llround(seconds * sampleRate)));
}

Voice analysisVoice(double pitch) {
    Voice voice;
    voice.name = "trajectory-verification";
    voice.pitch = pitch;
    voice.harmonics = 200;
    voice.vibratoRate = 0.0;
    voice.vibratoDepth = 0.0;
    voice.breathiness = 0.0;
    voice.declination = 0.0;
    voice.attack = 0.002;
    voice.release = 0.002;
    voice.gain = 1.0;
    return voice;
}

bool loadPhoneme(Case &item, const std::string &name, double duration) {
    const Phoneme *source = formant::findPhoneme(name);
    if (source == nullptr) {
        return false;
    }
    Phoneme copy = *source;
    copy.duration = duration;
    item.phonemes = {copy};
    return true;
}

std::vector<Case> corpus() {
    std::vector<Case> cases;

    Case steady;
    steady.name = "steady-vowel";
    steady.source = "phoneme ee, 100 Hz, 44.1 kHz";
    steady.voice = analysisVoice(100.0);
    if (loadPhoneme(steady, "ee", 1.0)) {
        cases.push_back(std::move(steady));
    }

    Case glide;
    glide.name = "diphthong-glide";
    glide.source = "phoneme ay, 110 Hz, 44.1 kHz";
    glide.voice = analysisVoice(110.0);
    if (loadPhoneme(glide, "ay", 1.0)) {
        cases.push_back(std::move(glide));
    }

    Case bass;
    bass.name = "bass-vowel";
    bass.source = "phoneme ee, bass preset, 48 kHz";
    bass.voice = formant::voicePreset("bass");
    bass.voice.vibratoRate = 0.0;
    bass.voice.vibratoDepth = 0.0;
    bass.voice.breathiness = 0.0;
    bass.voice.declination = 0.0;
    bass.sampleRate = 48000;
    if (loadPhoneme(bass, "ee", 1.0)) {
        cases.push_back(std::move(bass));
    }

    return cases;
}

// Mirrors AudioDraft's segment construction: a segment inherits the previous
// segment's final bands so the crossfade matches the rendered samples exactly.
std::vector<TrajectoryFrame> intendedTrajectory(const Case &item) {
    const auto spans = formant::detail::schedulePhonemes(
        item.phonemes, item.voice.rate, item.sampleRate);
    if (spans.empty()) {
        return {};
    }
    const int totalSamples = spans.back().end;
    const int window = framesFor(kWindowSeconds, item.sampleRate);
    const int hop = framesFor(kHopSeconds, item.sampleRate);
    const bool rising = formant::detail::utteranceRises(item.phonemes);

    struct Segment {
        const Phoneme *phoneme = nullptr;
        int start = 0;
        int end = 0;
        std::array<FormantBand, 4> from{};
    };
    std::vector<Segment> segments;
    std::array<FormantBand, 4> previous = formant::detail::zeroBands();
    for (std::size_t i = 0; i < item.phonemes.size(); ++i) {
        segments.push_back(
            {&item.phonemes[i], spans[i].start, spans[i].end, previous});
        previous = item.phonemes[i].hasEndBands ? item.phonemes[i].endBands
                                                : item.phonemes[i].bands;
    }

    const double nyquistGuard = 0.45 * static_cast<double>(item.sampleRate);
    std::vector<TrajectoryFrame> frames;
    for (int start = 0; start + window <= totalSamples; start += hop) {
        const int centre = start + window / 2;
        TrajectoryFrame frame;
        frame.centre = centre;
        const Segment *segment = nullptr;
        for (const Segment &candidate : segments) {
            if (centre >= candidate.start && centre < candidate.end) {
                segment = &candidate;
                break;
            }
        }
        if (segment != nullptr && segment->phoneme->voiced &&
            segment->phoneme->kind != formant::PhonemeKind::Silence) {
            const auto bands = formant::detail::bandsAt(
                *segment->phoneme, segment->from, centre, segment->start,
                segment->end, item.voice, item.sampleRate);
            bool bandsUsable = true;
            for (std::size_t band = 0; band < bands.size(); ++band) {
                const double hz =
                    bands[band].frequency * item.voice.formantScale;
                frame.frequencies[band] = hz;
                if (!(hz > 0.0) || hz >= nyquistGuard ||
                    !(bands[band].amplitude > 0.0)) {
                    bandsUsable = false;
                }
            }
            frame.fundamental = formant::detail::fundamentalAt(
                item.voice, *segment->phoneme, centre, segment->start,
                segment->end, totalSamples, item.sampleRate, rising);
            frame.usable = bandsUsable && frame.fundamental > 0.0;
        }
        frames.push_back(frame);
    }
    return frames;
}

std::vector<float> frameSamples(const std::vector<float> &samples, int start,
                                int window) {
    std::vector<float> frame;
    frame.reserve(static_cast<std::size_t>(window));
    for (int i = 0; i < window && start + i < static_cast<int>(samples.size());
         ++i) {
        frame.push_back(samples[static_cast<std::size_t>(start + i)]);
    }
    return frame;
}

struct MeasuredFrame {
    std::array<double, 4> frequencies{};
    bool usable = false;
};

std::vector<MeasuredFrame>
measuredTrajectory(const Case &item,
                   const std::vector<TrajectoryFrame> &intended,
                   const std::vector<float> &samples) {
    const int window = framesFor(kWindowSeconds, item.sampleRate);
    const int hop = framesFor(kHopSeconds, item.sampleRate);
    const auto f0 = formant::estimateF0(samples, item.sampleRate,
                                        formant::AnalyzerOptions{});
    std::vector<MeasuredFrame> result;
    result.reserve(intended.size());
    for (std::size_t i = 0; i < intended.size(); ++i) {
        MeasuredFrame frame;
        const TrajectoryFrame &target = intended[i];
        if (target.usable && i < f0.size() && f0[i].frequency > 0.0) {
            const int start = static_cast<int>(i) * hop;
            const std::vector<float> windowSamples =
                frameSamples(samples, start, window);
            const int harmonics = std::max(
                2, std::min(item.voice.harmonics,
                            static_cast<int>(item.sampleRate * 0.49 /
                                             std::max(1.0, f0[i].frequency))));
            const auto low = formant::estimateFormantPeaks(
                windowSamples, item.sampleRate, f0[i].frequency, harmonics,
                {target.frequencies[0], target.frequencies[1]});
            const auto high = formant::estimateFormantPeaks(
                windowSamples, item.sampleRate, f0[i].frequency, harmonics,
                {target.frequencies[2], target.frequencies[3]});
            frame.frequencies = {low[0].frequency, low[1].frequency,
                                 high[0].frequency, high[1].frequency};
            frame.usable = std::all_of(
                frame.frequencies.begin(), frame.frequencies.end(),
                [](double hz) { return std::isfinite(hz) && hz > 0.0; });
        }
        result.push_back(frame);
    }
    return result;
}

std::array<double, 4> rmsDiff(const std::vector<TrajectoryFrame> &a,
                              const std::vector<TrajectoryFrame> &b,
                              std::size_t &count) {
    std::array<double, 4> sums{};
    count = 0;
    const std::size_t size = std::min(a.size(), b.size());
    for (std::size_t i = 0; i < size; ++i) {
        if (!a[i].usable || !b[i].usable || a[i].centre != b[i].centre) {
            continue;
        }
        for (std::size_t band = 0; band < 4; ++band) {
            const double delta =
                a[i].frequencies[band] - b[i].frequencies[band];
            sums[band] += delta * delta;
        }
        ++count;
    }
    std::array<double, 4> result{};
    for (std::size_t band = 0; band < 4; ++band) {
        result[band] = count > 0
                           ? std::sqrt(sums[band] / static_cast<double>(count))
                           : 0.0;
    }
    return result;
}

using Golden = std::map<std::string, std::vector<TrajectoryFrame>>;

Golden readGolden(const std::string &path) {
    Golden golden;
    std::ifstream input(path);
    if (!input) {
        std::cerr << "FAIL cannot open golden trajectory fixture: " << path
                  << "\n";
        ++failures;
        return golden;
    }
    std::string line;
    while (std::getline(input, line)) {
        if (line.empty() || line[0] == '#') {
            continue;
        }
        std::istringstream fields(line);
        std::string name;
        int centre = 0;
        double f0 = 0.0;
        TrajectoryFrame frame;
        if (!(fields >> name >> centre >> f0 >> frame.frequencies[0] >>
              frame.frequencies[1] >> frame.frequencies[2] >>
              frame.frequencies[3])) {
            std::cerr << "FAIL malformed golden fixture line: " << line << "\n";
            ++failures;
            continue;
        }
        frame.centre = centre;
        frame.fundamental = f0;
        frame.usable = true;
        golden[name].push_back(frame);
    }
    return golden;
}

void writeGolden(const std::string &path, const std::vector<Case> &cases) {
    std::ofstream output(path);
    if (!output) {
        std::cerr << "FAIL cannot write golden trajectory fixture: " << path
                  << "\n";
        ++failures;
        return;
    }
    output << "# formant trajectory golden fixture (issue #104)\n"
           << "# generated by formant_verify --write-golden; review the diff\n"
           << "# columns: case centreSample intendedF0Hz F1Hz F2Hz F3Hz F4Hz\n";
    for (const Case &item : cases) {
        for (const TrajectoryFrame &frame : item.intended) {
            if (!frame.usable) {
                continue;
            }
            output << item.name << ' ' << frame.centre << ' '
                   << number(frame.fundamental) << ' '
                   << number(frame.frequencies[0]) << ' '
                   << number(frame.frequencies[1]) << ' '
                   << number(frame.frequencies[2]) << ' '
                   << number(frame.frequencies[3]) << " # " << item.source
                   << "\n";
        }
    }
    std::cerr << "wrote " << path << "\n";
}

BandReport compareCase(const Case &item,
                       const std::vector<TrajectoryFrame> &intended,
                       const std::vector<float> &samples, const Golden &golden,
                       std::ostream &report) {
    BandReport result;
    const auto found = golden.find(item.name);
    if (found == golden.end()) {
        std::cerr << "FAIL no golden trajectory for " << item.name << "\n";
        ++failures;
        return result;
    }
    result.goldenRms = rmsDiff(intended, found->second, result.frames);

    const auto measured = measuredTrajectory(item, intended, samples);
    // Reuse the same RMS helper by presenting measured frames as trajectories;
    // the frame index supplies the centre sample so alignment is checked too.
    std::vector<TrajectoryFrame> measuredAsTrajectory;
    measuredAsTrajectory.reserve(measured.size());
    for (std::size_t i = 0; i < measured.size(); ++i) {
        TrajectoryFrame frame;
        frame.centre = i < intended.size() ? intended[i].centre : 0;
        frame.frequencies = measured[i].frequencies;
        frame.usable = measured[i].usable;
        measuredAsTrajectory.push_back(frame);
    }
    std::size_t measuredFrames = 0;
    const auto measuredDiff =
        rmsDiff(measuredAsTrajectory, intended, measuredFrames);
    for (std::size_t band = 0; band < 4; ++band) {
        result.measuredRms[band] = measuredDiff[band];
    }

    report << item.name << ' ' << result.frames;
    for (std::size_t band = 0; band < 4; ++band) {
        report << ' ' << number(result.goldenRms[band]);
    }
    for (std::size_t band = 0; band < 4; ++band) {
        report << ' ' << number(result.measuredRms[band]);
    }
    report << " # " << item.source << "\n";

    if (result.frames < 5) {
        std::cerr << "FAIL too few comparable frames for " << item.name << ": "
                  << result.frames << "\n";
        ++failures;
    }
    if (measuredFrames < 5) {
        std::cerr << "FAIL too few measurable frames for " << item.name << ": "
                  << measuredFrames << "\n";
        ++failures;
    }
    for (std::size_t band = 0; band < 4; ++band) {
        if (result.goldenRms[band] > kGoldenToleranceHz) {
            std::cerr << "FAIL formant table drift for " << item.name << " F"
                      << (band + 1) << ": " << number(result.goldenRms[band])
                      << " Hz > " << kGoldenToleranceHz << " Hz\n";
            ++failures;
        }
        if (result.measuredRms[band] > kMeasuredToleranceHz[band]) {
            std::cerr << "FAIL acoustic formant drift for " << item.name << " F"
                      << (band + 1) << ": " << number(result.measuredRms[band])
                      << " Hz > " << kMeasuredToleranceHz[band] << " Hz\n";
            ++failures;
        }
    }
    return result;
}

// Simulates a formant table regression without mutating the compiled-in table:
// each corpus phoneme gets a large positive offset on F1 and F2. The golden
// comparison must notice, otherwise the harness would be vacuous.
bool brokenTableIsDetected(const std::vector<Case> &cases,
                           const Golden &golden) {
    bool detected = false;
    for (const Case &original : cases) {
        const auto found = golden.find(original.name);
        if (found == golden.end()) {
            continue;
        }
        Case broken = original;
        for (Phoneme &phoneme : broken.phonemes) {
            for (std::size_t band = 0; band < 2; ++band) {
                phoneme.bands[band].frequency += kBrokenOffsetHz;
            }
        }
        const auto intended = intendedTrajectory(broken);
        std::size_t frames = 0;
        const auto drift = rmsDiff(intended, found->second, frames);
        std::cout << "broken dry run " << broken.name
                  << " F1=" << number(drift[0]) << " Hz F2=" << number(drift[1])
                  << " Hz (" << frames << " frames)\n";
        if (frames >= 5 && drift[0] > kGoldenToleranceHz &&
            drift[1] > kGoldenToleranceHz) {
            detected = true;
        }
    }
    return detected;
}

} // namespace

int main(int argc, char **argv) {
    std::string goldenPath = FORMANT_TRAJECTORY_GOLDEN;
    std::string reportPath;
    bool writeGoldenFlag = false;
    bool selfTest = false;

    for (int i = 1; i < argc; ++i) {
        const std::string argument = argv[i];
        if (argument == "--golden" && i + 1 < argc) {
            goldenPath = argv[++i];
        } else if (argument == "--report" && i + 1 < argc) {
            reportPath = argv[++i];
        } else if (argument == "--write-golden" && i + 1 < argc) {
            goldenPath = argv[++i];
            writeGoldenFlag = true;
        } else if (argument == "--self-test-broken") {
            selfTest = true;
        } else if (argument == "--help") {
            std::cout << "usage: formant_verify_tool [--golden FILE] "
                         "[--report FILE] [--write-golden FILE] "
                         "[--self-test-broken]\n";
            return 0;
        } else {
            std::cerr << "unknown option: " << argument << "\n";
            return 2;
        }
    }

    std::vector<Case> cases = corpus();
    if (cases.empty()) {
        std::cerr << "FAIL empty trajectory corpus\n";
        return 1;
    }
    for (Case &item : cases) {
        item.intended = intendedTrajectory(item);
    }

    if (writeGoldenFlag) {
        writeGolden(goldenPath, cases);
        return failures == 0 ? 0 : 1;
    }

    const Golden golden = readGolden(goldenPath);
    if (selfTest) {
        if (!brokenTableIsDetected(cases, golden)) {
            std::cerr << "FAIL broken formant table was not detected\n";
            return 1;
        }
        std::cout << "broken formant table detected: golden comparison is "
                     "non-vacuous\n";
        return 0;
    }

    std::ostringstream report;
    report << "# case frames golden_f1 golden_f2 golden_f3 golden_f4 "
              "measured_f1 measured_f2 measured_f3 measured_f4 (Hz RMS)\n";
    for (const Case &item : cases) {
        const std::vector<float> samples =
            formant::Synthesizer(item.voice)
                .render(item.phonemes, item.sampleRate);
        compareCase(item, item.intended, samples, golden, report);
    }
    std::cout << report.str();
    if (!reportPath.empty()) {
        std::ofstream output(reportPath);
        if (!output) {
            std::cerr << "FAIL cannot write report: " << reportPath << "\n";
            ++failures;
        } else {
            output << report.str();
            std::cerr << "wrote " << reportPath << "\n";
        }
    }
    if (failures == 0) {
        std::cout << "formant trajectory verification passed: " << cases.size()
                  << " cases\n";
    }
    return failures == 0 ? 0 : 1;
}
