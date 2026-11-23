#include "formant/wav.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <string>

namespace formant {
namespace {

constexpr size_t kChunkFrames = 4096;

struct FormatInfo {
    uint16_t formatTag;
    uint16_t bitsPerSample;
};

FormatInfo formatInfo(AudioFormat format) {
    switch (format) {
    case AudioFormat::Pcm16:
        return {1u, 16u};
    case AudioFormat::Pcm24:
        return {1u, 24u};
    case AudioFormat::Float32:
        return {3u, 32u};
    case AudioFormat::RawPcm16:
    case AudioFormat::RawPcm24:
    case AudioFormat::RawFloat32:
        throw std::invalid_argument("raw audio format has no WAV header");
    }
    throw std::invalid_argument("unknown audio format");
}

template <typename T>
void appendLittleEndian(std::vector<uint8_t> &out, T value) {
    static_assert(sizeof(T) <= 4, "unexpected integer width");
    for (size_t i = 0; i < sizeof(T); ++i) {
        out.push_back(static_cast<uint8_t>((value >> (8 * i)) & 0xffu));
    }
}

double clampedSample(float sample) {
    if (!std::isfinite(sample)) {
        throw std::invalid_argument("audio samples must be finite");
    }
    return std::clamp(static_cast<double>(sample), -1.0, 1.0);
}

// A tiny xorshift source used only by the optional output dither. It is not
// part of synthesis: every audio sample still comes from deterministic sine
// partials. A fixed seed makes quantization noise reproducible across runs.
class DitherSource {
  public:
    explicit DitherSource(uint32_t seed) {
        // Seed zero is valid and distinct from one; xorshift just cannot start
        // from an all-zero state.
        state_ = seed ^ 0x6d2b79f5u;
        if (state_ == 0u) {
            state_ = 0x9e3779b9u;
        }
    }

    double uniform() {
        state_ ^= state_ << 13;
        state_ ^= state_ >> 17;
        state_ ^= state_ << 5;
        // Keep the top 24 bits so the conversion is exact in double.
        return static_cast<double>(state_ >> 8) / 16777216.0;
    }

    // Triangular probability density in (-1, 1) LSB units.
    double triangular() { return uniform() - uniform(); }

  private:
    uint32_t state_ = 0;
};

void validate(const std::vector<float> &samples, int sampleRate,
              AudioFormat format, const AudioWriteOptions &options) {
    if (options.dither && isFloatAudioFormat(format)) {
        throw std::invalid_argument(
            "dither only applies to 16-bit and 24-bit integer formats");
    }
    if (sampleRate <= 0) {
        throw std::invalid_argument("sample rate must be positive");
    }
    for (float sample : samples) {
        (void)clampedSample(sample);
    }
    if (!isRawAudioFormat(format)) {
        (void)wavHeader(samples.size(), sampleRate, format);
    }
}

void appendPcm16(std::vector<uint8_t> &out, float sample,
                 DitherSource *dither) {
    double scaled = clampedSample(sample) * 32767.0;
    if (sample < 0.0f) {
        scaled = clampedSample(sample) * 32768.0;
    }
    if (dither != nullptr) {
        scaled += dither->triangular();
    }
    const long rounded = std::llround(scaled);
    const int16_t value =
        static_cast<int16_t>(std::clamp<long>(rounded, -32768L, 32767L));
    appendLittleEndian<uint16_t>(out, static_cast<uint16_t>(value));
}

void appendPcm24(std::vector<uint8_t> &out, float sample,
                 DitherSource *dither) {
    const double clamped = clampedSample(sample);
    double scaled = clamped * 8388607.0;
    if (clamped < 0.0) {
        scaled = clamped * 8388608.0;
    }
    if (dither != nullptr) {
        scaled += dither->triangular();
    }
    const long rounded = std::llround(scaled);
    const int32_t value =
        static_cast<int32_t>(std::clamp<long>(rounded, -8388608L, 8388607L));
    const uint32_t bits = static_cast<uint32_t>(value);
    for (size_t i = 0; i < 3; ++i) {
        out.push_back(static_cast<uint8_t>((bits >> (8 * i)) & 0xffu));
    }
}

void appendFloat32(std::vector<uint8_t> &out, float sample) {
    static_assert(std::numeric_limits<float>::is_iec559,
                  "IEEE float WAV requires IEC 559 floats");
    const float clamped = static_cast<float>(clampedSample(sample));
    uint32_t bits = 0;
    std::memcpy(&bits, &clamped, sizeof(bits));
    appendLittleEndian<uint32_t>(out, bits);
}

void appendSample(std::vector<uint8_t> &out, float sample, AudioFormat format,
                  DitherSource *dither) {
    switch (format) {
    case AudioFormat::Pcm16:
    case AudioFormat::RawPcm16:
        appendPcm16(out, sample, dither);
        break;
    case AudioFormat::Pcm24:
    case AudioFormat::RawPcm24:
        appendPcm24(out, sample, dither);
        break;
    case AudioFormat::Float32:
    case AudioFormat::RawFloat32:
        appendFloat32(out, sample);
        break;
    }
}

} // namespace

int audioFormatSampleBytes(AudioFormat format) {
    switch (format) {
    case AudioFormat::Pcm16:
    case AudioFormat::RawPcm16:
        return 2;
    case AudioFormat::Pcm24:
    case AudioFormat::RawPcm24:
        return 3;
    case AudioFormat::Float32:
    case AudioFormat::RawFloat32:
        return 4;
    }
    throw std::invalid_argument("unknown audio format");
}

bool isRawAudioFormat(AudioFormat format) {
    switch (format) {
    case AudioFormat::RawPcm16:
    case AudioFormat::RawPcm24:
    case AudioFormat::RawFloat32:
        return true;
    case AudioFormat::Pcm16:
    case AudioFormat::Pcm24:
    case AudioFormat::Float32:
        return false;
    }
    throw std::invalid_argument("unknown audio format");
}

bool isFloatAudioFormat(AudioFormat format) {
    switch (format) {
    case AudioFormat::Float32:
    case AudioFormat::RawFloat32:
        return true;
    case AudioFormat::Pcm16:
    case AudioFormat::Pcm24:
    case AudioFormat::RawPcm16:
    case AudioFormat::RawPcm24:
        return false;
    }
    throw std::invalid_argument("unknown audio format");
}

std::vector<uint8_t> wavHeader(size_t sampleCount, int sampleRate,
                               AudioFormat format) {
    const FormatInfo info = formatInfo(format);
    if (sampleRate <= 0) {
        throw std::invalid_argument("sample rate must be positive");
    }
    const int sampleBytes = audioFormatSampleBytes(format);
    const uint64_t bytes64 =
        static_cast<uint64_t>(sampleCount) * static_cast<uint64_t>(sampleBytes);
    // The RIFF chunk size stores 36 + dataBytes, so the data itself must
    // leave room for the 36 bytes of header fields that follow the size.
    constexpr uint64_t maxDataBytes = 0xffffffffull - 36ull;
    if (bytes64 > maxDataBytes) {
        throw std::invalid_argument(
            "audio data is too large for a 32-bit WAV header");
    }
    const uint64_t byteRate64 =
        static_cast<uint64_t>(sampleRate) * static_cast<uint64_t>(sampleBytes);
    if (byteRate64 > 0xffffffffull) {
        throw std::invalid_argument("byte rate is too large for a WAV header");
    }
    const uint32_t dataBytes = static_cast<uint32_t>(bytes64);
    const uint32_t channels = 1;

    std::vector<uint8_t> header;
    header.reserve(44);
    header.insert(header.end(), {'R', 'I', 'F', 'F'});
    appendLittleEndian<uint32_t>(header, 36u + dataBytes);
    header.insert(header.end(), {'W', 'A', 'V', 'E'});
    header.insert(header.end(), {'f', 'm', 't', ' '});
    appendLittleEndian<uint32_t>(header, 16u);
    appendLittleEndian<uint16_t>(header, info.formatTag);
    appendLittleEndian<uint16_t>(header, static_cast<uint16_t>(channels));
    appendLittleEndian<uint32_t>(header, static_cast<uint32_t>(sampleRate));
    appendLittleEndian<uint32_t>(header, static_cast<uint32_t>(byteRate64));
    appendLittleEndian<uint16_t>(
        header,
        static_cast<uint16_t>(sampleBytes)); // block align
    appendLittleEndian<uint16_t>(header, info.bitsPerSample);
    header.insert(header.end(), {'d', 'a', 't', 'a'});
    appendLittleEndian<uint32_t>(header, dataBytes);
    return header;
}

void writeAudio(std::ostream &out, const std::vector<float> &samples,
                int sampleRate, AudioFormat format,
                const AudioWriteOptions &options) {
    // Validate before writing a header or samples. Non-finite values would
    // otherwise reach llround (undefined behaviour) and leave a corrupt file.
    validate(samples, sampleRate, format, options);

    const int sampleBytes = audioFormatSampleBytes(format);
    std::vector<uint8_t> header;
    if (!isRawAudioFormat(format)) {
        header = wavHeader(samples.size(), sampleRate, format);
        out.write(reinterpret_cast<const char *>(header.data()),
                  static_cast<std::streamsize>(header.size()));
        if (!out) {
            throw std::runtime_error("could not write audio header");
        }
    }

    DitherSource dither(options.seed);
    DitherSource *ditherPtr = options.dither ? &dither : nullptr;
    std::vector<uint8_t> chunk;
    chunk.reserve(kChunkFrames * static_cast<size_t>(sampleBytes));
    for (size_t base = 0; base < samples.size(); base += kChunkFrames) {
        const size_t end = std::min(base + kChunkFrames, samples.size());
        chunk.clear();
        for (size_t i = base; i < end; ++i) {
            appendSample(chunk, samples[i], format, ditherPtr);
        }
        out.write(reinterpret_cast<const char *>(chunk.data()),
                  static_cast<std::streamsize>(chunk.size()));
        if (!out) {
            throw std::runtime_error("could not write audio samples");
        }
    }
    out.flush();
    if (!out) {
        throw std::runtime_error("could not flush audio output");
    }
}

void writeAudioFile(std::string_view path, const std::vector<float> &samples,
                    int sampleRate, AudioFormat format,
                    const AudioWriteOptions &options) {
    // Validate everything that can fail before touching the file, matching the
    // original writeWav() contract that bad input never leaves a partial file.
    validate(samples, sampleRate, format, options);

    std::ofstream file(std::string(path), std::ios::binary | std::ios::trunc);
    if (!file) {
        throw std::runtime_error("could not open audio output: " +
                                 std::string(path));
    }
    writeAudio(file, samples, sampleRate, format, options);
}

void writeWav(std::string_view path, const std::vector<float> &samples,
              int sampleRate) {
    writeAudioFile(path, samples, sampleRate, AudioFormat::Pcm16);
}

} // namespace formant
