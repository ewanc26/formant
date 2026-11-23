# Audio output

Formant renders floating-point PCM first and writes it through one dependency-free encoder. The default path is the original 44-byte, mono, 16-bit PCM WAV; it remains byte-identical to earlier releases. Additional formats are selected with `--format` and can go to a file or straight to stdout.

## Formats

| Name | Container | Sample encoding | Bytes/frame | WAV format tag |
|------|-----------|-----------------|-------------|----------------|
| `wav16` | WAV | signed 16-bit PCM | 2 | `WAVE_FORMAT_PCM` (1) |
| `wav24` | WAV | packed signed 24-bit PCM | 3 | `WAVE_FORMAT_PCM` (1) |
| `wav32f` | WAV | 32-bit IEEE float | 4 | `WAVE_FORMAT_IEEE_FLOAT` (3) |
| `raw16` | none | signed 16-bit PCM | 2 | — |
| `raw24` | none | packed signed 24-bit PCM | 3 | — |
| `raw32f` | none | 32-bit IEEE float | 4 | — |

All multi-byte values are little-endian on every host. Integer samples clamp at the exact full scale (`-32768`/`32767` and `-8388608`/`8388607`); float samples clamp to `[-1, 1]`. WAV files always have a canonical 44-byte header, so a valid header's byte rate is `sampleRate * bytesPerFrame` and its data size is `sampleCount * bytesPerFrame`.

```sh
# Deterministic WAV variants
formant --text "Hello, world." --format wav24 --output hello-24.wav
formant --text "Hello, world." --format wav32f --output hello-float.wav

# Headerless PCM for an encoder or player that already knows the rate
formant --ipa "ˈhəˈloʊ" --format raw16 --output hello.raw
```

The library exposes the same encoder:

```cpp
#include <formant/formant.h>

const auto samples = formant::synthesizeText("Hello, world.");
formant::writeAudioFile("hello-24.wav", samples, 44100,
                        formant::AudioFormat::Pcm24);

// writeWav(path, samples, rate) is still the unchanged 16-bit WAV writer.
```

## Streaming to stdout

`--output -` writes audio bytes to stdout in fixed 4096-frame chunks. Raw formats have no header, so the consumer must be given the sample rate. Human-readable status and `--trace` output move to stderr in this mode, leaving stdout byte-clean.

```sh
# Play raw PCM with a standard ALSA player
formant --text "Hello." --format raw16 --sample-rate 44100 --output - \
  | aplay -f S16_LE -c 1 -r 44100

# Pipe raw float PCM into ffmpeg
formant --ipa "ˈhəˈloʊ" --format raw32f --sample-rate 48000 --output - \
  | ffmpeg -f f32le -ar 48000 -ac 1 -i - hello.wav

# A WAV stream carries its own header
formant --text "Hello." --output - | ffplay -autoexit -
```

Chunk boundaries are a property of the writer, not of sample generation: every call with the same input and options emits the same byte sequence. No temporary file or platform-specific pipe shim is needed.

## Sample rates

`--sample-rate HZ` accepts any integer rate of at least 8000 Hz. The renderer returns `llround(totalDuration * sampleRate / rate)` samples, and both voiced partials and sine-noise partials derive their Nyquist ceiling from that rate. The voiced ceiling is `0.49 * sampleRate`; the 12 deterministic sine partials used for fricatives stop at `0.45 * sampleRate`. Consequently a higher rate admits more partials without folding energy back into the audible band.

```sh
formant --text "Testing one two three." --sample-rate 22050 --output 22k.wav
formant --text "Testing one two three." --sample-rate 48000 --output 48k.wav
formant --text "Testing one two three." --sample-rate 192000 --output 192k.wav
```

The `formant_formant_verification` CTest target renders a steady vowel at 22050, 44100, 48000, 96000 and 192000 Hz and measures its harmonic spectrum with a Goertzel filter. It checks that F1 and F2 stay at the phoneme table's values and dominate nearby off-formant harmonics, using a harmonic count that reaches beyond Nyquist at the lowest rate.

## Deterministic dither

Integer quantization can optionally use triangular dither from a fixed 32-bit seed:

```sh
formant --text "Quiet signal." --format wav16 --dither 42 --output dithered.wav
formant --text "Quiet signal." --format raw24 --dither=7 --output dithered.raw
```

The seed drives a small xorshift source with no wall-clock state. One seed produces one stable sequence for the whole render, independent of chunk boundaries or prior renders, so `sha256sum` is identical across runs. Dither is additive noise in the output encoder only; it is not a synthesis source, and the oscillator path remains sine-only. It is rejected for `wav32f` and `raw32f`, whose mantissa already has more precision than the synthesis source.

The same option is available to embedders:

```cpp
formant::AudioWriteOptions options;
options.dither = true;
options.seed = 42;
formant::writeAudioFile("dithered.wav", samples, 44100,
                        formant::AudioFormat::Pcm16, options);
```

Omitting `--dither`, or using `writeWav()` / `writeAudioFile()` without options, leaves quantization undithered and preserves the historical byte stream.
