#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "formant/phoneme.h"
#include "formant/voice.h"

namespace formant {

// One timed unit in an utterance. All sample boundaries come from the same
// scheduler used by Synthesizer::render(), so for a given voice, rate and
// sample rate these values describe the audio that was actually produced.
// Times are audio times (sample / sampleRate); the speaking rate is already
// reflected in the sample count.
struct TimedUnit {
    std::string label;
    PhonemeKind kind = PhonemeKind::Silence;
    int startSample = 0;
    int endSample = 0;
    double startTime = 0.0;
    double endTime = 0.0;

    bool operator==(const TimedUnit &) const = default;
};

// A coarse viseme event for animation rigs. `shape` is one of the documented
// names in docs/timing.md and `openness` runs 0 (closed) to 1 (wide open).
// Like every other timing value it is derived from the phoneme stream before
// rendering, not estimated from the waveform afterwards.
struct VisemeFrame {
    std::string shape;
    double openness = 0.0;
    std::string phoneme;
    int startSample = 0;
    int endSample = 0;
    double startTime = 0.0;
    double endTime = 0.0;

    bool operator==(const VisemeFrame &) const = default;
};

// A sample-accurate timing plan for one utterance.
struct TimingPlan {
    int sampleRate = 44100;
    int totalSamples = 0;
    double duration = 0.0; // totalSamples / sampleRate, in seconds
    std::string voice;
    std::vector<TimedUnit> words;
    std::vector<TimedUnit> phonemes;
};

// Build a plan from phonemes using the exact render schedule. Text front ends
// emit a "|" word-gap phoneme between words; those gaps delimit word units.
// Pauses are folded into the preceding word's span. If `wordLabels` has one
// entry per word they are used as the word labels; otherwise labels are
// derived deterministically from each word's phoneme names.
TimingPlan planTiming(const std::vector<Phoneme> &phonemes, const Voice &voice,
                      int sampleRate,
                      const std::vector<std::string> &wordLabels = {});

// Split text into word labels using the same boundaries as the text front
// ends (Unicode letters, digits and internal apostrophes stay in a word).
std::vector<std::string> splitWordsForTiming(std::string_view text);

// Derive viseme events from a plan. The mapping is a documented data table in
// timing.cpp; unknowns fall back by PhonemeKind so new phonemes still render
// a stable viseme instead of throwing.
std::vector<VisemeFrame> planVisemes(const TimingPlan &plan);

// Plain-text writers. All use "\n" line endings and a fixed numeric format,
// so the same plan yields byte-identical files on every platform.
std::string timingJson(const TimingPlan &plan,
                       const std::vector<VisemeFrame> &visemes);
std::string timingVisemesJson(const TimingPlan &plan,
                              const std::vector<VisemeFrame> &visemes);
std::string timingLrc(const TimingPlan &plan);
std::string timingSrt(const TimingPlan &plan);
std::string timingVtt(const TimingPlan &plan);

} // namespace formant
