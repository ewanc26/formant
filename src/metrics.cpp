#include "formant/metrics.h"

#include "prosody.h"
#include "schedule.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <stdexcept>
#include <vector>

namespace formant {
namespace {

using detail::kPi;

constexpr double kLogEpsilon = 1e-12;
constexpr double kRmsEpsilon = 1e-4;
constexpr double kMcdScale = 6.14185; // 10 * sqrt(2) / ln(10)

void validateOptions(int sampleRate, const AnalyzerOptions &options) {
    if (sampleRate < 8000) {
        throw std::invalid_argument("sample rate must be at least 8000 Hz");
    }
    if (!(options.minF0 > 0.0) || !(options.maxF0 > options.minF0)) {
        throw std::invalid_argument("F0 range must be positive and increasing");
    }
    if (!(options.windowSeconds > 0.0) || !(options.hopSeconds > 0.0)) {
        throw std::invalid_argument("analysis window and hop must be positive");
    }
    if (options.melBands < 2) {
        throw std::invalid_argument("at least two mel bands are required");
    }
    if (options.cepstralCoefficients < 1 ||
        options.cepstralCoefficients >= options.melBands) {
        throw std::invalid_argument(
            "cepstral order must be between 1 and melBands - 1");
    }
    if (options.maxHarmonic < 2) {
        throw std::invalid_argument("at least two harmonics are required");
    }
}

int frameSamples(double seconds, int sampleRate) {
    return std::max(2, static_cast<int>(std::llround(seconds * sampleRate)));
}

std::vector<int> frameStarts(int totalSamples, int sampleRate,
                             const AnalyzerOptions &options) {
    const int window = frameSamples(options.windowSeconds, sampleRate);
    const int hop = frameSamples(options.hopSeconds, sampleRate);
    std::vector<int> starts;
    if (totalSamples < window) {
        return starts;
    }
    for (int start = 0; start + window <= totalSamples; start += hop) {
        starts.push_back(start);
    }
    return starts;
}

double mel(double frequency) {
    return 2595.0 * std::log10(1.0 + frequency / 700.0);
}

double inverseMel(double value) {
    return 700.0 * (std::pow(10.0, value / 2595.0) - 1.0);
}

std::vector<double> melFrequencies(double high,
                                   const AnalyzerOptions &options) {
    const double low = 80.0;
    if (high <= low) {
        throw std::invalid_argument("analysis band is too narrow for mel grid");
    }
    const double lowMel = mel(low);
    const double highMel = mel(high);
    std::vector<double> frequencies;
    frequencies.reserve(static_cast<std::size_t>(options.melBands));
    for (int band = 0; band < options.melBands; ++band) {
        const double fraction = static_cast<double>(band) /
                                static_cast<double>(options.melBands - 1);
        frequencies.push_back(
            inverseMel(lowMel + fraction * (highMel - lowMel)));
    }
    return frequencies;
}

std::vector<double> harmonicFrequencies(double fundamental, double high,
                                        int maxHarmonic) {
    std::vector<double> frequencies;
    for (int harmonic = 1; harmonic <= maxHarmonic; ++harmonic) {
        const double frequency = fundamental * static_cast<double>(harmonic);
        if (frequency > high) {
            break;
        }
        frequencies.push_back(frequency);
    }
    return frequencies;
}

// Linear interpolation in mel space. Both tracks use the same harmonic grid
// ends, so this only compensates for the small F0 estimation error.
std::vector<double>
interpolateMel(const std::vector<double> &sourceFrequencies,
               const std::vector<double> &sourcePower,
               const std::vector<double> &targetFrequencies) {
    std::vector<double> result;
    result.reserve(targetFrequencies.size());
    if (sourceFrequencies.empty() ||
        sourcePower.size() != sourceFrequencies.size()) {
        return result;
    }
    for (double frequency : targetFrequencies) {
        if (frequency <= sourceFrequencies.front()) {
            result.push_back(sourcePower.front());
            continue;
        }
        if (frequency >= sourceFrequencies.back()) {
            result.push_back(sourcePower.back());
            continue;
        }
        std::size_t upper = 1;
        while (upper < sourceFrequencies.size() &&
               sourceFrequencies[upper] < frequency) {
            ++upper;
        }
        const double lowMelValue = mel(sourceFrequencies[upper - 1]);
        const double highMelValue = mel(sourceFrequencies[upper]);
        const double fraction = (mel(frequency) - lowMelValue) /
                                std::max(1e-12, highMelValue - lowMelValue);
        result.push_back(sourcePower[upper - 1] * (1.0 - fraction) +
                         sourcePower[upper] * fraction);
    }
    return result;
}

std::vector<double> hannWindow(int length) {
    std::vector<double> window(static_cast<std::size_t>(length));
    for (int i = 0; i < length; ++i) {
        const double phase = 2.0 * kPi * static_cast<double>(i) /
                             static_cast<double>(length - 1);
        window[static_cast<std::size_t>(i)] = 0.5 - 0.5 * std::cos(phase);
    }
    return window;
}

std::vector<double> windowFrame(const std::vector<float> &samples, int start,
                                const std::vector<double> &window) {
    std::vector<double> frame(window.size());
    for (std::size_t i = 0; i < window.size(); ++i) {
        frame[i] =
            static_cast<double>(samples[static_cast<std::size_t>(start) + i]) *
            window[i];
    }
    return frame;
}

double goertzelPower(const std::vector<double> &frame, double frequency,
                     int sampleRate) {
    const double coefficient =
        2.0 * std::cos(2.0 * kPi * frequency / static_cast<double>(sampleRate));
    double previous = 0.0;
    double previous2 = 0.0;
    for (double value : frame) {
        const double current = value + coefficient * previous - previous2;
        previous2 = previous;
        previous = current;
    }
    const double power = previous * previous + previous2 * previous2 -
                         coefficient * previous * previous2;
    return std::max(0.0, power);
}

std::vector<double> melCepstra(const std::vector<double> &logPower,
                               int coefficients) {
    const int bands = static_cast<int>(logPower.size());
    std::vector<double> result(static_cast<std::size_t>(coefficients + 1));
    for (int n = 0; n <= coefficients; ++n) {
        double sum = 0.0;
        for (int band = 0; band < bands; ++band) {
            const double angle = kPi * static_cast<double>(n) *
                                 (static_cast<double>(band) + 0.5) /
                                 static_cast<double>(bands);
            sum += logPower[static_cast<std::size_t>(band)] * std::cos(angle);
        }
        const double scale = n == 0
                                 ? std::sqrt(1.0 / static_cast<double>(bands))
                                 : std::sqrt(2.0 / static_cast<double>(bands));
        result[static_cast<std::size_t>(n)] = sum * scale;
    }
    return result;
}

struct PlannedSegment {
    const Phoneme *phoneme = nullptr;
    int start = 0;
    int end = 0;
    std::array<FormantBand, 4> from{};
    std::array<SpectralZero, 2> fromZeros{};
};

std::vector<PlannedSegment> planSegments(const std::vector<Phoneme> &phonemes,
                                         const Voice &voice, int sampleRate) {
    const std::vector<detail::ScheduledSpan> spans =
        detail::schedulePhonemes(phonemes, voice.rate, sampleRate);
    std::vector<PlannedSegment> segments;
    segments.reserve(phonemes.size());
    std::array<FormantBand, 4> previous = detail::zeroBands();
    std::array<SpectralZero, 2> previousZeros{};

    for (std::size_t i = 0; i < phonemes.size(); ++i) {
        segments.push_back({&phonemes[i], spans[i].start, spans[i].end,
                            previous, previousZeros});
        previous =
            phonemes[i].hasEndBands ? phonemes[i].endBands : phonemes[i].bands;
        previousZeros = phonemes[i].zeros;
    }
    return segments;
}

const PlannedSegment *segmentAt(const std::vector<PlannedSegment> &segments,
                                int sample) {
    for (const PlannedSegment &segment : segments) {
        if (sample >= segment.start && sample < segment.end) {
            return &segment;
        }
    }
    return nullptr;
}

double formantBandEnergy(const std::vector<float> &samples, int sampleRate,
                         double frequency) {
    if (samples.empty()) {
        return 0.0;
    }
    const double coefficient =
        2.0 * std::cos(2.0 * kPi * frequency / static_cast<double>(sampleRate));
    const std::vector<double> window =
        hannWindow(static_cast<int>(samples.size()));
    double previous = 0.0;
    double previous2 = 0.0;
    double windowSum = 0.0;
    for (std::size_t i = 0; i < samples.size(); ++i) {
        const double value = static_cast<double>(samples[i]) * window[i];
        windowSum += window[i];
        const double current = value + coefficient * previous - previous2;
        previous2 = previous;
        previous = current;
    }
    if (windowSum <= 0.0) {
        return 0.0;
    }
    const double power = previous * previous + previous2 * previous2 -
                         coefficient * previous * previous2;
    return std::sqrt(std::max(0.0, power)) / (0.5 * windowSum);
}

struct Peak {
    double frequency = 0.0;
    double energy = 0.0;
};

Peak strongestHarmonic(const std::vector<float> &samples, int sampleRate,
                       double fundamental, int harmonics, double low,
                       double high) {
    if (!(fundamental > 0.0)) {
        return {};
    }
    const int ceiling =
        std::min(harmonics,
                 static_cast<int>(std::floor(sampleRate * 0.49 / fundamental)));
    Peak best;
    for (int harmonic = 1; harmonic <= ceiling; ++harmonic) {
        const double frequency = fundamental * static_cast<double>(harmonic);
        if (frequency < low || frequency > high) {
            continue;
        }
        const double energy = formantBandEnergy(samples, sampleRate, frequency);
        if (energy > best.energy) {
            best = {frequency, energy};
        }
    }
    return best;
}

} // namespace

F0Track estimateF0(const std::vector<float> &samples, int sampleRate,
                   const AnalyzerOptions &options) {
    validateOptions(sampleRate, options);
    F0Track track;
    if (samples.empty()) {
        return track;
    }

    const int window = frameSamples(options.windowSeconds, sampleRate);
    const int hop = frameSamples(options.hopSeconds, sampleRate);
    const int minLag =
        std::max(2, static_cast<int>(std::floor(sampleRate / options.maxF0)));
    const int maxLag = std::min(
        window - 2, static_cast<int>(std::ceil(sampleRate / options.minF0)));
    const std::vector<double> windowValues = hannWindow(window);

    for (int start = 0; start + window <= static_cast<int>(samples.size());
         start += hop) {
        F0Frame frame;
        frame.startSample = start;
        const std::vector<double> values =
            windowFrame(samples, start, windowValues);

        double energy = 0.0;
        for (double value : values) {
            energy += value * value;
        }
        if (maxLag <= minLag ||
            std::sqrt(energy / values.size()) < kRmsEpsilon) {
            track.push_back(frame);
            continue;
        }

        std::vector<double> scores(static_cast<std::size_t>(maxLag + 2), 0.0);
        double bestScore = 0.0;
        int bestLag = minLag;
        for (int lag = minLag; lag <= maxLag; ++lag) {
            double cross = 0.0;
            double left = 0.0;
            double right = 0.0;
            for (int i = 0; i + lag < window; ++i) {
                const double a = values[static_cast<std::size_t>(i)];
                const double b = values[static_cast<std::size_t>(i + lag)];
                cross += a * b;
                left += a * a;
                right += b * b;
            }
            const double score = left > 0.0 && right > 0.0
                                     ? cross / std::sqrt(left * right)
                                     : 0.0;
            scores[static_cast<std::size_t>(lag)] = score;
            if (score > bestScore) {
                bestScore = score;
                bestLag = lag;
            }
        }

        const double threshold = bestScore * 0.94;
        int chosen = bestLag;
        for (int lag = minLag + 1; lag < maxLag; ++lag) {
            const double score = scores[static_cast<std::size_t>(lag)];
            if (score >= threshold &&
                score >= scores[static_cast<std::size_t>(lag - 1)] &&
                score >= scores[static_cast<std::size_t>(lag + 1)]) {
                chosen = lag;
                break;
            }
        }

        const double left = scores[static_cast<std::size_t>(chosen - 1)];
        const double centre = scores[static_cast<std::size_t>(chosen)];
        const double right = scores[static_cast<std::size_t>(chosen + 1)];
        const double denominator = left - 2.0 * centre + right;
        double shift = 0.0;
        if (std::abs(denominator) > 1e-12) {
            shift = 0.5 * (left - right) / denominator;
            shift = std::max(-0.5, std::min(0.5, shift));
        }
        const double lag = static_cast<double>(chosen) + shift;
        const double frequency = static_cast<double>(sampleRate) / lag;
        frame.confidence = std::max(0.0, std::min(1.0, bestScore));
        if (bestScore >= 0.55 && frequency >= options.minF0 &&
            frequency <= options.maxF0) {
            frame.frequency = frequency;
        }
        track.push_back(frame);
    }
    return track;
}

F0Track expectedF0(const std::vector<Phoneme> &phonemes, const Voice &voice,
                   int sampleRate, const AnalyzerOptions &options) {
    validateOptions(sampleRate, options);
    F0Track track;
    if (phonemes.empty()) {
        return track;
    }
    const std::vector<PlannedSegment> segments =
        planSegments(phonemes, voice, sampleRate);
    const int totalSamples = segments.back().end;
    const int window = frameSamples(options.windowSeconds, sampleRate);
    const bool rising = detail::utteranceRises(phonemes);

    for (int start : frameStarts(totalSamples, sampleRate, options)) {
        F0Frame frame;
        frame.startSample = start;
        const int centre = start + window / 2;
        const PlannedSegment *segment = segmentAt(segments, centre);
        if (segment != nullptr &&
            segment->phoneme->kind != PhonemeKind::Silence &&
            segment->phoneme->voiced) {
            const double frequency = detail::fundamentalAt(
                voice, *segment->phoneme, centre, segment->start, segment->end,
                totalSamples, sampleRate, rising);
            if (frequency >= options.minF0 && frequency <= options.maxF0) {
                frame.frequency = frequency;
                frame.confidence = 1.0;
            }
        }
        track.push_back(frame);
    }
    return track;
}

F0Metrics compareF0(const F0Track &estimated, const F0Track &expected) {
    F0Metrics metrics;
    std::vector<double> errors;
    std::vector<double> estimatedValues;
    std::vector<double> expectedValues;
    const std::size_t count = std::min(estimated.size(), expected.size());
    for (std::size_t i = 0; i < count; ++i) {
        if (estimated[i].frequency <= 0.0 || expected[i].frequency <= 0.0) {
            continue;
        }
        const double actual = estimated[i].frequency;
        const double target = expected[i].frequency;
        errors.push_back(actual - target);
        estimatedValues.push_back(actual);
        expectedValues.push_back(target);
        metrics.rmseCents += std::pow(1200.0 * std::log2(actual / target), 2.0);
    }
    metrics.frames = errors.size();
    if (metrics.frames == 0) {
        return metrics;
    }

    double squared = 0.0;
    for (double error : errors) {
        squared += error * error;
    }
    metrics.rmseHz = std::sqrt(squared / static_cast<double>(metrics.frames));
    metrics.rmseCents =
        std::sqrt(metrics.rmseCents / static_cast<double>(metrics.frames));

    if (metrics.frames < 2) {
        return metrics;
    }
    double estimatedMean = 0.0;
    double expectedMean = 0.0;
    for (std::size_t i = 0; i < metrics.frames; ++i) {
        estimatedMean += estimatedValues[i];
        expectedMean += expectedValues[i];
    }
    estimatedMean /= static_cast<double>(metrics.frames);
    expectedMean /= static_cast<double>(metrics.frames);
    double covariance = 0.0;
    double estimatedVariance = 0.0;
    double expectedVariance = 0.0;
    for (std::size_t i = 0; i < metrics.frames; ++i) {
        const double a = estimatedValues[i] - estimatedMean;
        const double b = expectedValues[i] - expectedMean;
        covariance += a * b;
        estimatedVariance += a * a;
        expectedVariance += b * b;
    }
    const double denominator = std::sqrt(estimatedVariance * expectedVariance);
    if (denominator > 0.0) {
        metrics.correlation = covariance / denominator;
    } else {
        // A perfectly steady contour has zero variance, so Pearson's r is
        // undefined. Report 1.0 only when the two constant tracks agree within
        // one hertz; otherwise the flat tracks are not a match.
        metrics.correlation = metrics.rmseHz <= 1.0 ? 1.0 : 0.0;
    }
    return metrics;
}

EnvelopeTrack estimateEnvelope(const std::vector<float> &samples,
                               int sampleRate, const AnalyzerOptions &options) {
    validateOptions(sampleRate, options);
    EnvelopeTrack track;
    if (samples.empty()) {
        return track;
    }
    const F0Track f0Track = estimateF0(samples, sampleRate, options);
    const std::vector<double> window =
        hannWindow(frameSamples(options.windowSeconds, sampleRate));
    double windowSum = 0.0;
    for (double value : window) {
        windowSum += value;
    }
    const double windowScale = std::max(1e-12, 0.5 * windowSum);

    std::size_t index = 0;
    for (int start :
         frameStarts(static_cast<int>(samples.size()), sampleRate, options)) {
        EnvelopeFrame frame;
        frame.startSample = start;
        const double fundamental =
            index < f0Track.size() ? f0Track[index].frequency : 0.0;
        ++index;
        if (fundamental > 0.0) {
            const double high = std::min(
                {8000.0, sampleRate * 0.45,
                 fundamental * static_cast<double>(options.maxHarmonic)});
            const std::vector<double> frequencies =
                melFrequencies(high, options);
            const std::vector<double> harmonics =
                harmonicFrequencies(fundamental, high, options.maxHarmonic);
            const std::vector<double> values =
                windowFrame(samples, start, window);

            std::vector<double> sourcePower;
            sourcePower.reserve(harmonics.size());
            for (double frequency : harmonics) {
                const double power =
                    goertzelPower(values, frequency, sampleRate) /
                    (windowScale * windowScale);
                sourcePower.push_back(std::log(std::max(kLogEpsilon, power)));
            }
            frame.logPower =
                interpolateMel(harmonics, sourcePower, frequencies);
            frame.cepstra =
                melCepstra(frame.logPower, options.cepstralCoefficients);
        }
        track.push_back(std::move(frame));
    }
    return track;
}

EnvelopeTrack expectedEnvelope(const std::vector<Phoneme> &phonemes,
                               const Voice &voice, int sampleRate,
                               const AnalyzerOptions &options) {
    validateOptions(sampleRate, options);
    EnvelopeTrack track;
    if (phonemes.empty()) {
        return track;
    }
    const std::vector<PlannedSegment> segments =
        planSegments(phonemes, voice, sampleRate);
    const int totalSamples = segments.back().end;
    const int window = frameSamples(options.windowSeconds, sampleRate);
    const F0Track f0Track = expectedF0(phonemes, voice, sampleRate, options);

    std::size_t index = 0;
    for (int start : frameStarts(totalSamples, sampleRate, options)) {
        EnvelopeFrame frame;
        frame.startSample = start;
        const int centre = start + window / 2;
        const double fundamental =
            index < f0Track.size() ? f0Track[index].frequency : 0.0;
        const PlannedSegment *segment = segmentAt(segments, centre);
        ++index;
        if (fundamental > 0.0 && segment != nullptr &&
            segment->phoneme->kind != PhonemeKind::Silence &&
            segment->phoneme->voiced) {
            const double high = std::min(
                {8000.0, sampleRate * 0.45,
                 fundamental * static_cast<double>(options.maxHarmonic)});
            const std::vector<double> frequencies =
                melFrequencies(high, options);
            const std::vector<double> harmonics =
                harmonicFrequencies(fundamental, high, options.maxHarmonic);
            const std::array<FormantBand, 4> bands = detail::bandsAt(
                *segment->phoneme, segment->from, centre, segment->start,
                segment->end, voice, sampleRate);
            const std::array<SpectralZero, 2> zeros =
                detail::zerosAt(*segment->phoneme, segment->fromZeros, centre,
                                segment->start, voice, sampleRate);
            const detail::FormantEnvelope envelope =
                detail::makeFormantEnvelope(bands, zeros, voice.formantScale,
                                            voice.nasality);

            std::vector<double> sourcePower;
            sourcePower.reserve(harmonics.size());
            const bool capped = voice.harmonics >= detail::kMaxHarmonics;
            int harmonicNumber = 0;
            for (double frequency : harmonics) {
                ++harmonicNumber;
                double amplitude = 0.0;
                if (harmonicNumber <= voice.harmonics &&
                    frequency < sampleRate * detail::kHarmonicCeiling) {
                    double gain =
                        voice.taperCeiling
                            ? detail::ceilingGain(frequency, sampleRate)
                            : 1.0;
                    if (capped) {
                        gain *= voice.taperCeiling
                                    ? detail::capGain(harmonicNumber)
                                    : 1.0;
                    }
                    amplitude = detail::formantResponse(frequency, envelope) /
                                static_cast<double>(harmonicNumber) * gain *
                                segment->phoneme->amplitude;
                }
                sourcePower.push_back(
                    std::log(std::max(kLogEpsilon, amplitude * amplitude)));
            }
            frame.logPower =
                interpolateMel(harmonics, sourcePower, frequencies);
            frame.cepstra =
                melCepstra(frame.logPower, options.cepstralCoefficients);
        }
        track.push_back(std::move(frame));
    }
    return track;
}

EnvelopeMetrics compareEnvelope(const EnvelopeTrack &estimated,
                                const EnvelopeTrack &expected) {
    EnvelopeMetrics metrics;
    const std::size_t count = std::min(estimated.size(), expected.size());
    for (std::size_t i = 0; i < count; ++i) {
        const EnvelopeFrame &actual = estimated[i];
        const EnvelopeFrame &target = expected[i];
        if (actual.startSample != target.startSample ||
            actual.logPower.empty() || target.logPower.empty() ||
            actual.logPower.size() != target.logPower.size() ||
            actual.cepstra.size() != target.cepstra.size()) {
            continue;
        }

        double actualMean = 0.0;
        double targetMean = 0.0;
        for (std::size_t band = 0; band < actual.logPower.size(); ++band) {
            actualMean += actual.logPower[band];
            targetMean += target.logPower[band];
        }
        actualMean /= static_cast<double>(actual.logPower.size());
        targetMean /= static_cast<double>(target.logPower.size());
        double squared = 0.0;
        for (std::size_t band = 0; band < actual.logPower.size(); ++band) {
            const double difference = (actual.logPower[band] - actualMean) -
                                      (target.logPower[band] - targetMean);
            squared += difference * difference;
        }
        metrics.logDistance +=
            std::sqrt(squared / static_cast<double>(actual.logPower.size()));

        double cepstralSquared = 0.0;
        for (std::size_t coefficient = 1; coefficient < actual.cepstra.size();
             ++coefficient) {
            const double difference =
                actual.cepstra[coefficient] - target.cepstra[coefficient];
            cepstralSquared += difference * difference;
        }
        metrics.mcdDb += kMcdScale * std::sqrt(cepstralSquared);
        ++metrics.frames;
    }
    if (metrics.frames > 0) {
        const double divisor = static_cast<double>(metrics.frames);
        metrics.logDistance /= divisor;
        metrics.mcdDb /= divisor;
    }
    return metrics;
}

std::array<FormantPeak, 2>
estimateFormantPeaks(const std::vector<float> &samples, int sampleRate,
                     double fundamental, int harmonics,
                     const std::array<double, 2> &expectedFrequencies) {
    std::array<FormantPeak, 2> peaks{};
    for (std::size_t band = 0; band < peaks.size(); ++band) {
        const double expected = expectedFrequencies[band];
        const Peak best =
            strongestHarmonic(samples, sampleRate, fundamental, harmonics,
                              expected - 300.0, expected + 300.0);
        const double fundamentalFloor = std::max(80.0, fundamental);
        const double guard = std::max(
            strongestHarmonic(samples, sampleRate, fundamental, harmonics,
                              std::max(fundamentalFloor, expected - 600.0),
                              expected - 400.0)
                .energy,
            strongestHarmonic(samples, sampleRate, fundamental, harmonics,
                              expected + 400.0, expected + 600.0)
                .energy);
        peaks[band] = {best.frequency, best.energy, guard};
    }
    return peaks;
}

FormantMetrics
compareFormants(const std::array<FormantPeak, 2> &peaks,
                const std::array<double, 2> &expectedFrequencies) {
    FormantMetrics metrics;
    double contrast = std::numeric_limits<double>::infinity();
    for (std::size_t band = 0; band < peaks.size(); ++band) {
        metrics.errorHz[band] =
            std::abs(peaks[band].frequency - expectedFrequencies[band]);
        metrics.maxErrorHz =
            std::max(metrics.maxErrorHz, metrics.errorHz[band]);
        contrast =
            std::min(contrast, peaks[band].energy /
                                   std::max(1e-12, peaks[band].guardEnergy));
    }
    metrics.minContrast = std::isfinite(contrast)
                              ? contrast
                              : std::numeric_limits<double>::infinity();
    return metrics;
}

namespace {

bool rhythmEligible(const Phoneme &phoneme) {
    if (phoneme.kind == PhonemeKind::Silence || !(phoneme.duration > 0.0) ||
        !std::isfinite(phoneme.duration)) {
        return false;
    }
    if (phoneme.source.rfind("breath:", 0) == 0 ||
        phoneme.source.rfind("nvv:", 0) == 0) {
        return false;
    }
    // Singing and prosody-direct own their timing and F0 outright.
    return !phoneme.noteF0 && !phoneme.f0StartHz && !phoneme.f0EndHz;
}

bool vocalic(const Phoneme &phoneme) {
    return phoneme.kind == PhonemeKind::Voiced && phoneme.voiced;
}

double normalizedPvi(const std::vector<double> &durations) {
    if (durations.size() < 2) {
        return 0.0;
    }
    double sum = 0.0;
    for (std::size_t i = 1; i < durations.size(); ++i) {
        sum += 200.0 * std::abs(durations[i - 1] - durations[i]) /
               (durations[i - 1] + durations[i]);
    }
    return sum / static_cast<double>(durations.size() - 1);
}

double rawPviMs(const std::vector<double> &durations) {
    if (durations.size() < 2) {
        return 0.0;
    }
    double sum = 0.0;
    for (std::size_t i = 1; i < durations.size(); ++i) {
        sum += std::abs(durations[i - 1] - durations[i]) * 1000.0;
    }
    return sum / static_cast<double>(durations.size() - 1);
}

} // namespace

RhythmMetrics measureRhythm(const std::vector<Phoneme> &phonemes) {
    std::vector<double> vocalicIntervals;
    std::vector<double> intervocalicIntervals;
    double vocalicSeconds = 0.0;
    double intervocalicSeconds = 0.0;
    bool inVocalic = false;
    bool inIntervocalic = false;
    bool sawVocalic = false;

    const auto flushVocalic = [&]() {
        if (inVocalic) {
            vocalicIntervals.push_back(vocalicSeconds);
            vocalicSeconds = 0.0;
            inVocalic = false;
        }
    };
    const auto flushIntervocalic = [&]() {
        if (inIntervocalic) {
            intervocalicIntervals.push_back(intervocalicSeconds);
            intervocalicSeconds = 0.0;
            inIntervocalic = false;
        }
    };

    for (const Phoneme &phoneme : phonemes) {
        if (!rhythmEligible(phoneme)) {
            flushVocalic();
            flushIntervocalic();
            sawVocalic = false;
            continue;
        }
        if (vocalic(phoneme)) {
            flushIntervocalic();
            if (!inVocalic) {
                inVocalic = true;
            }
            vocalicSeconds += phoneme.duration;
            sawVocalic = true;
        } else {
            flushVocalic();
            if (sawVocalic) {
                if (!inIntervocalic) {
                    inIntervocalic = true;
                }
                intervocalicSeconds += phoneme.duration;
            }
        }
    }
    // A trailing consonant span is not intervocalic; drop it.
    flushVocalic();

    RhythmMetrics metrics;
    metrics.vocalicNPvi = normalizedPvi(vocalicIntervals);
    metrics.intervocalicRPvi = rawPviMs(intervocalicIntervals);
    metrics.vocalicIntervals = vocalicIntervals.size();
    metrics.intervocalicIntervals = intervocalicIntervals.size();
    return metrics;
}

} // namespace formant
