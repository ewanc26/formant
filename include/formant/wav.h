#pragma once

#include <cstdint>
#include <string_view>
#include <vector>

namespace formant {

// Writes mono 16-bit PCM WAV data. Throws std::runtime_error on I/O failure.
// Values outside [-1, 1] are clamped before conversion.
void writeWav(std::string_view path,
              const std::vector<float>& samples,
              int sampleRate);

// Builds the 44-byte header on its own, handy for streaming or embedding.
std::vector<uint8_t> wavHeader(size_t sampleCount, int sampleRate);

} // namespace formant
