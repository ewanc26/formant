# Formant

Formant is a customisable voice synthesis engine written in C++20. It makes speech from sine waves only.

It is a library first and a command-line tool second. Embed it in a game, a robot, an accessibility tool or anything else that needs a voice without shipping a speech model, audio samples or a large runtime.

---

## How it works

Text, IPA or X-SAMPA goes through a pronunciation stage, then an additive sine-wave synthesiser. There are no recorded samples, no noise buffers and no external audio libraries. IPA is the primary input: the English lexicon is only an optional convenience layer.

```
text | [phonemes] | IPA | X-SAMPA
        │
        ▼
language front end (English, Spanish) or articulatory IPA mapping
                                       │
                                       ▼
                              phoneme sequence
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

# Speak IPA or X-SAMPA directly (language independent)
build/formant --ipa "ˈhəˈloʊ" --output ipa.wav
build/formant --xsampa "h@loU" --output xsampa.wav

# Speak Spanish text with the Castilian front end
build/formant --lang es --text "Hola, mundo." --output hola.wav

# Speak a user-defined conlang from a language definition file
build/formant --lang-file examples/velta.lang --text "Velta shael." --output velta.wav

# Read text from stdin
echo "Are you there?" | build/formant --stdin --output question.wav

# Inspect what is available
build/formant --list-voices
build/formant --list-phonemes
build/formant --list-ipa
build/formant --list-languages
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

### Languages

Text input is language-tagged. IPA, X-SAMPA and bracketed phonemes bypass the text layer and stay language-independent.

| Tag | Language | Front end |
|-----|----------|-----------|
| `en` | English | Small lexicon plus English letter rules (default) |
| `es` | Spanish (Castilian) | Rule-based G2P with orthographic stress |

```sh
build/formant --lang es --text "Hola, mundo." --output hola.wav
build/formant --list-languages
```

```cpp
#include <formant/formant.h>

const auto phonemes =
    formant::phonemize("Hola, mundo.", formant::Language::Spanish);
const auto samples = formant::synthesizeText(
    "Hola, mundo.", formant::Language::Spanish,
    formant::voicePreset("neutral"), 44100);
```

The language module only decides how text becomes phonemes. Formant tables, IPA parsing, prosody and the additive synthesiser are shared, so adding a language is a front-end job rather than a DSP job. This is the same separation used by eSpeak NG and MaryTTS.

#### Conlangs and user languages

A language definition file supplies a language as data: grapheme-to-IPA mappings, a word lexicon, an optional inventory, per-phoneme realization overrides and ordered context-sensitive rewrite rules. No C++ or rebuild is needed.

```sh
build/formant --lang-file examples/velta.lang --text "Velta tata, celta, toran'." --output velta.wav
build/formant --lang-file examples/velta.lang --trace --text "tata" --output tata.wav
```

```cpp
const auto velta = formant::parseLanguageDefinition(source);
const auto phonemes = formant::phonemize("Velta shael.", velta);
```

Rewrite rules use SCA-style `pattern > replacement / environment` bodies, class sets for vowels, consonants or arbitrary categories, `#` word edges, and `0` for deletion. Grapheme rules run before lookup; phoneme rules run after mapping and before realization overrides. `--trace` reports every application.

See [docs/language-files.md](docs/language-files.md) for the grammar, rule matching, trace output and override semantics. [`examples/velta.lang`](examples/velta.lang) is a complete example.

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

You can bypass the text layer entirely: call `parseIpa()`, `parseXsampa()` or `parsePhonemes()` and pass the result to `render()`. IPA is mapped from articulatory features (height, backness, rounding, place and manner) to four formant bands, so no language-specific dictionary is needed. The tables in `src/phoneme.cpp` and `src/ipa.cpp` are plain data, not hardcoded synthesis logic. To choose another text front end, call `phonemize(text, Language::Spanish)`; every front end returns the same `formant::Phoneme` sequence.

---

## Project layout

```
CMakeLists.txt          – C++20 library, CLI and CTest targets
include/formant/        – public headers (voice, phoneme, IPA, synthesizer, wav)
src/                    – synthesis engine, language front ends and IPA implementation
src/cli/                – command-line driver
tests/                  – unit tests and the CLI smoke check
examples/               – small text and phoneme inputs
docs/                   – synthesis design notes, language-file grammar and release process
.github/workflows/      – CI on Linux and macOS
```

---

## Verification

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
ctest --test-dir build --output-on-failure
```

The suite covers voice presets, English and Spanish text-to-phoneme conversion, language tags, user-defined language files, bracket parsing, the full IPA symbol inventory, X-SAMPA translation, stress/length/tone diacritics, deterministic rendering, WAV headers and a CLI end-to-end render. An optional sanitizer build is available with `-DFORMANT_SANITIZE=ON`.

---

## Limitations

- The English text layer is a small lexicon plus letter rules (including `-tion`, `-sion`, `-ing` and common contractions), not a full G2P model. The Spanish layer is a rule-based Castilian front end without a lexicon; dialect variation and allophonic realisations such as [β ð ɣ] are not modelled. IPA and X-SAMPA input avoid the text layer entirely.
- User-defined language files cover grapheme maps, word lexicons, inventories and per-phoneme realization overrides. Ordered rewrite rules, phonotactic generation and automatic stress/tone selection are not implemented.
- Only English and Spanish text front ends are built in. The IPA and X-SAMPA front ends remain language-independent.
- IPA covers the official chart plus common extensions: all vowels, pulmonic and non-pulmonic consonants, diacritics, tones and suprasegmentals. Clicks, trills, ejectives and rare extensions are approximated by sine partials rather than modelled as true airstream mechanisms.
- Sine-only synthesis cannot produce true fricative noise; loud `s` and `f` sounds are approximate.
- Output is mono PCM. Stereo, streaming and real-time playback are up to the embedder.

See [docs/design.md](docs/design.md) for the reasoning, [docs/release.md](docs/release.md) for the release process and [CHANGELOG.md](CHANGELOG.md) for what changed.

## Support

[![Ko-fi](https://img.shields.io/badge/Ko--fi-F16061?style=for-the-badge&logo=ko-fi&logoColor=white)](https://ko-fi.com/ewancroft)
[![GitHub Sponsors](https://img.shields.io/badge/GitHub%20Sponsors-30363D?style=for-the-badge&logo=github&logoColor=white)](https://github.com/sponsors/ewanc26)

## Licence

AGPL-3.0. See [LICENSE](LICENSE).
