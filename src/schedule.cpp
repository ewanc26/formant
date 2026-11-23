#include "schedule.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace formant {
namespace detail {

std::vector<ScheduledSpan>
schedulePhonemes(const std::vector<Phoneme> &phonemes, double rate,
                 int sampleRate) {
    if (sampleRate < 8000) {
        throw std::invalid_argument("sample rate must be at least 8000 Hz");
    }
    if (!std::isfinite(rate) || rate <= 0.0) {
        throw std::invalid_argument("rate must be positive and finite");
    }
    if (phonemes.empty()) {
        return {};
    }

    constexpr double kMaxInt =
        static_cast<double>(std::numeric_limits<int>::max());

    std::vector<ScheduledSpan> spans;
    spans.reserve(phonemes.size());
    double cumulativeDuration = 0.0;
    int cursor = 0;
    for (const Phoneme &phoneme : phonemes) {
        cumulativeDuration += phoneme.duration;
        const double wanted =
            cumulativeDuration * static_cast<double>(sampleRate) / rate;
        if (!std::isfinite(wanted) || wanted < -kMaxInt || wanted > kMaxInt) {
            throw std::invalid_argument("phoneme duration / rate out of range");
        }
        const int boundary = static_cast<int>(std::llround(wanted));
        // A zero-length segment is valid (fast rates or adjacent short
        // phonemes sharing a rounded boundary); the cumulative boundary stays
        // exact, which is the documented render contract.
        const int length = std::max(0, boundary - cursor);
        if (cursor > std::numeric_limits<int>::max() - length) {
            throw std::invalid_argument("rendered audio is too long");
        }
        spans.push_back({cursor, cursor + length});
        cursor += length;
    }
    return spans;
}

} // namespace detail
} // namespace formant
