#pragma once

#include <array>
#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

#include "formant/phoneme.h"

namespace formant {

// The learned style layer uses four bounded context features and two bounded
// corrections. Keeping the dimensions fixed makes saved states portable and
// keeps inference a pure lookup with no model file format negotiation.
inline constexpr std::size_t kNeuralInputSize = 4;
inline constexpr std::size_t kNeuralOutputSize = 2;

struct NeuralSample {
    std::array<double, kNeuralInputSize> input{};
    std::array<double, kNeuralOutputSize> target{};
};

struct NeuralConfig {
    double winnerRate = 0.10;
    double neighborRate = 0.03;
    double errorDecay = 0.50;
    double insertionError = 0.02;
    std::size_t growthInterval = 4;
    std::size_t maxUnits = 64;
    std::size_t maxNeighbors = 4;
    std::size_t maxEdgeAge = 24;
    std::size_t predictionNeighbors = 3;
};

struct NeuralUnit {
    std::array<double, kNeuralInputSize> center{};
    std::array<double, kNeuralOutputSize> output{1.0, 1.0};
    double error = 0.0;
    std::size_t presentations = 0;
    std::size_t age = 0;

    bool operator==(const NeuralUnit &) const = default;
};

struct NeuralEdge {
    std::size_t a = 0;
    std::size_t b = 0;
    std::size_t age = 0;

    bool operator==(const NeuralEdge &) const = default;
};

struct NeuralSummary {
    std::size_t units = 0;
    std::size_t edges = 0;
    std::size_t observations = 0;
    std::size_t growths = 0;
    double meanError = 0.0;
};

// A supervised Growing Neural Gas variant. Units are inserted only when the
// current topology carries more error than the configured threshold, and the
// unit count is bounded. All decisions use sample order and stable indices, so
// the same samples always produce the same network and the same audio.
class GrowingNeuralNetwork {
  public:
    explicit GrowingNeuralNetwork(NeuralConfig config = {});

    const NeuralConfig &config() const noexcept { return config_; }
    std::size_t observationCount() const noexcept { return observations_; }
    std::size_t growthCount() const noexcept { return growths_; }
    const std::vector<NeuralUnit> &units() const noexcept { return units_; }
    const std::vector<NeuralEdge> &edges() const noexcept { return edges_; }

    // Returns true when this observation inserted a unit.
    bool observe(const NeuralSample &sample);
    void train(const std::vector<NeuralSample> &samples);

    // Weighted mean of the nearest units. With no units the neutral output
    // {1.0, 1.0} is returned, so an empty model is a no-op.
    std::array<double, kNeuralOutputSize>
    predict(const std::array<double, kNeuralInputSize> &input) const;

    NeuralSummary summary() const;

    // Versioned, line-oriented text with 17 significant digits for doubles.
    // The same state always has the same bytes on every platform.
    std::string serialize() const;
    static GrowingNeuralNetwork parse(std::string_view data);

  private:
    NeuralConfig config_;
    std::vector<NeuralUnit> units_;
    std::vector<NeuralEdge> edges_;
    std::size_t observations_ = 0;
    std::size_t growths_ = 0;
    std::size_t lastGrowthObservation_ = 0;

    std::size_t nearest(const std::array<double, kNeuralInputSize> &input,
                        std::size_t skip) const;
    bool hasEdge(std::size_t a, std::size_t b) const;
    void addEdge(std::size_t a, std::size_t b);
    void removeEdge(std::size_t a, std::size_t b);
    void trimEdges(std::size_t unit);
    void decayErrors();
};

// Parses `sample p w b c pitch duration` lines. Inputs must be in [0, 1] and
// targets in [0.5, 2.0]. Malformed lines throw std::invalid_argument with the
// source line number.
std::vector<NeuralSample> parseNeuralSamples(std::string_view data);

// Deterministic context extraction shared by training authors and the CLI:
//   0. position within the utterance, from 0 to 1;
//   1. position within the current word, from 0 to 1;
//   2. following boundary strength (0 none, 0.5 comma, 1 sentence end);
//   3. accent/stress evidence from amplitude, relative F0 and duration.
std::array<double, kNeuralInputSize>
neuralFeatures(const std::vector<Phoneme> &phonemes, std::size_t index);

// Multiplies pitch and duration by the learned corrections. Authored NVV and
// breath segments, silences and absolute F0 targets are left untouched. The
// strength is clamped to [0, 1], so a trained model composes predictably with
// exact intonation and prosody overrides that run later.
void applyNeuralProsody(std::vector<Phoneme> &phonemes,
                        const GrowingNeuralNetwork &network,
                        double strength = 1.0);

} // namespace formant
