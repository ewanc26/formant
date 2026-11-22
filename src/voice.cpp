#include "formant/voice.h"

#include <stdexcept>

namespace formant {

Voice voicePreset(std::string_view name) {
    Voice voice;

    if (name == "neutral") {
        voice.name = "neutral";
        voice.pitch = 140.0;
        voice.formantScale = 1.0;
        voice.breathiness = 0.04;
        voice.vibratoDepth = 8.0;
    } else if (name == "alto") {
        voice.name = "alto";
        voice.pitch = 210.0;
        voice.formantScale = 1.12;
        voice.breathiness = 0.05;
        voice.vibratoDepth = 12.0;
    } else if (name == "bass") {
        voice.name = "bass";
        voice.pitch = 95.0;
        voice.formantScale = 0.88;
        voice.harmonics = 52;
        voice.declination = 0.10;
    } else if (name == "child") {
        voice.name = "child";
        voice.pitch = 270.0;
        voice.formantScale = 1.28;
        voice.rate = 1.12;
        voice.vibratoDepth = 16.0;
    } else if (name == "robot") {
        voice.name = "robot";
        voice.pitch = 120.0;
        voice.declination = 0.0;
        voice.formantScale = 1.0;
        voice.vibratoRate = 0.0;
        voice.vibratoDepth = 0.0;
        voice.breathiness = 0.0;
        voice.harmonics = 18;
        voice.attack = 0.002;
        voice.release = 0.004;
    } else if (name == "whisper") {
        voice.name = "whisper";
        voice.pitch = 0.0;
        voice.declination = 0.0;
        voice.formantScale = 1.05;
        voice.breathiness = 1.0;
        voice.harmonics = 0;
    } else {
        throw std::invalid_argument("unknown voice preset: " + std::string(name));
    }

    return voice;
}

std::vector<std::string> voicePresetNames() {
    return {"neutral", "alto", "bass", "child", "robot", "whisper"};
}

} // namespace formant
