# Formant

Formant is a customisable voice synthesis engine written in C++20. It makes speech from sine waves only.

It is a library first and a command-line tool second. Embed it in a game, a robot, an accessibility tool or anything else that needs a voice without shipping a speech model, audio samples or a large runtime.

---

## How it works

Text goes through a small pronunciation stage, then an additive sine-wave synthesiser. There are no recorded samples, no noise buffers and no external audio libraries.

```
text or [phonemes]
        │
        ▼
lexicon + English letter rules  ──►  phoneme sequence
                                       │
                                       ▼
                            formant targets (F1–F4)
                                       │
             ┌─────────────────────────┴──────────────────────────┐
             ▼                                                     ▼
   voiced: harmonics of F0                unvoiced: 12 detuned sine
   each amplitude shaped by               partials shaped by the same
   four resonant formants                 four formants
             └─────────────────────────┬──────────────────────────┘
                                       ▼
                          interpolated PCM → 16-bit WAV
```

A vowel is four resonances of the vocal tract. The glottal source is a stack of sine waves at multiples of the fundamental frequency; each harmonic is weighted by how close it sits to a formant. Fricatives and stops use a fixed set of sine partials with pseudo-random phases instead of white noise, so every sound in the output is still a sine wave. Formant targets are blended across phoneme boundaries to avoid clicks and to give coarticulation.

The result is deliberately synthetic: it sounds like a small speaking machine, not a human. The trade is that the whole engine is a few thousand lines of dependency-free C++ and runs anywhere you can compile it.

---

## Installation

Requirements: a C++20 compiler and CMake 3.20 or newer. There are no third-party dependencies.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
```

The CLI is `build/formant`. To install the library, headers and CLI:

```sh
cmake --install build --prefix ~/.local
```

---

## Usage

```sh
# Speak text
build/formant --text "Hello, world." --output hello.wav

# Pick a built-in voice and change parameters
build/formant --text "Formant is a voice engine." \
  --voice alto --pitch 190 --rate 1.05 --vibrato-depth 14 --output alto.wav

# Speak phonemes directly
build/formant --phonemes "[h ee l o , w er l d .]" --output raw.wav

# Read text from stdin
echo "Are you there?" | build/formant --stdin --output question.wav

# Inspect what is available
build/formant --list-voices
build/formant --list-phonemes
```

Run `build/formant --help` for the full option list. Every value flag also accepts `--flag=value`.

### Voices

| Preset | Character |
|--------|-----------|
| `neutral` | Mid pitch, gentle vibrato |
| `alto` | Higher pitch, longer vocal tract |
| `bass` | Low pitch, more harmonics |
| `child` | High pitch, shorter vocal tract, faster |
| `robot` | Flat pitch, fast attack, few harmonics |
| `whisper` | No voicing, sine-noise only |

### Customisation

A voice is a plain `formant::Voice` struct. Change any field, or start from a preset:

```cpp
#include <formant/formant.h>

formant::Voice voice = formant::voicePreset("alto");
voice.pitch = 200.0;          // Hz
voice.formantScale = 1.15;    // longer vocal tract
voice.vibratoDepth = 10.0;    // cents
voice.breathiness = 0.08;     // sine-noise under voiced sounds

const auto samples = formant::synthesizeText("Hello, world.", voice, 44100);
formant::writeWav("hello.wav", samples, 44100);
```

You can also bypass the English text layer entirely: edit `src/phoneme.cpp` to change formant frequencies, amplitudes and bandwidths, or call `parsePhonemes()` / `render()` with your own sequence. The phoneme table is data, not hardcoded logic.

---

## Project layout

```
CMakeLists.txt          – C++20 library, CLI and CTest targets
include/formant/        – public headers (voice, phoneme, synthesizer, wav)
src/                    – synthesis engine implementation
src/cli/                – command-line driver
tests/                  – unit tests and the CLI smoke check
examples/               – small text and phoneme inputs
docs/                   – synthesis design notes and roadmap
.github/workflows/      – CI on Linux and macOS
```

---

## Verification

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
ctest --test-dir build --output-on-failure
```

The suite covers voice presets, text-to-phoneme conversion, bracket parsing, deterministic rendering, WAV headers and a CLI end-to-end render. An optional sanitizer build is available with `-DFORMANT_SANITIZE=ON`.

---

## Limitations

- The text layer is a small English lexicon plus letter rules, not a full G2P model. Unusual words will be mispronounced.
- Sine-only synthesis cannot produce true fricative noise; loud `s` and `f` sounds are approximate.
- Output is mono PCM. Stereo, streaming and real-time playback are up to the embedder.

See [docs/design.md](docs/design.md) for the reasoning and [CHANGELOG.md](CHANGELOG.md) for what changed.

## Support

[![Ko-fi](https://img.shields.io/badge/Ko--fi-F16061?style=for-the-badge&logo=ko-fi&logoColor=white)](https://ko-fi.com/ewancroft)
[![GitHub Sponsors](https://img.shields.io/badge/GitHub%20Sponsors-30363D?style=for-the-badge&logo=github&logoColor=white)](https://github.com/sponsors/ewanc26)

## Licence

AGPL-3.0. See [LICENSE](LICENSE).
