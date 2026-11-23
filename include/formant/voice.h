#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace formant {

// A voice is a small bundle of synthesis parameters. Every value can be
// changed by the caller, which makes voices data rather than subclasses.
struct Voice {
    std::string name = "neutral";
    double pitch = 140.0;      // base fundamental frequency in Hz
    double declination = 0.12; // natural downward drift across a phrase
    double rate = 1.0;         // speaking-rate multiplier
    double formantScale = 1.0; // scales the vocal-tract length
    double vibratoRate = 5.0;  // Hz
    double vibratoDepth = 0.0; // cents
    double breathiness = 0.0;  // amount of sine-noise mixed into voiced sounds
    int harmonics = 40;        // sine partials used for the glottal source
    double attack = 0.012;     // seconds
    double release = 0.02;     // seconds
    double gain = 0.85;        // output level before normalisation

    // Fade the last 4% of the usable band before the Nyquist break. Keep this
    // enabled for normal rendering; it is a data-driven switch so callers can
    // A/B the anti-click fade and tests can prove the regression it prevents.
    bool taperCeiling = true;
};

// Returns a copy of a built-in voice. Throws std::invalid_argument for an
// unknown name.
Voice voicePreset(std::string_view name);

// Stable, ordered list of the built-in voice names.
std::vector<std::string> voicePresetNames();

} // namespace formant
