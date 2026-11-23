#include "formant/transpose.h"

#include <cmath>
#include <stdexcept>

namespace formant {
namespace {

void requireFinite(double value, const char *name) {
    if (!std::isfinite(value)) {
        throw std::invalid_argument(std::string(name) + " must be finite");
    }
}

void requirePositive(double value, const char *name) {
    requireFinite(value, name);
    if (value <= 0.0) {
        throw std::invalid_argument(std::string(name) + " must be positive");
    }
}

} // namespace

double centsToRatio(double cents) {
    requireFinite(cents, "pitch interval");
    return std::pow(2.0, cents / 1200.0);
}

Voice shiftPitch(const Voice &voice, double cents) {
    const double ratio = centsToRatio(cents);
    Voice result = voice;
    if (result.pitch > 0.0) {
        result.pitch *= ratio;
        if (!std::isfinite(result.pitch)) {
            throw std::invalid_argument(
                "pitch interval produces a non-finite fundamental");
        }
    }
    return result;
}

Voice scaleFormants(const Voice &voice, double ratio) {
    requirePositive(ratio, "formant ratio");
    Voice result = voice;
    result.formantScale *= ratio;
    if (!std::isfinite(result.formantScale)) {
        throw std::invalid_argument(
            "formant ratio produces a non-finite formant scale");
    }
    return result;
}

Voice withVocalTractLength(const Voice &voice, double tractScale) {
    requirePositive(tractScale, "vocal-tract scale");
    return scaleFormants(voice, 1.0 / tractScale);
}

Voice transposeVoice(const Voice &voice, double cents, double formantRatio) {
    return scaleFormants(shiftPitch(voice, cents), formantRatio);
}

std::vector<Voice> detuneVoices(const Voice &voice,
                                const std::vector<double> &cents) {
    std::vector<Voice> voices;
    voices.reserve(cents.size());
    for (double offset : cents) {
        voices.push_back(shiftPitch(voice, offset));
    }
    return voices;
}

} // namespace formant
