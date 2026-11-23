# Exact timing output

Formant plans every phoneme's duration before it renders a single sample. That
plan is now available as data, so captions, karaoke highlights and lip-sync
tracks can be built from the same schedule that produced the WAV instead of
being recovered from the audio afterwards.

All timing values are exact by construction:

- `startSample` / `endSample` are integer PCM sample indices into the rendered
  buffer. They come from the same cumulative scheduler used by
  `Synthesizer::render()`, rounded with `llround(cumulativeDuration *
  sampleRate / rate)` exactly as the audio length is.
- `start` / `end` are the same boundaries in seconds (`sample / sampleRate`).
  The speaking rate is already reflected in the sample counts.
- `totalSamples` equals the number of samples in the WAV written in the same
  CLI invocation. For a fast `--rate` this may be zero, which is a valid,
  empty render.

No forced alignment, acoustic model or listening is involved, and the output
is deterministic: the same voice, text, rate and sample rate always produce
byte-identical timing files.

## CLI usage

```sh
# JSON timing with word and phoneme boundaries, plus captions
build/formant --text "Hello, world." --output hello.wav \
  --timing hello.json

# WebVTT and SRT captions
build/formant --text "Hello, world." --output hello.wav \
  --timing hello.vtt
build/formant --text "Hello, world." --output hello.wav \
  --timing hello.srt

# LRC with word-level timestamps for karaoke
build/formant --text "Hello, world." --output hello.wav \
  --timing hello.lrc

# A separate lip-sync viseme track
build/formant --text "Hello, world." --output hello.wav \
  --visemes hello.visemes.json
```

`--timing` infers its format from the file extension: `.json`, `.srt`, `.vtt`
or `.lrc` (case-insensitive). Any other extension is rejected before rendering
so a typo cannot silently write JSON under the wrong name. `--visemes` always
writes JSON. Both files are written alongside the WAV and reported on stdout.

Word labels come from the source text when text input is used, including
Unicode spelling and internal apostrophes (`don't`). For `--phonemes`,
`--ipa` and `--xsampa` there are no source words, so words are derived
deterministically from runs of phonemes between `|` gaps and labelled with
their phoneme names.

## Timing JSON

The `.json` timing file contains the plan and, for convenience, the viseme
track derived from it:

```json
{
  "format": "formant-timing",
  "version": 1,
  "sampleRate": 22050,
  "totalSamples": 26791,
  "duration": 1.215011,
  "voice": "neutral",
  "words": [
    {"label": "Hello", "startSample": 0, "endSample": 12679, "start": 0.000000, "end": 0.575011},
    {"label": "world", "startSample": 12679, "endSample": 26791, "start": 0.575011, "end": 1.215011}
  ],
  "phonemes": [ ... ],
  "visemes": [ ... ]
}
```

Words are contiguous: each word ends where the next begins, and the last word
ends at `totalSamples`. Pause phonemes and the `|` word-gap markers between
words are folded into the preceding word's span so captions stay contiguous;
they are still present as individual entries in `phonemes` and `visemes`.

Numbers use a fixed six-decimal format and strings are JSON-escaped, so files
are stable across platforms and safe to embed in other JSON documents.

## Caption formats

- **SRT** (`--timing file.srt`): numbered cues with `HH:MM:SS,mmm --> ...`.
- **WebVTT** (`--timing file.vtt`): a `WEBVTT` header followed by cues with
  `HH:MM:SS.mmm --> ...`.
- **LRC** (`--timing file.lrc`): karaoke-friendly enhanced LRC with a
  `[ar:formant]` tag and word-level `<mm:ss.xx>` timestamps inside each line.

With no phonemes, SRT has no cues, VTT is just the header, and LRC has no
tagged words.

## Viseme track

`--visemes FILE` writes a compact JSON document with one frame per phoneme:

```json
{
  "format": "formant-visemes",
  "version": 1,
  "sampleRate": 22050,
  "totalSamples": 26791,
  "frames": [
    {"shape": "glottal", "openness": 0.400, "phoneme": "h", "startSample": 0, "endSample": 1323, "start": 0.000000, "end": 0.060000},
    {"shape": "spread", "openness": 0.550, "phoneme": "e", "startSample": 1323, "endSample": 4190, "start": 0.060000, "end": 0.190023}
  ]
}
```

`shape` is one of the stable names below. `openness` is an approximate mouth
aperture from 0 (closed) to 1 (wide open); it is animation guidance, not an
acoustic measurement.

| Shape | Typical phonemes |
|-------|------------------|
| `open` | low vowels `a`, `ɑ`, `æ`, `aɪ`, `aʊ` |
| `spread` | front vowels `i`, `ɪ`, `e`, `ɛ`, `ee` |
| `central` | mid/central vowels `ə`, `ɜ`, `ʌ` |
| `round` | back rounded vowels and glides `u`, `o`, `ɔ`, `ʊ`, `w` |
| `rhotic` | rhotic vowels and approximants `ɹ`, `r`, `er` |
| `closed` | bilabials and nasals `p`, `b`, `m` |
| `labiodental` | `f`, `v` |
| `dental` | `θ`, `ð` |
| `alveolar` | `t`, `d`, `n`, `s`, `z`, `l` |
| `postalveolar` | `ʃ`, `ʒ`, `tʃ`, `dʒ` |
| `velar` | `k`, `g`, `ŋ` |
| `glottal` | `h`, `ʔ` |
| `rest` | pauses and word gaps `,`, `.`, `|` |
| `narrow` | fallback for unlisted fricatives and approximants |

The mapping is a plain data table in `src/timing.cpp`. Unknown or future
phonemes fall back to a shape derived from their `PhonemeKind` (`narrow` for
fricatives and approximants, `closed` for nasals and plosives, `open` for voiced
sounds and `rest` for silence), so a new inventory entry never breaks a
lip-sync track.

## Library API

Embedders can build the same plans without the CLI:

```cpp
#include <formant/formant.h>

const formant::Voice voice = formant::voicePreset("neutral");
const auto phonemes = formant::phonemize("Hello, world.");

const formant::TimingPlan plan = formant::planTiming(
    phonemes, voice, 44100,
    formant::splitWordsForTiming("Hello, world."));
const std::vector<formant::VisemeFrame> visemes =
    formant::planVisemes(plan);

const std::string json = formant::timingJson(plan, visemes);
const std::string srt = formant::timingSrt(plan);
```

`planTiming` accepts optional `wordLabels`. They are used only when there is
exactly one label per detected word; otherwise labels are derived from the
phonemes, which prevents silently misaligned captions. `planVisemes`,
`timingJson`, `timingVisemesJson`, `timingSrt`, `timingVtt` and `timingLrc` are
pure functions of their inputs.

## Scope

This release covers offline timing for one utterance. Dialogue/multi-voice
timeline assembly, stereo panning and live streaming are separate roadmap
items; the sample-accurate plan here is the foundation those can build on.
