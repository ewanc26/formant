# Voice definition files and the parameter lab

A voice definition file makes a voice shareable data instead of a recompile. It
layers a small set of numeric parameters over a built-in preset, which keeps
files short and makes every built-in voice expressible as a file. The same
`formant::Voice` struct still feeds the synthesiser, so the
text → phonemes → formant targets → sine rendering pipeline is unchanged and
rendering stays deterministic.

```sh
# Discover the parameters and their safe neighbourhoods.
build/formant --list-voice-parameters

# Export any built-in preset as a complete, round-trippable definition.
build/formant --dump-voice alto > alto.voice

# Load a definition over its base preset.
build/formant --text "Hello, world." --voice-file alto.voice --output hello.wav

# Sweep one or two parameters, or compare two voices on identical text.
build/formant --text "Hello, world." \
  --sweep "pitch=120:200:5,breathiness=0:0.08:3" --lab-dir lab
build/formant --text "Hello, world." --pair "neutral,alto" --lab-dir lab
```

For sharing voices, the versioned distribution format adds attribution and a
checksum on top of these definitions; see
[voice-ecosystem.md](voice-ecosystem.md) and `examples/voices/`.

The C++ API mirrors the file API:

```cpp
#include <formant/formant.h>

const formant::VoiceDefinition definition =
    formant::loadVoiceDefinition("alto.voice");
const formant::Voice voice = formant::resolveVoice(definition);
const auto samples = formant::Synthesizer(voice).render(
    formant::phonemize("Hello, world."));
```

## File format

The file is UTF-8 text, one directive per line. Blank lines and text after `#`
are comments. Fields are whitespace-separated. The `schema` directive is
required so future revisions can be detected instead of silently misread.
`name` is optional metadata; `base` defaults to `neutral`.

```ebnf
definition = { line } ;
line       = [ directive ] [ comment ] newline ;

directive  = "schema" "1"
           | "name" text
           | "base" preset
           | parameter number
           | "taper-ceiling" boolean ;

preset     = "neutral" | "alto" | "bass" | "child" | "robot" | "whisper" ;
boolean    = "true" | "false" | "1" | "0" ;
parameter  = "pitch" | "declination" | "rate" | "formant-scale"
           | "vibrato-rate" | "vibrato-depth" | "breathiness"
           | "nasality" | "harmonics" | "attack" | "release" | "gain"
           | "jitter-cents" | "shimmer" ;
```

A minimal definition changes just one knob:

```text
# Warm, slightly breathy alto.
schema 1
name Warm Alto
base alto
pitch 196.5
breathiness 0.02
```

Unset parameters keep the base preset's value. A definition that sets every
parameter, as printed by `--dump-voice`, is a complete snapshot. Loading that
file reproduces the compiled-in voice byte-for-byte, which is tested for all
six presets. Values are written with 17 significant digits so an IEEE double
round-trips exactly.

## Validation and early failure

Files are parsed and validated before any audio is written:

- an unknown directive or parameter is an error with its line number,
  e.g. `bad.voice: voice line 3: unknown parameter 'wobble'`;
- a missing or unsupported `schema` is rejected;
- a non-finite number, an extra field, or a value outside the parameter's hard
  interval is rejected;
- `harmonics` must be an integer;
- an unknown `base` is rejected as an unknown voice preset.

A value inside the hard interval but outside the documented safe neighbourhood
prints a warning to stderr and is still accepted. That keeps experimental
voices possible without hiding the corner cases described below. Warnings are
only emitted for values the file actually changes, so a round-tripped preset
does not warn about its deliberate extremes.

## Tuning cookbook: safe ranges

Hard bounds are what the loader accepts. Safe bounds are the neighbourhood
where the parameter behaves predictably at ordinary speaking rates; beyond
them the renderer still produces finite audio, but formant resonances can
clip, partials can crowd the Nyquist ceiling, or timing can become unnatural.
Start at a preset and move one axis at a time through the safe band.

| Parameter | Hard min | Hard max | Safe min | Safe max | Notes |
|-----------|---------:|---------:|---------:|---------:|------|
| `pitch` | 0 | 2000 | 60 | 400 | Hz; `0` disables voicing (whisper). Above ~400 the harmonic stack reaches the Nyquist taper. |
| `declination` | 0 | 1 | 0 | 0.3 | Downward F0 drift across a phrase. |
| `rate` | 0.01 | 5 | 0.5 | 2 | Speaking-rate multiplier; very slow rates make long, unnaturally steady vowels. |
| `formant-scale` | 0.25 | 4 | 0.7 | 1.5 | Vocal-tract length multiplier. Beyond ~1.5 resonances approach the clamp in `prosody.h`. |
| `vibrato-rate` | 0 | 20 | 3 | 8 | Hz; `0` disables vibrato. |
| `vibrato-depth` | 0 | 200 | 0 | 40 | Cents; large depths sound like a siren. |
| `breathiness` | 0 | 1 | 0 | 0.15 | Deterministic sine-noise under voicing. The `whisper` preset deliberately uses `1`. |
| `nasality` | 0 | 1 | 0 | 0.35 | Pervasive nasal twang: a fixed 1000 Hz anti-formant applied to every phoneme. Per-phoneme zeros for `m`, `n`, `ng` and nasalized vowels are table data and are independent of it. |
| `harmonics` | 0 | 512 | 12 | 80 | Sine partials in the glottal source. Higher counts mainly cost time near Nyquist. |
| `attack` | 0 | 1 | 0.002 | 0.05 | Seconds; very slow attacks soften plosives. |
| `release` | 0 | 2 | 0.002 | 0.1 | Seconds; long releases blur word boundaries. |
| `gain` | 0.001 | 4 | 0.3 | 1 | Level before peak normalisation. |
| `jitter-cents` | 0 | 50 | 0 | 12 | Bounded F0 irregularity from two incommensurate sines; `0` disables the branch. |
| `shimmer` | 0 | 0.25 | 0 | 0.08 | Bounded amplitude irregularity from the same sine-only source model. |
| `emphasis-partial` | 0 | 64 | 0 | 16 | Harmonic of F0 carrying a pinched resonance; `0` disables it. |
| `emphasis-gain` | 1 | 12 | 1 | 6 | Multiplier at the emphasised harmonic. |
| `emphasis-bandwidth` | 20 | 4000 | 20 | 400 | Width of the pinched resonance in Hz. |
| `subharmonic-mix` | 0 | 1 | 0 | 0.6 | Blend of the F0/2 pulse series; `0` disables it. |
| `subharmonic-partials` | 0 | 64 | 0 | 32 | Size of that sine series; `0` disables it. |

The last five parameters are the register levers from
[docs/registers.md](registers.md). They travel in an ordinary voice file, so a
register preset can be shared without a second file format; all six built-in
presets keep the neutral zero/off values and remain byte-identical.

The resonance response limits each formant band's total contribution at 2
with a C1 soft knee (it keeps slope 1 at the corner and saturates toward 2.25)
and floors bandwidth scaling at 0.7, so extreme `formant-scale` values change
timbre less than expected. Formant frequencies crossfade in log2 space, so a
1.5x move sounds like the same interval at F1 and at F4. The Nyquist taper in `Voice::taperCeiling` fades
the last 4% of the usable band before the hard harmonic break; leave it on
unless you are deliberately A/B testing that fade. `--list-voice-parameters`
prints the same table as machine-friendly tab-separated fields, so docs and
the loader cannot drift apart.

Useful starting points:

- **More presence**: raise `pitch` 10–20 Hz, then `breathiness` by 0.01.
- **Older voice**: lower `pitch`, add 0.02–0.05 `declination`, widen
  `formant-scale` slightly above 1.
- **Clearer consonants**: shorten `attack` and `release` toward 0.004 s and
  raise `gain` within the safe band.
- **Character voice**: combine a small `formant-scale` change with a
  `vibrato-depth` change; sweeping both at once is what `--sweep` is for.

## Parameter sweeps

`--sweep` accepts one or two `name=start:end:steps` axes separated by commas.
`steps` must be at least 2, both endpoints are included, and the total cell
count is capped at 10000. Every cell is rendered from the same phoneme
sequence, so differences come only from voice parameters.

```sh
build/formant --text "Hello, world." \
  --sweep "pitch=140:200:4,vibrato-depth=0:20:3" \
  --lab-dir lab --lab-index lab/index.json
```

Each cell is written as `<dir>/NNN-<label>.wav`, where `NNN` is a
zero-padded cell number and the label contains every parameter name and value
(for example `001-pitch-140p000000_vibrato-depth-0p000000.wav`). The index is
deterministic JSON mapping each file to its values:

```json
{
  "mode": "sweep",
  "sample-rate": 44100,
  "parameters": ["pitch", "vibrato-depth"],
  "cells": [
    {"file": "001-pitch-140p000000_vibrato-depth-0p000000.wav",
     "label": "pitch-140p000000_vibrato-depth-0p000000",
     "samples": 18522,
     "values": {"pitch": 140.000000, "vibrato-depth": 0.000000}}
  ]
}
```

`--lab-dir` defaults to `lab`; `--lab-index` defaults to `<lab-dir>/index.json`.
Lab mode writes multiple files, so it rejects `--output -`, `--timing` and
`--visemes` before rendering.

## A/B pairs

`--pair "A,B"` renders two voices on identical text and writes `a.wav`,
`b.wav` and the same style of index into `--lab-dir`. Each side can be:

- a built-in preset name, such as `neutral`;
- a voice file path, such as `voices/warm.voice`;
- a voice plus inline `param=value` assignments separated by `;`, such as
  `neutral:pitch=180;rate=1.1`.

Inline assignments use the same hard validation and safe-range warnings as
voice files. Because pair mode is self-contained, global `--voice` and numeric
flags do not leak into either side; specify them in each side if needed.

```sh
build/formant --text "Are you there?" \
  --pair "neutral:pitch=160,neutral:pitch=200" --lab-dir ab
```

## Determinism and integration

Voice files contain data only: no samples, random seeds, clocks or global
state. A sweep or pair render uses the same `Synthesizer` as a normal render,
which is why the index can safely record sample counts and why comparing two
files byte-for-byte is meaningful. The default path without these flags is
bit-for-bit unchanged from previous releases for oral phonemes; the nasal
anti-formants added in 0.28.0 deliberately change nasal renders.
