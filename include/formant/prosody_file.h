#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "formant/phoneme.h"

namespace formant {

// Selects one segment of an utterance. Exactly one field is set: `segment` is
// a zero-based phoneme index, `timeSeconds` is a time in the score timeline
// (sum of Phoneme::duration values, before Voice::rate) and selects the segment
// containing that instant.
struct ProsodySelector {
    std::optional<std::size_t> segment;
    std::optional<double> timeSeconds;
};

// One directive from a prosody override file. Every target is optional: fields
// left unset keep the value supplied by the language profile, SSML or IPA front
// end. Absolute F0 targets (Hz) and relative ones (cents, semitones, percent)
// are mutually exclusive within a directive.
struct ProsodyDirective {
    ProsodySelector selector;
    std::optional<double> f0Hz;
    std::optional<double> f0EndHz;
    std::optional<double> f0Ratio;
    std::optional<double> f0EndRatio;
    std::optional<double> durationScale;
    std::optional<double> durationSeconds;
    std::optional<double> amplitudeScale;
    std::size_t line = 0; // 1-based source line, kept for instructive errors
};

// A parsed prosody override document. It is a pure value with no file handles
// or global state.
struct ProsodyFile {
    std::vector<ProsodyDirective> directives;
    bool empty() const { return directives.empty(); }
};

// Parse the line-oriented prosody format. Throws std::invalid_argument with
// "prosody line N: ..." for malformed input. Blank lines and lines beginning
// with '#' are ignored.
ProsodyFile parseProsodyFile(std::string_view text);

// Read and parse a file from disk. Throws std::runtime_error when the file
// cannot be opened or read, and std::invalid_argument for malformed content.
ProsodyFile loadProsodyFile(const std::string &path);

// Layer a parsed file over a phoneme sequence in the parameter domain.
//
// Duration directives are resolved against the incoming timeline and applied
// first. F0 and amplitude directives are then resolved against the resulting
// timeline, so a time selector names the segment that is actually at that
// instant in the final utterance. All time selectors in one phase refer to the
// same snapshot, making the result independent of directive order within that
// phase. Later directives override earlier ones for the same field.
//
// Unselected segments are untouched, so unspecified values keep their defaults
// byte-for-byte. Throws std::invalid_argument if a selector is out of range.
void applyProsodyFile(std::vector<Phoneme> &phonemes, const ProsodyFile &file);

} // namespace formant
