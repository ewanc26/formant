#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "formant/accent.h"
#include "formant/language.h"
#include "formant/voice.h"

namespace formant {

struct MrtEnsemble {
    std::string id;
    std::string focus; // "initial" or "final"
    std::array<std::string, 6> words;
};

struct MrtBatteryOptions {
    std::string outDir;
    Voice voice = voicePreset("neutral");
    Language language = Language::English;
    int sampleRate = 44100;
    // Optional accent transfer applied after phonemization, so a listening
    // battery can measure intelligibility for an accented speaker.
    std::optional<AccentProfile> accent;
};

struct MrtBatteryResult {
    std::string outDir;
    int filesWritten = 0;
};

// Canonical 50-ensemble Modified Rhyme Test word list.
std::vector<MrtEnsemble> mrtEnsembles();

// Render the full MRT battery as WAV files plus response/scoring templates.
// The output is deterministic for a given voice, language and sample rate.
MrtBatteryResult writeMrtBattery(const MrtBatteryOptions &options);

} // namespace formant
