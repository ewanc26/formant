#pragma once

// Dependency-free objective analyzers for verification and research.
//
// Every function here is a pure transform of its inputs. No audio is read from
// disk, no randomness is used and no clock is consulted, so the same samples
// produce the same numbers on every platform. See docs/metrics.md for the
// exact definitions.

#include "formant/phoneme.h"
#include "formant/voice.h"

#include <array>
#include <cstddef>
#include <vector>

namespace formant {

struct AnalyzerOptions {
    double minF0 = 70.0;  // Hz
    double maxF0 = 500.0; // Hz
    double windowSeconds = 0.05;
    double hopSeconds = 0.01;
    int melBands = 32;
    int cepstralCoefficients = 13;
    int maxHarmonic = 64; // shared analysis ceiling for harmonic envelopes
};

struct F0Frame {
    int startSample = 0;
    double frequency = 0.0;  // Hz; zero means unvoiced or below confidence
    double confidence = 0.0; // normalised autocorrelation, 0..1
};

using F0Track = std::vector<F0Frame>;

// Autocorrelation F0 tracker. The best lag in the search range is found first,
// then the earliest local maximum within 94% of it is selected to avoid
// octave-down errors on harmonic sounds. A parabolic peak estimate gives
// sub-sample lag resolution.
F0Track estimateF0(const std::vector<float> &samples, int sampleRate,
                   const AnalyzerOptions &options = {});

// Intended F0 at the same frame centres, computed from the exact render
// schedule and the shared prosody model. Silent and unvoiced frames are zero.
F0Track expectedF0(const std::vector<Phoneme> &phonemes, const Voice &voice,
                   int sampleRate, const AnalyzerOptions &options = {});

struct F0Metrics {
    double rmseHz = 0.0;
    double rmseCents = 0.0;
    double correlation = 0.0; // Pearson, over jointly voiced frames
    std::size_t frames = 0;
};

F0Metrics compareF0(const F0Track &estimated, const F0Track &expected);

struct EnvelopeFrame {
    int startSample = 0;
    std::vector<double> logPower; // natural log, one entry per mel band
    std::vector<double> cepstra;  // DCT-II mel-cepstral coefficients
};

using EnvelopeTrack = std::vector<EnvelopeFrame>;

// Hann-windowed Goertzel power on an evenly spaced mel-frequency grid,
// followed by an orthonormal DCT. Overall gain is removed before comparison,
// so global normalisation of a render does not affect the metrics.
EnvelopeTrack estimateEnvelope(const std::vector<float> &samples,
                               int sampleRate,
                               const AnalyzerOptions &options = {});

// Intended resonance envelope at the same frame centres. It uses the same
// formant interpolation and Nyquist-derived frequency grid as the render.
EnvelopeTrack expectedEnvelope(const std::vector<Phoneme> &phonemes,
                               const Voice &voice, int sampleRate,
                               const AnalyzerOptions &options = {});

struct EnvelopeMetrics {
    double logDistance = 0.0; // RMS of gain-normalised log-power difference
    double mcdDb = 0.0;       // mean mel-cepstral distortion in dB
    std::size_t frames = 0;
};

EnvelopeMetrics compareEnvelope(const EnvelopeTrack &estimated,
                                const EnvelopeTrack &expected);

struct FormantPeak {
    double frequency = 0.0;
    double energy = 0.0;
    double guardEnergy = 0.0; // strongest off-formant harmonic in guard bands
};

// Finds the strongest harmonic near each expected formant and the strongest
// harmonic in the guard bands 400–600 Hz away on either side.
std::array<FormantPeak, 2>
estimateFormantPeaks(const std::vector<float> &samples, int sampleRate,
                     double fundamental, int harmonics,
                     const std::array<double, 2> &expectedFrequencies);

struct FormantMetrics {
    std::array<double, 2> errorHz{};
    double maxErrorHz = 0.0;
    double minContrast = 0.0; // peak / guard ratio; higher is better
};

FormantMetrics
compareFormants(const std::array<FormantPeak, 2> &peaks,
                const std::array<double, 2> &expectedFrequencies);

// #144: rhythm metrics derived from the exact phoneme-duration schedule that
// drives rendering. nPVI is dimensionless (x100); rPVI is in milliseconds.
// Silence, breath/NVV events, absolute-F0 and note-F0 segments are excluded.
struct RhythmMetrics {
    double vocalicNPvi = 0.0;
    double intervocalicRPvi = 0.0;
    std::size_t vocalicIntervals = 0;
    std::size_t intervocalicIntervals = 0;
};

RhythmMetrics measureRhythm(const std::vector<Phoneme> &phonemes);

} // namespace formant
