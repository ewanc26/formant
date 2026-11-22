#include "formant/wav.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <fstream>
#include <stdexcept>
#include <string>

namespace formant {
namespace {

template <typename T>
void appendLittleEndian(std::vector<uint8_t> &out, T value) {
    static_assert(sizeof(T) <= 4, "unexpected integer width");
    for (size_t i = 0; i < sizeof(T); ++i) {
        out.push_back(static_cast<uint8_t>((value >> (8 * i)) & 0xff));
    }
}

} // namespace

std::vector<uint8_t> wavHeader(size_t sampleCount, int sampleRate) {
    if (sampleRate <= 0) {
        throw std::invalid_argument("sample rate must be positive");
    }
    const uint64_t bytes64 = static_cast<uint64_t>(sampleCount) * 2u;
    // The RIFF chunk size stores 36 + dataBytes, so the data itself must
    // leave room for the 36 bytes of header fields that follow the size.
    constexpr uint64_t maxDataBytes = 0xffffffffull - 36ull;
    if (bytes64 > maxDataBytes) {
        throw std::invalid_argument(
            "audio data is too large for a 32-bit WAV header");
    }
    const uint32_t dataBytes = static_cast<uint32_t>(bytes64);
    const uint32_t channels = 1;
    const uint32_t byteRate = static_cast<uint32_t>(sampleRate) * channels * 2u;

    std::vector<uint8_t> header;
    header.reserve(44);
    header.insert(header.end(), {'R', 'I', 'F', 'F'});
    appendLittleEndian<uint32_t>(header, 36u + dataBytes);
    header.insert(header.end(), {'W', 'A', 'V', 'E'});
    header.insert(header.end(), {'f', 'm', 't', ' '});
    appendLittleEndian<uint32_t>(header, 16u);
    appendLittleEndian<uint16_t>(header, 1u); // PCM
    appendLittleEndian<uint16_t>(header, static_cast<uint16_t>(channels));
    appendLittleEndian<uint32_t>(header, static_cast<uint32_t>(sampleRate));
    appendLittleEndian<uint32_t>(header, byteRate);
    appendLittleEndian<uint16_t>(header, 2u);  // block align
    appendLittleEndian<uint16_t>(header, 16u); // bits per sample
    header.insert(header.end(), {'d', 'a', 't', 'a'});
    appendLittleEndian<uint32_t>(header, dataBytes);
    return header;
}

void writeWav(std::string_view path, const std::vector<float> &samples,
              int sampleRate) {
    // Validate before opening the file. std::llround on a non-finite value is
    // undefined behaviour, and failing after the header is written would leave
    // a corrupt WAV behind.
    for (float sample : samples) {
        if (!std::isfinite(sample)) {
            throw std::invalid_argument("WAV samples must be finite");
        }
    }
    const std::vector<uint8_t> header = wavHeader(samples.size(), sampleRate);
    std::ofstream file(std::string(path), std::ios::binary | std::ios::trunc);
    if (!file) {
        throw std::runtime_error("could not open WAV output: " +
                                 std::string(path));
    }
    file.write(reinterpret_cast<const char *>(header.data()),
               static_cast<std::streamsize>(header.size()));
    for (float sample : samples) {
        const double clamped =
            std::clamp(static_cast<double>(sample), -1.0, 1.0);
        const double scale = clamped < 0.0 ? 32768.0 : 32767.0;
        const auto value = static_cast<int16_t>(std::llround(clamped * scale));
        file.write(reinterpret_cast<const char *>(&value), sizeof(value));
    }
    if (!file) {
        throw std::runtime_error("could not write WAV output: " +
                                 std::string(path));
    }
}

} // namespace formant
