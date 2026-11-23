#pragma once

#include <vector>

#include "formant/phoneme.h"

namespace formant {
namespace detail {

// The sample span occupied by one phoneme in a render. Both Synthesizer and
// the timing track use this scheduler, so reported word/phoneme boundaries
// are mechanically identical to the audio that was rendered.
struct ScheduledSpan {
    int start = 0;
    int end = 0;
};

// Applies the documented sample-count contract: the boundary after phoneme i
// is llround(cumulativeDuration * sampleRate / rate), clamped to zero length
// per segment. Throws std::invalid_argument for an invalid sample rate, a
// non-positive or non-finite rate, or a duration/rate outside int range.
std::vector<ScheduledSpan>
schedulePhonemes(const std::vector<Phoneme> &phonemes, double rate,
                 int sampleRate);

} // namespace detail
} // namespace formant
