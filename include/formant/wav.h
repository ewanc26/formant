#pragma once

#include <cstdint>
#include <iosfwd>
#include <string_view>
#include <vector>

namespace formant {

// Encodings supported by the audio writers. WAV formats write a canonical
// 44-byte RIFF header; raw formats write sample bytes only and carry no sample
// rate metadata, so callers must supply the rate to their player.
enum class AudioFormat {
    Pcm16,      // WAVE_FORMAT_PCM, 16-bit signed little-endian
    Pcm24,      // WAVE_FORMAT_PCM, packed 24-bit signed little-endian
    Float32,    // WAVE_FORMAT_IEEE_FLOAT, 32-bit little-endian IEEE float
    RawPcm16,   // 16-bit signed little-endian, no header
    RawPcm24,   // packed 24-bit signed little-endian, no header
    RawFloat32, // 32-bit little-endian IEEE float, no header
};

// Optional deterministic dither for integer encodings. One seed produces one
// fixed triangular dither sequence for the whole stream, independent of chunk
// boundaries, wall-clock state or previous renders. Float formats reject it:
// their mantissa already carries more precision than the synthesis source.
struct AudioWriteOptions {
    bool dither = false;
    uint32_t seed = 1;
};

// Number of bytes used for one mono sample in `format`.
int audioFormatSampleBytes(AudioFormat format);

// True for formats that deliberately omit a WAV header.
bool isRawAudioFormat(AudioFormat format);

// True for formats that use 32-bit floating-point samples.
bool isFloatAudioFormat(AudioFormat format);

// Writes a 44-byte mono WAV header. Raw formats are rejected because they have
// no header. Throws std::invalid_argument for an invalid rate, format or data
// size.
std::vector<uint8_t> wavHeader(size_t sampleCount, int sampleRate,
                               AudioFormat format = AudioFormat::Pcm16);

// Streams encoded audio in fixed-size chunks of 4096 frames. Sample values and
// options are validated before anything is written, so a non-finite sample or
// an invalid option cannot leave a partially written stream. All byte order is
// little-endian regardless of the host. Throws std::invalid_argument for bad
// input and std::runtime_error on I/O failure.
void writeAudio(std::ostream &out, const std::vector<float> &samples,
                int sampleRate, AudioFormat format,
                const AudioWriteOptions &options = {});

// File variant of writeAudio(). Opens in binary mode and truncates the file.
void writeAudioFile(std::string_view path, const std::vector<float> &samples,
                    int sampleRate, AudioFormat format,
                    const AudioWriteOptions &options = {});

// Backwards-compatible 16-bit PCM WAV writer. Its output is byte-identical to
// every earlier release and never applies dither.
void writeWav(std::string_view path, const std::vector<float> &samples,
              int sampleRate);

} // namespace formant
