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

# Generate deterministic, seeded gibberish from an Awkwords-style pattern
build/formant --gibberish "CVC" --seed 7 --count 4 \
  --transcript babble.txt --output babble.wav
# Sample language-like words from committed n-gram statistics
build/formant --gibberish "ignored" --gibberish-style en-us --seed 7 --count 4 \
  --output babble-stat.wav
build/formant --list-pattern-help

# Speak Spanish text with the Castilian front end
build/formant --lang es --text "Hola, mundo." --output hola.wav

# Speak a user-defined conlang from a language definition file
build/formant --lang-file examples/velta.lang --text "Velta shael." --output velta.wav

# Apply a user pronunciation lexicon (names and homographs as data)
build/formant --text "Ewan lead us" --lexicon names.lex --trace --output names.wav

# Speak an SSML document (breaks, inline IPA, say-as and prosody spans)
build/formant --ssml-file script.ssml --output ssml.wav

# Layer exact F0, duration and amplitude targets over the default prosody
build/formant --text "Hello, world." --prosody-file script.prosody --output exact.wav

# Load a shareable voice definition and sweep or compare voice parameters
build/formant --text "Hello, world." --voice-file examples/warm.voice --output warm.wav
# Inspect a distributed package and create one from a preset
build/formant --inspect-voice examples/voices/warm-narrator.voice
build/formant --package-voice alto --voice-author "Your Name" \
  --voice-description "Warm alto" --voice-license CC0-1.0 --voice-version 1.0.0 > my-alto.voice
build/formant --text "Hello, world." \
  --sweep "pitch=120:200:5,breathiness=0:0.08:3" --lab-dir lab
build/formant --text "Hello, world." --pair "neutral,alto" --lab-dir ab

# Expand symbolic ToBI intonation (named tune and/or annotation file)
build/formant --text "Are you there?" --intonation-tune yesno --output question.wav
build/formant --text "Hello, world." --intonation-file script.intonation --output intoned.wav

# Read text from stdin
echo "Are you there?" | build/formant --stdin --output question.wav

# Generate the MRT intelligibility battery for listening tests
build/formant --mrt /tmp/mrt-neutral --voice neutral --rate 1.0 --pitch 120

# Inspect what is available
build/formant --list-voices
build/formant --list-voice-parameters
build/formant --list-phonemes
build/formant --list-ipa
build/formant --list-languages
```

Run `build/formant --help` for the full option list. Every value flag also accepts `--flag=value`.

### Output formats and streaming

The default is still a deterministic 44-byte mono 16-bit PCM WAV. Additional
encodings cover archive and pipeline use, and `--output -` writes bytes to
stdout in fixed 4096-frame chunks with all status messages on stderr:

```sh
# 24-bit PCM WAV and 32-bit float WAV
build/formant --text "Hello, world." --format wav24 --output hello-24.wav
build/formant --text "Hello, world." --format wav32f --output hello-float.wav

# Headerless PCM, piped live into a player (rate is not stored in raw output)
build/formant --text "Hello." --format raw16 --sample-rate 44100 --output - \
  | aplay -f S16_LE -c 1 -r 44100

# Deterministic dither for integer output (same seed, same bytes)
build/formant --text "Hello." --format wav16 --dither 42 --output dithered.wav
```

`--sample-rate` accepts any rate from 8 kHz upward; the harmonic ceiling is
derived from it, so spectra scale safely up to 192 kHz and beyond. See
[docs/audio-output.md](docs/audio-output.md) for the format table, WAV tags,
dither semantics and library API.

### IPA coverage conformance

The full PHOIBLE 2.0 segment inventory is checked mechanically: all 2,162
documented symbols must parse and render non-silence, with a committed
per-category coverage table and a documented approximation policy for the
symbols the sine-only model does not support natively. See
[docs/phoible.md](docs/phoible.md).

### Objective metrics

The verification harness measures F0 error, gain-independent mel-envelope
distance, mel-cepstral distortion (MCD) and formant-position error against the
*intended* prosody and resonance tracks, with no third-party dependency:

```sh
ctest --test-dir build --output-on-failure
./build/formant_formant_verification --report /tmp/metrics-report.txt
```

CI compares a five-case corpus against checked-in baselines and runs a
deliberate-regression check, so a synthesis change that shifts pitch or formants
is caught mechanically. The analyzer API is also public through
`formant/metrics.h`. Definitions, corpus details and thresholds are in
[docs/metrics.md](docs/metrics.md).

An optional offline harness adds a per-band Hz RMS acoustic diff against golden
formant-trajectory fixtures, plus a broken-table dry run:

```sh
cmake --build build --target formant_verify
cat build/formant-verify-report.txt
```

It is not part of default `ctest`; see
[docs/trajectory-verification.md](docs/trajectory-verification.md).

### Voices

| Preset | Character |
|--------|-----------|
| `neutral` | Mid pitch, gentle vibrato |
| `alto` | Higher pitch, longer vocal tract |
| `bass` | Low pitch, more harmonics |
| `child` | High pitch, shorter vocal tract, faster |
| `robot` | Flat pitch, fast attack, few harmonics |
| `whisper` | No voicing, sine-noise only |

A voice can also be a file layered over one of these presets, and the lab can
sweep parameters or render A/B pairs on identical text. See
[docs/voice-files.md](docs/voice-files.md) for the schema, safe ranges and a
tuning cookbook, and [docs/voice-ecosystem.md](docs/voice-ecosystem.md) for
shareable schema 2 packages with attribution and checksums. Eight ready-to-use
packages live in [examples/voices](examples/voices); the six preset packages
render byte-identically to their compiled counterparts.
`--list-voice-parameters` prints the same table as data, `--dump-voice NAME`
exports a preset as a byte-identical round-trippable file, `--package-voice`
adds attribution and a checksum for sharing, and `--inspect-voice` verifies a
received package without rendering.

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

A language definition file supplies a language as data: grapheme-to-IPA mappings, a word lexicon, an optional inventory, per-phoneme realization overrides, language-level typology defaults and ordered context-sensitive rewrite rules. No C++ or rebuild is needed.

```sh
build/formant --lang-file examples/velta.lang --text "Velta tata, celta, toran'." --output velta.wav
build/formant --lang-file examples/velta.lang --trace --text "tata" --output tata.wav
build/formant --lang-file examples/thalen.lang --list-phonemes   # prints the conlang inventory
```

```cpp
const auto velta = formant::parseLanguageDefinition(source);
const auto phonemes = formant::phonemize("Velta shael.", velta);
```

Rewrite rules use SCA-style `pattern > replacement / environment` bodies, class sets for vowels, consonants or arbitrary categories, `#` word edges, and `0` for deletion. Grapheme rules run before lookup; phoneme rules run after mapping and before realization overrides. `--trace` reports every application.

A `strict` inventory rejects undeclared phonemes and names the nearest declaration; in `lenient` mode an undeclared phoneme falls back to its nearest declared neighbour with a warning. `tone`, `stress`, `rhythm` and `rate` carry language-level typology as data (`rate` scales durations, `stress=fixed` gives each word a default initial stress). `--list-phonemes` reports the active conlang's inventory.

See [docs/language-files.md](docs/language-files.md) for the grammar, rule matching, trace output, fallback and override semantics. [`examples/velta.lang`](examples/velta.lang) and [`examples/thalen.lang`](examples/thalen.lang) are complete examples.

#### User lexicons

A lexicon file adds exact pronunciation entries and context-sensitive homograph rules on top of any text front end. It composes with `--lang` and `--lang-file`; `--trace` reports whether each word came from a homograph rule, an exact entry or the language fallback.

```sh
build/formant --text "Ewan lead us" --lexicon names.lex --output names.wav
build/formant --text "Ewan" --lang es --lexicon names.lex --trace --output names.wav
```

```cpp
const auto lex = formant::parsePronunciationLexicon(source);
std::vector<std::string> trace;
const auto phonemes =
    formant::phonemize("Ewan lead us", formant::Language::English, lex, &trace);
```

See [docs/lexicons.md](docs/lexicons.md) for the file format, layer precedence, validation trace and documented limits.

### Exact timing and captions

Because every phoneme's duration is planned before synthesis, Formant can emit
sample-accurate timing alongside the audio. This powers captions, karaoke
highlights and lip-sync tracks without forced alignment.

```sh
# Word/phoneme timing JSON, SRT, WebVTT and enhanced LRC captions
build/formant --text "Hello, world." --output hello.wav --timing hello.json
build/formant --text "Hello, world." --output hello.wav --timing hello.srt
build/formant --text "Hello, world." --output hello.wav --timing hello.vtt
build/formant --text "Hello, world." --output hello.wav --timing hello.lrc

# A separate viseme track for animation rigs
build/formant --text "Hello, world." --output hello.wav --visemes hello.visemes.json
```

`--timing` picks the format from the extension (`.json`, `.srt`, `.vtt`,
`.lrc`). Word boundaries match the WAV sample count exactly; for `--ipa`,
`--xsampa` and `--phonemes` the words are derived from the phoneme stream.
See [docs/timing.md](docs/timing.md) for the file schemas, the documented
viseme mapping and the `planTiming()` / `planVisemes()` library API.

### Customisation

A voice is a plain `formant::Voice` struct. Change any field, or start from a preset:

```cpp
#include <formant/formant.h>

formant::Voice voice = formant::voicePreset("alto");
voice.pitch = 200.0;          // Hz
voice.formantScale = 1.15;    // longer vocal tract
voice.vibratoDepth = 10.0;    // cents
voice.breathiness = 0.08;     // sine-noise under voiced sounds
voice.taperCeiling = true;    // fade partials before Nyquist (default)

// Parameter-domain transposition: no audio post-processing.
formant::Voice higher = formant::shiftPitch(voice, 300.0);   // cents
formant::Voice wider = formant::scaleFormants(voice, 1.15);  // resonances
formant::Voice character = formant::transposeVoice(voice, -200.0, 1.10);

const auto samples = formant::synthesizeText("Hello, world.", voice, 44100);
formant::writeWav("hello.wav", samples, 44100);
```

For sentence-by-sentence rendering, carry the render state between calls so the glottal phase and noise time base continue across the splice. The final result is byte-identical to rendering the concatenated phonemes in one call:

```cpp
formant::Synthesizer synth(voice);
formant::RenderResult result;
for (const auto &sentence : sentences) {
    result = synth.renderContinued(formant::phonemize(sentence), 44100,
                                   std::move(result.state));
}
formant::writeWav("speech.wav", result.samples, 44100);
```

You can bypass the text layer entirely: call `parseIpa()`, `parseXsampa()` or `parsePhonemes()` and pass the result to `render()`. IPA is mapped from articulatory features (height, backness, rounding, place and manner) to four formant bands, so no language-specific dictionary is needed. The tables in `src/phoneme.cpp` and `src/ipa.cpp` are plain data, not hardcoded synthesis logic. To choose another text front end, call `phonemize(text, Language::Spanish)`; every front end returns the same `formant::Phoneme` sequence.

---

## Project layout

```
CMakeLists.txt          – C++20 library, CLI and CTest targets
include/formant/        – public headers (voice, phoneme, IPA, synthesizer, wav, metrics)
src/                    – synthesis engine, language front ends and IPA implementation
src/cli/                – command-line driver
tests/                  – unit tests, objective-metric harness, baselines and CLI smoke check
examples/               – small text and phoneme inputs
docs/                   – synthesis design, audio output, objective metrics, language files and release process
.github/workflows/      – CI on Linux and macOS
```

---

## Verification

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
ctest --test-dir build --output-on-failure
```

The suite covers voice presets, English and Spanish text-to-phoneme conversion, language tags, user-defined language files, bracket parsing, the full IPA symbol inventory, X-SAMPA translation, stress/length/tone diacritics, deterministic rendering, WAV headers and encodings, deterministic dither, formant positions at five sample rates, F0/envelope/MCD metrics against checked-in baselines, deliberate-regression detection and CLI end-to-end renders. An optional sanitizer build is available with `-DFORMANT_SANITIZE=ON`.

---

## Limitations

- The English text layer is a small lexicon plus letter rules (including `-tion`, `-sion`, `-ing` and common contractions), not a full G2P model. The Spanish layer is a rule-based Castilian front end without a lexicon; dialect variation and allophonic realisations such as [β ð ɣ] are not modelled. IPA and X-SAMPA input avoid the text layer entirely.
- User-defined language files cover grapheme maps, word lexicons, inventories, per-phoneme realization overrides, nearest-feature inventory fallback and language-level typology defaults (`rate`, `stress=fixed`; `tone`/`rhythm` are recorded metadata). Ordered rewrite rules are supported; free tone/stress assignment remains unimplemented. Seeded phonotactic word generation is available separately via `--gibberish` (see [docs/gibberish.md](docs/gibberish.md)).
- Only English and Spanish text front ends are built in. The IPA and X-SAMPA front ends remain language-independent.
- IPA covers the official chart plus common extensions: all vowels, pulmonic and non-pulmonic consonants, diacritics, tones and suprasegmentals. Clicks, trills, ejectives and rare extensions are approximated by sine partials rather than modelled as true airstream mechanisms.
- Sine-only synthesis cannot produce true fricative noise; loud `s` and `f` sounds are approximate.
- Output is mono PCM. Stereo, streaming and real-time playback are up to the embedder.

See [docs/phoible.md](docs/phoible.md) for the PHOIBLE 2.0 segment conformance table, per-category coverage and the documented approximation policy, and [data/phoible/PROVENANCE.md](data/phoible/PROVENANCE.md) for provenance and licence.

See [docs/design.md](docs/design.md) for the reasoning, [docs/gibberish.md](docs/gibberish.md) for seeded word generation, [docs/property-testing.md](docs/property-testing.md) for property/fuzz testing and the checksum corpus, [docs/metrics.md](docs/metrics.md) for objective metrics, [docs/trajectory-verification.md](docs/trajectory-verification.md) for the offline formant-trajectory harness, [docs/performance.md](docs/performance.md) for the benchmark harness and real-time-factor budget, [docs/voice-files.md](docs/voice-files.md) for voice tuning and the lab, [docs/voice-ecosystem.md](docs/voice-ecosystem.md) for sharing voices, [docs/release.md](docs/release.md) for the release process and [CHANGELOG.md](CHANGELOG.md) for what changed.

## Support

[![Ko-fi](https://img.shields.io/badge/Ko--fi-F16061?style=for-the-badge&logo=ko-fi&logoColor=white)](https://ko-fi.com/ewancroft)
[![GitHub Sponsors](https://img.shields.io/badge/GitHub%20Sponsors-30363D?style=for-the-badge&logo=github&logoColor=white)](https://github.com/sponsors/ewanc26)

## Licence

AGPL-3.0. See [LICENSE](LICENSE).

Copyright (C) 2026 Ewan Croft (ewanc26).
