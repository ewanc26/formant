#include "formant/neural.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <iomanip>
#include <limits>
#include <locale>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace formant {
namespace {

constexpr std::string_view kStateMagic = "formant-neural";
constexpr int kStateVersion = 1;

NeuralConfig sanitize(NeuralConfig config) {
    const auto clampDouble = [](double value, double low, double high) {
        return std::isfinite(value) ? std::min(high, std::max(low, value))
                                    : low;
    };
    const auto clampSize = [](std::size_t value, std::size_t low,
                              std::size_t high) {
        return std::min(high, std::max(low, value));
    };
    config.winnerRate = clampDouble(config.winnerRate, 0.0, 1.0);
    config.neighborRate = clampDouble(config.neighborRate, 0.0, 1.0);
    config.errorDecay = clampDouble(config.errorDecay, 0.0, 1.0);
    config.insertionError = clampDouble(config.insertionError, 0.001, 1.0);
    config.growthInterval = clampSize(config.growthInterval, 1, 1000000);
    config.maxUnits = clampSize(config.maxUnits, 2, 4096);
    config.maxNeighbors =
        clampSize(config.maxNeighbors, 1, config.maxUnits - 1);
    config.maxEdgeAge = clampSize(config.maxEdgeAge, 1, 1000000);
    config.predictionNeighbors =
        clampSize(config.predictionNeighbors, 1, config.maxUnits);
    return config;
}

double squaredDistance(const std::array<double, kNeuralInputSize> &a,
                       const std::array<double, kNeuralInputSize> &b) {
    double sum = 0.0;
    for (std::size_t i = 0; i < kNeuralInputSize; ++i) {
        const double difference = a[i] - b[i];
        sum += difference * difference;
    }
    return sum;
}

double outputError(const std::array<double, kNeuralOutputSize> &target,
                   const std::array<double, kNeuralOutputSize> &output) {
    double sum = 0.0;
    for (std::size_t i = 0; i < kNeuralOutputSize; ++i) {
        const double difference = target[i] - output[i];
        sum += difference * difference;
    }
    return sum;
}

bool finiteInput(const std::array<double, kNeuralInputSize> &input) {
    for (const double value : input) {
        if (!std::isfinite(value) || value < 0.0 || value > 1.0) {
            return false;
        }
    }
    return true;
}

bool finiteTarget(const std::array<double, kNeuralOutputSize> &target) {
    for (const double value : target) {
        if (!std::isfinite(value) || value < 0.5 || value > 2.0) {
            return false;
        }
    }
    return true;
}

std::vector<std::string> fields(std::string_view line) {
    std::vector<std::string> result;
    std::istringstream stream{std::string(line)};
    stream.imbue(std::locale::classic());
    std::string token;
    while (stream >> token) {
        result.push_back(token);
    }
    return result;
}

[[noreturn]] void fail(std::size_t line, std::string_view message) {
    throw std::invalid_argument("neural line " + std::to_string(line) + ": " +
                                std::string(message));
}

std::size_t parseIndex(const std::string &token, std::size_t line) {
    std::size_t value = 0;
    const char *begin = token.data();
    const char *end = begin + token.size();
    const auto parsed = std::from_chars(begin, end, value);
    if (token.empty() || parsed.ec != std::errc{} || parsed.ptr != end) {
        fail(line, "expected a non-negative integer, got '" + token + "'");
    }
    return value;
}

double parseDouble(const std::string &token, std::size_t line) {
    double value = 0.0;
    std::istringstream stream{token};
    stream.imbue(std::locale::classic());
    stream >> value;
    if (!stream || !stream.eof() || !std::isfinite(value)) {
        fail(line, "expected a finite number, got '" + token + "'");
    }
    return value;
}

void requireCount(const std::vector<std::string> &parts, std::size_t expected,
                  std::size_t line) {
    if (parts.size() != expected) {
        fail(line, "expected " + std::to_string(expected) + " fields, got " +
                       std::to_string(parts.size()));
    }
}

std::string formatDouble(double value) {
    std::ostringstream stream;
    stream.imbue(std::locale::classic());
    stream << std::setprecision(17) << value;
    return stream.str();
}

bool isGap(const Phoneme &phoneme) { return phoneme.name == "|"; }

bool strongBoundary(const Phoneme &phoneme) {
    return phoneme.name == "." || phoneme.name == "!" || phoneme.name == "?" ||
           phoneme.name == ";" || phoneme.name == ":";
}

bool weakBoundary(const Phoneme &phoneme) { return phoneme.name == ","; }

double bounded(double value, double low, double high) {
    return std::isfinite(value) ? std::min(high, std::max(low, value)) : 0.5;
}

// Accent leaves three parameter-domain traces: more amplitude, a raised F0
// contour and a longer segment. Mapping each to 0..1 around neutral values
// keeps the feature bounded for caller-built phonemes as well as IPA input.
double stressValue(const Phoneme &phoneme) {
    const double amplitude = bounded(phoneme.amplitude, 0.5, 2.0);
    const double pitch = bounded(phoneme.pitchStart, 0.5, 2.0);
    const double duration = bounded(phoneme.duration, 0.0, 1.0);
    const double amplitudeEvidence =
        std::min(1.0, std::max(0.0, 0.5 + 0.5 * (amplitude - 1.0)));
    const double pitchEvidence =
        std::min(1.0, std::max(0.0, 0.5 + 0.5 * (pitch - 1.0)));
    const double durationEvidence =
        std::min(1.0, std::max(0.0, 0.5 + 2.0 * (duration - 0.1)));
    return (amplitudeEvidence + pitchEvidence + durationEvidence) / 3.0;
}

} // namespace

GrowingNeuralNetwork::GrowingNeuralNetwork(NeuralConfig config)
    : config_(sanitize(config)) {}

std::size_t
GrowingNeuralNetwork::nearest(const std::array<double, kNeuralInputSize> &input,
                              std::size_t skip) const {
    std::size_t best = std::numeric_limits<std::size_t>::max();
    double bestDistance = std::numeric_limits<double>::infinity();
    for (std::size_t i = 0; i < units_.size(); ++i) {
        if (i == skip) {
            continue;
        }
        const double distance = squaredDistance(input, units_[i].center);
        if (distance < bestDistance) {
            bestDistance = distance;
            best = i;
        }
    }
    return best;
}

bool GrowingNeuralNetwork::hasEdge(std::size_t a, std::size_t b) const {
    if (a == b) {
        return true;
    }
    const std::size_t low = std::min(a, b);
    const std::size_t high = std::max(a, b);
    return std::any_of(edges_.begin(), edges_.end(),
                       [&](const NeuralEdge &edge) {
                           return edge.a == low && edge.b == high;
                       });
}

void GrowingNeuralNetwork::addEdge(std::size_t a, std::size_t b) {
    if (a == b || hasEdge(a, b)) {
        return;
    }
    edges_.push_back(NeuralEdge{std::min(a, b), std::max(a, b), 0});
    trimEdges(a);
    trimEdges(b);
}

void GrowingNeuralNetwork::removeEdge(std::size_t a, std::size_t b) {
    const std::size_t low = std::min(a, b);
    const std::size_t high = std::max(a, b);
    edges_.erase(std::remove_if(edges_.begin(), edges_.end(),
                                [&](const NeuralEdge &edge) {
                                    return edge.a == low && edge.b == high;
                                }),
                 edges_.end());
}

void GrowingNeuralNetwork::trimEdges(std::size_t unit) {
    std::vector<std::size_t> incident;
    for (std::size_t i = 0; i < edges_.size(); ++i) {
        if (edges_[i].a == unit || edges_[i].b == unit) {
            incident.push_back(i);
        }
    }
    while (incident.size() > config_.maxNeighbors) {
        // Remove the oldest edge first; ties resolve to the latest index so
        // the surviving topology never depends on container iteration order.
        std::size_t victim = incident.back();
        for (const std::size_t index : incident) {
            if (edges_[index].age > edges_[victim].age) {
                victim = index;
            }
        }
        edges_.erase(edges_.begin() + static_cast<std::ptrdiff_t>(victim));
        incident.clear();
        for (std::size_t i = 0; i < edges_.size(); ++i) {
            if (edges_[i].a == unit || edges_[i].b == unit) {
                incident.push_back(i);
            }
        }
    }
}

void GrowingNeuralNetwork::decayErrors() {
    for (NeuralUnit &unit : units_) {
        unit.error *= config_.errorDecay;
    }
}

bool GrowingNeuralNetwork::observe(const NeuralSample &sample) {
    if (!finiteInput(sample.input) || !finiteTarget(sample.target)) {
        throw std::invalid_argument(
            "neural sample input must be in [0,1] and target in [0.5,2]");
    }
    ++observations_;

    if (units_.empty()) {
        NeuralUnit unit;
        unit.center = sample.input;
        unit.output = sample.target;
        unit.presentations = 1;
        units_.push_back(unit);
        return false;
    }

    const std::size_t winner =
        nearest(sample.input, std::numeric_limits<std::size_t>::max());
    const std::size_t runnerUp = nearest(sample.input, winner);
    if (runnerUp != std::numeric_limits<std::size_t>::max()) {
        addEdge(winner, runnerUp);
    }

    NeuralUnit &best = units_[winner];
    best.error += squaredDistance(sample.input, best.center) +
                  outputError(sample.target, best.output);
    for (std::size_t i = 0; i < kNeuralInputSize; ++i) {
        best.center[i] +=
            config_.winnerRate * (sample.input[i] - best.center[i]);
    }
    for (std::size_t i = 0; i < kNeuralOutputSize; ++i) {
        best.output[i] +=
            config_.winnerRate * (sample.target[i] - best.output[i]);
    }
    ++best.presentations;

    std::vector<std::size_t> neighbors;
    for (const NeuralEdge &edge : edges_) {
        if (edge.a == winner) {
            neighbors.push_back(edge.b);
        } else if (edge.b == winner) {
            neighbors.push_back(edge.a);
        }
    }
    for (const std::size_t index : neighbors) {
        NeuralUnit &neighbor = units_[index];
        for (std::size_t i = 0; i < kNeuralInputSize; ++i) {
            neighbor.center[i] +=
                config_.neighborRate * (sample.input[i] - neighbor.center[i]);
        }
        for (std::size_t i = 0; i < kNeuralOutputSize; ++i) {
            neighbor.output[i] +=
                config_.neighborRate * (sample.target[i] - neighbor.output[i]);
        }
        ++neighbor.presentations;
    }

    for (NeuralEdge &edge : edges_) {
        ++edge.age;
    }
    if (runnerUp != std::numeric_limits<std::size_t>::max()) {
        const std::size_t low = std::min(winner, runnerUp);
        const std::size_t high = std::max(winner, runnerUp);
        for (NeuralEdge &edge : edges_) {
            if (edge.a == low && edge.b == high) {
                edge.age = 0;
            }
        }
    }
    edges_.erase(std::remove_if(edges_.begin(), edges_.end(),
                                [&](const NeuralEdge &edge) {
                                    return edge.age > config_.maxEdgeAge;
                                }),
                 edges_.end());

    if (observations_ - lastGrowthObservation_ < config_.growthInterval ||
        units_.size() >= config_.maxUnits) {
        decayErrors();
        return false;
    }

    std::size_t worst = 0;
    for (std::size_t i = 1; i < units_.size(); ++i) {
        if (units_[i].error > units_[worst].error) {
            worst = i;
        }
    }
    if (units_[worst].error <= config_.insertionError) {
        decayErrors();
        return false;
    }

    NeuralUnit inserted;
    inserted.center = sample.input;
    inserted.output = sample.target;
    inserted.presentations = 1;
    inserted.error = units_[worst].error * 0.5;
    const std::size_t added = units_.size();
    units_.push_back(inserted);
    units_[worst].error *= 0.5;
    addEdge(worst, added);
    if (runnerUp != std::numeric_limits<std::size_t>::max() &&
        runnerUp != worst) {
        addEdge(runnerUp, added);
    }
    ++growths_;
    lastGrowthObservation_ = observations_;
    decayErrors();
    return true;
}

void GrowingNeuralNetwork::train(const std::vector<NeuralSample> &samples) {
    for (const NeuralSample &sample : samples) {
        (void)observe(sample);
    }
}

std::array<double, kNeuralOutputSize> GrowingNeuralNetwork::predict(
    const std::array<double, kNeuralInputSize> &input) const {
    std::array<double, kNeuralOutputSize> result{};
    if (units_.empty()) {
        return {1.0, 1.0};
    }
    if (!finiteInput(input)) {
        return {1.0, 1.0};
    }

    std::vector<std::pair<double, std::size_t>> ranked;
    ranked.reserve(units_.size());
    for (std::size_t i = 0; i < units_.size(); ++i) {
        ranked.emplace_back(squaredDistance(input, units_[i].center), i);
    }
    std::stable_sort(ranked.begin(), ranked.end(),
                     [](const auto &left, const auto &right) {
                         return left.first < right.first;
                     });

    const std::size_t count =
        std::min(config_.predictionNeighbors, ranked.size());
    double totalWeight = 0.0;
    for (std::size_t i = 0; i < count; ++i) {
        const double weight = 1.0 / (ranked[i].first + 1e-6);
        totalWeight += weight;
        for (std::size_t channel = 0; channel < kNeuralOutputSize; ++channel) {
            result[channel] +=
                weight * units_[ranked[i].second].output[channel];
        }
    }
    for (std::size_t channel = 0; channel < kNeuralOutputSize; ++channel) {
        result[channel] /= totalWeight;
        result[channel] = std::min(2.0, std::max(0.5, result[channel]));
    }
    return result;
}

NeuralSummary GrowingNeuralNetwork::summary() const {
    NeuralSummary result;
    result.units = units_.size();
    result.edges = edges_.size();
    result.observations = observations_;
    result.growths = growths_;
    for (const NeuralUnit &unit : units_) {
        result.meanError += unit.error;
    }
    if (!units_.empty()) {
        result.meanError /= static_cast<double>(units_.size());
    }
    return result;
}

std::string GrowingNeuralNetwork::serialize() const {
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out << kStateMagic << ' ' << kStateVersion << '\n';
    out << "config winner-rate " << formatDouble(config_.winnerRate) << '\n';
    out << "config neighbor-rate " << formatDouble(config_.neighborRate)
        << '\n';
    out << "config error-decay " << formatDouble(config_.errorDecay) << '\n';
    out << "config insertion-error " << formatDouble(config_.insertionError)
        << '\n';
    out << "config growth-interval " << config_.growthInterval << '\n';
    out << "config max-units " << config_.maxUnits << '\n';
    out << "config max-neighbors " << config_.maxNeighbors << '\n';
    out << "config max-edge-age " << config_.maxEdgeAge << '\n';
    out << "config prediction-neighbors " << config_.predictionNeighbors
        << '\n';
    out << "observations " << observations_ << '\n';
    out << "growths " << growths_ << '\n';
    out << "last-growth-observation " << lastGrowthObservation_ << '\n';
    for (std::size_t i = 0; i < units_.size(); ++i) {
        const NeuralUnit &unit = units_[i];
        out << "unit " << i;
        for (const double value : unit.center) {
            out << ' ' << formatDouble(value);
        }
        for (const double value : unit.output) {
            out << ' ' << formatDouble(value);
        }
        out << ' ' << formatDouble(unit.error) << ' ' << unit.presentations
            << ' ' << unit.age << '\n';
    }
    for (const NeuralEdge &edge : edges_) {
        out << "edge " << edge.a << ' ' << edge.b << ' ' << edge.age << '\n';
    }
    return out.str();
}

GrowingNeuralNetwork GrowingNeuralNetwork::parse(std::string_view data) {
    NeuralConfig config;
    bool haveConfig[kNeuralInputSize + 5] = {};
    std::size_t observations = 0;
    std::size_t growths = 0;
    std::size_t lastGrowth = 0;
    bool haveObservations = false;
    bool haveGrowths = false;
    bool haveLastGrowth = false;
    std::vector<NeuralUnit> units;
    std::vector<NeuralEdge> edges;

    std::size_t lineNumber = 0;
    std::size_t offset = 0;
    while (offset < data.size()) {
        const std::size_t newline = data.find('\n', offset);
        const std::size_t length = newline == std::string_view::npos
                                       ? data.size() - offset
                                       : newline - offset;
        const std::string_view raw = data.substr(offset, length);
        offset += length + (newline == std::string_view::npos ? 0 : 1);
        ++lineNumber;
        const std::size_t comment = raw.find('#');
        const std::string_view line =
            comment == std::string_view::npos ? raw : raw.substr(0, comment);
        const std::vector<std::string> parts = fields(line);
        if (parts.empty()) {
            continue;
        }

        if (lineNumber == 1) {
            if (parts.size() != 2 || parts[0] != kStateMagic ||
                parseIndex(parts[1], lineNumber) !=
                    static_cast<std::size_t>(kStateVersion)) {
                fail(lineNumber, "unsupported neural state header");
            }
            continue;
        }

        if (parts[0] == "config") {
            requireCount(parts, 3, lineNumber);
            double value = 0.0;
            std::size_t integer = 0;
            std::size_t slot = 0;
            if (parts[1] == "winner-rate" || parts[1] == "neighbor-rate" ||
                parts[1] == "error-decay" || parts[1] == "insertion-error") {
                value = parseDouble(parts[2], lineNumber);
            } else {
                integer = parseIndex(parts[2], lineNumber);
            }
            if (parts[1] == "winner-rate") {
                config.winnerRate = value;
                slot = 0;
            } else if (parts[1] == "neighbor-rate") {
                config.neighborRate = value;
                slot = 1;
            } else if (parts[1] == "error-decay") {
                config.errorDecay = value;
                slot = 2;
            } else if (parts[1] == "insertion-error") {
                config.insertionError = value;
                slot = 3;
            } else if (parts[1] == "growth-interval") {
                config.growthInterval = integer;
                slot = 4;
            } else if (parts[1] == "max-units") {
                config.maxUnits = integer;
                slot = 5;
            } else if (parts[1] == "max-neighbors") {
                config.maxNeighbors = integer;
                slot = 6;
            } else if (parts[1] == "max-edge-age") {
                config.maxEdgeAge = integer;
                slot = 7;
            } else if (parts[1] == "prediction-neighbors") {
                config.predictionNeighbors = integer;
                slot = 8;
            } else {
                fail(lineNumber, "unknown config key '" + parts[1] + "'");
            }
            if (haveConfig[slot]) {
                fail(lineNumber, "duplicate config key '" + parts[1] + "'");
            }
            haveConfig[slot] = true;
            continue;
        }
        if (parts[0] == "observations") {
            requireCount(parts, 2, lineNumber);
            if (haveObservations) {
                fail(lineNumber, "duplicate observations line");
            }
            observations = parseIndex(parts[1], lineNumber);
            haveObservations = true;
            continue;
        }
        if (parts[0] == "growths") {
            requireCount(parts, 2, lineNumber);
            if (haveGrowths) {
                fail(lineNumber, "duplicate growths line");
            }
            growths = parseIndex(parts[1], lineNumber);
            haveGrowths = true;
            continue;
        }
        if (parts[0] == "last-growth-observation") {
            requireCount(parts, 2, lineNumber);
            if (haveLastGrowth) {
                fail(lineNumber, "duplicate last-growth-observation line");
            }
            lastGrowth = parseIndex(parts[1], lineNumber);
            haveLastGrowth = true;
            continue;
        }
        if (parts[0] == "unit") {
            requireCount(parts, 11, lineNumber);
            const std::size_t index = parseIndex(parts[1], lineNumber);
            if (index != units.size()) {
                fail(lineNumber, "unit indices must be sequential from 0");
            }
            NeuralUnit unit;
            for (std::size_t i = 0; i < kNeuralInputSize; ++i) {
                unit.center[i] = parseDouble(parts[2 + i], lineNumber);
            }
            for (std::size_t i = 0; i < kNeuralOutputSize; ++i) {
                unit.output[i] = parseDouble(parts[6 + i], lineNumber);
            }
            unit.error = parseDouble(parts[8], lineNumber);
            unit.presentations = parseIndex(parts[9], lineNumber);
            unit.age = parseIndex(parts[10], lineNumber);
            if (!finiteInput(unit.center) || !finiteTarget(unit.output) ||
                unit.error < 0.0 || unit.error > 1e6) {
                fail(lineNumber, "unit values are out of range");
            }
            units.push_back(unit);
            continue;
        }
        if (parts[0] == "edge") {
            requireCount(parts, 4, lineNumber);
            const std::size_t a = parseIndex(parts[1], lineNumber);
            const std::size_t b = parseIndex(parts[2], lineNumber);
            const std::size_t age = parseIndex(parts[3], lineNumber);
            if (a >= units.size() || b >= units.size() || a == b ||
                age > config.maxEdgeAge) {
                fail(lineNumber, "invalid edge endpoints or age");
            }
            const std::size_t low = std::min(a, b);
            const std::size_t high = std::max(a, b);
            if (std::any_of(edges.begin(), edges.end(),
                            [&](const NeuralEdge &edge) {
                                return edge.a == low && edge.b == high;
                            })) {
                fail(lineNumber, "duplicate edge");
            }
            edges.push_back(NeuralEdge{low, high, age});
            continue;
        }
        fail(lineNumber, "unknown directive '" + parts[0] + "'");
    }

    if (lineNumber == 0 || !haveObservations || !haveGrowths ||
        !haveLastGrowth) {
        fail(std::max<std::size_t>(1, lineNumber),
             "missing neural state header or counters");
    }
    for (bool present : haveConfig) {
        if (!present) {
            fail(std::max<std::size_t>(1, lineNumber),
                 "incomplete neural config");
        }
    }
    if (!units.empty() && units.size() > config.maxUnits) {
        fail(std::max<std::size_t>(1, lineNumber), "too many units");
    }
    if (units.empty() && (!edges.empty() || growths != 0)) {
        fail(std::max<std::size_t>(1, lineNumber),
             "empty state cannot contain edges or growths");
    }
    if ((!units.empty() && growths + 1 > units.size()) ||
        growths > observations || lastGrowth > observations) {
        fail(std::max<std::size_t>(1, lineNumber), "inconsistent counters");
    }
    if (units.size() == 1 && observations == 0) {
        fail(std::max<std::size_t>(1, lineNumber),
             "a unit requires at least one observation");
    }

    GrowingNeuralNetwork network(config);
    network.units_ = units;
    network.edges_ = edges;
    network.observations_ = observations;
    network.growths_ = growths;
    network.lastGrowthObservation_ = lastGrowth;
    return network;
}

std::vector<NeuralSample> parseNeuralSamples(std::string_view data) {
    std::vector<NeuralSample> samples;
    std::size_t lineNumber = 0;
    std::size_t offset = 0;
    while (offset < data.size()) {
        const std::size_t newline = data.find('\n', offset);
        const std::size_t length = newline == std::string_view::npos
                                       ? data.size() - offset
                                       : newline - offset;
        const std::string_view raw = data.substr(offset, length);
        offset += length + (newline == std::string_view::npos ? 0 : 1);
        ++lineNumber;
        const std::size_t comment = raw.find('#');
        const std::string_view line =
            comment == std::string_view::npos ? raw : raw.substr(0, comment);
        const std::vector<std::string> parts = fields(line);
        if (parts.empty()) {
            continue;
        }
        if (parts[0] != "sample") {
            fail(lineNumber, "unknown directive '" + parts[0] + "'");
        }
        requireCount(parts, 7, lineNumber);
        NeuralSample sample;
        for (std::size_t i = 0; i < kNeuralInputSize; ++i) {
            sample.input[i] = parseDouble(parts[1 + i], lineNumber);
        }
        for (std::size_t i = 0; i < kNeuralOutputSize; ++i) {
            sample.target[i] = parseDouble(parts[5 + i], lineNumber);
        }
        if (!finiteInput(sample.input) || !finiteTarget(sample.target)) {
            fail(lineNumber,
                 "sample input must be in [0,1] and target in [0.5,2]");
        }
        samples.push_back(sample);
    }
    return samples;
}

std::array<double, kNeuralInputSize>
neuralFeatures(const std::vector<Phoneme> &phonemes, std::size_t index) {
    std::array<double, kNeuralInputSize> result{};
    if (phonemes.empty() || index >= phonemes.size()) {
        return result;
    }

    const double denominator =
        phonemes.size() > 1 ? static_cast<double>(phonemes.size() - 1) : 1.0;
    result[0] = static_cast<double>(index) / denominator;

    std::size_t begin = 0;
    for (std::size_t i = index; i > 0; --i) {
        if (isGap(phonemes[i - 1])) {
            begin = i;
            break;
        }
    }
    std::size_t end = phonemes.size();
    for (std::size_t i = index + 1; i < phonemes.size(); ++i) {
        if (isGap(phonemes[i])) {
            end = i;
            break;
        }
    }
    const double width = static_cast<double>(end - begin);
    if (width > 0.0) {
        result[1] = std::min(1.0, static_cast<double>(index - begin) /
                                      std::max(1.0, width - 1.0));
    }

    for (std::size_t i = index + 1; i < phonemes.size(); ++i) {
        if (isGap(phonemes[i])) {
            continue;
        }
        if (strongBoundary(phonemes[i])) {
            result[2] = 1.0;
        } else if (weakBoundary(phonemes[i])) {
            result[2] = 0.5;
        }
        break;
    }
    if (result[2] == 0.0) {
        bool hasFollowingSpeech = false;
        for (std::size_t i = index + 1; i < phonemes.size(); ++i) {
            if (!isGap(phonemes[i]) &&
                phonemes[i].kind != PhonemeKind::Silence) {
                hasFollowingSpeech = true;
                break;
            }
        }
        if (!hasFollowingSpeech) {
            result[2] = 1.0;
        }
    }
    result[3] = stressValue(phonemes[index]);
    return result;
}

void applyNeuralProsody(std::vector<Phoneme> &phonemes,
                        const GrowingNeuralNetwork &network, double strength) {
    const double amount = std::min(1.0, std::max(0.0, strength));
    if (amount == 0.0 || network.units().empty()) {
        return;
    }
    for (std::size_t i = 0; i < phonemes.size(); ++i) {
        Phoneme &phoneme = phonemes[i];
        if (!phoneme.source.empty() || phoneme.kind == PhonemeKind::Silence ||
            phoneme.f0StartHz) {
            continue;
        }
        const auto output = network.predict(neuralFeatures(phonemes, i));
        const double pitchScale = 1.0 + amount * (output[0] - 1.0);
        const double durationScale = 1.0 + amount * (output[1] - 1.0);
        phoneme.pitchStart *= pitchScale;
        phoneme.pitchEnd *= pitchScale;
        phoneme.duration *= durationScale;
    }
}

} // namespace formant
