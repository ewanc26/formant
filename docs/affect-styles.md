# Affect styles and word emphasis

Formant's six built-in voices fix *who* speaks. The affect layer (#125) chooses
*how* a line is delivered without adding a second signal model. It maps three
continuous dimensions onto the synthesis levers the engine already owns, and it
marks individual words for local emphasis through SSML. Nothing is active by
default: a render without a style flag is byte-identical to earlier releases.

## Continuous dimensions

The axes are pleasure/arousal/dominance (PAD), each in `[-1, 1]` with `0` as the
neutral point:

| Axis | CLI flag | Low end | High end |
| --- | --- | --- | --- |
| arousal | `--arousal` | calm, slow, low energy | excited, fast, energetic |
| valence (pleasure) | `--valence` | negative, strained | positive, bright |
| dominance | `--dominance` | submissive, breathy | assertive, firm |

A named style is just a fixed point in that space. The shipped presets are:

| Style | Arousal | Valence | Dominance |
| --- | ---: | ---: | ---: |
| `neutral` | 0.00 | 0.00 | 0.00 |
| `calm` | -0.60 | 0.45 | -0.25 |
| `excited` | 0.80 | 0.70 | 0.50 |
| `sad` | -0.70 | -0.80 | -0.55 |
| `whispering` | -0.45 | 0.00 | -0.80 |
| `newsreader` | 0.05 | 0.10 | 0.35 |

Coordinates override a named style's axes, so `--style calm --arousal 0.5` is
allowed and resolves deterministically. With no name, coordinates start from
`neutral`.

## From axes to levers

`data/affect/affect_styles.txt` is the authoritative mapping. It is embedded in
the binary, installed under `share/formant/affect`, and layed over by
`--style-file`. For a coefficient row `map LEVER A V D`:

- scale levers evaluate as `1 + A*a + V*v + D*d`;
- perturbation levers evaluate as `A*a + V*v + D*d`, with their absolute value
  clamped when applied.

The mapping is affine, so the levers at the midpoint of two points are the
midpoints of their levers. This is what makes continuous coordinates audibly
interpolate between named presets rather than switching between them.

| Lever | Type | What it changes |
| --- | --- | --- |
| `pitch` | scale | `Voice::pitch`, the base F0 |
| `range` | scale | `Voice::declination`, the phrase-wide F0 drift |
| `rate` | scale | `Voice::rate`, speaking rate |
| `intensity` | scale | `Voice::gain` before peak normalisation |
| `jitter` | perturbation | `Voice::jitterCents`, bounded F0 irregularity |
| `shimmer` | perturbation | `Voice::shimmerDepth`, bounded amplitude irregularity |
| `adduction` | perturbation | breathiness and harmonic count |
| `effort` | perturbation | formant scale plus attack/release asymmetry |

Scale levers are clamped to `[0.5, 1.8]`; jitter to `0..50` cents, shimmer to
`0..0.25`, and adduction/effort to `[-1, 1]`. The resulting voice is then
clamped by the same hard intervals used by voice files, and the renderer's
existing peak normaliser keeps the output below full scale. The safe
neighbourhoods for hand sweeps are `0..12` cents for jitter and `0..0.08` for
shimmer.

`jitter` and `shimmer` are deterministic products of two incommensurate sines,
not PRNGs or clock state. Both branches are skipped entirely at zero, which is
why an unstyled voice still follows the original sample loop exactly.

## CLI usage

```sh
# Named preset or continuous coordinates
formant --text "I really like this." --style excited --output excited.wav
formant --text "I really like this." \
  --arousal 0.8 --valence 0.7 --dominance 0.5 --output coordinates.wav

# Inspect the shipped table, then share one preset as a versioned data file
formant --list-styles
formant --dump-style excited > excited.affect
formant --inspect-style excited.affect

# Layer a custom style or a whole replacement table over the built-ins
formant --style-file examples/affect/bright.affect --style bright \
  --text "Good morning." --output bright.wav
```

`--list-styles` prints the metadata, resolved levers for every preset and the
emphasis factors. `--dump-style NAME` emits a checksummed table containing the
common mapping, all emphasis levels and just that preset. `--inspect-style FILE`
reports schema, metadata, style/emphasis counts and checksum status without
rendering.

A style is applied to the selected `--voice` or `--voice-file` first. Numeric
voice flags (`--pitch`, `--rate`, `--gain`, `--jitter`, `--shimmer`, …) are
replayed afterwards, so they always remain the final authority. Affect/style
changes the Voice before G2P and is orthogonal to accents, which transform the
phoneme stream after every front end (see
[accent-profiles.md](accent-profiles.md)). SSML prosody, the neural layer,
symbolic intonation and exact `--prosody-file` targets then operate on the
phoneme sequence as before.

## Data-file format

An affect file is UTF-8 text, one directive per line. Blank lines and text after
`#` are comments. Fields are whitespace-separated and names are
case-insensitive where applicable.

```text
schema 1
name Bright
author Formant contributors
description A crisp, upbeat delivery
license AGPL-3.0-or-later
version 1.0.0
range -1 1
map pitch 0.16 0.03 0.04
emphasis strong 1.20 1.24 1.22
style bright 0.55 0.65 0.25
checksum <64 hexadecimal digits>
```

| Directive | Meaning |
| --- | --- |
| `schema N` | Required; only `1` is supported. |
| `name`, `author`, `description`, `license`, `version` | Optional metadata carried through `--dump-style` and `--inspect-style`. The rest of a line is the value. |
| `range -1 1` | Declares the axis interval; other intervals are rejected. |
| `map LEVER A V D` | Overrides one mapping row. `LEVER` is one of the eight rows above; coefficients must be in `[-4, 4]`. |
| `emphasis LEVEL P D A` | Overrides or adds an SSML emphasis level; each factor must be in `[0.5, 2]`. |
| `style NAME A V D` | Adds or replaces a named PAD preset. Each axis must be in `[-1, 1]`; a `neutral` preset is required. |
| `checksum HASH` | Optional SHA-256 integrity value. |

A user file layers over the built-in table line by line, so one new style or
one changed coefficient is a complete shareable file. Errors name the affected
line, for example `formant: affect line 21: map needs a lever and three
coefficients`. A checksum covers every byte of the file with checksum
directives removed and CRLF normalised to LF; comments and blank lines count.
`--dump-style` generates a valid checksum automatically.

## Word-level emphasis

`<emphasis level="strong|moderate|reduced">` wraps words in an SSML document.
It scales F0, duration and amplitude only on speech phonemes inside the span;
gaps, pauses, nonverbal events and surrounding words are untouched. Levels
compose multiplicatively with nested `<prosody>` spans and with the shipped
defaults, and a file can redefine any level.

```xml
<speak>
  I <emphasis level="strong">really</emphasis> like this.
  <prosody rate="1.1"><emphasis level="reduced">Maybe.</emphasis></prosody>
</speak>
```

Emphasized phonemes carry the provenance tag `style:emphasis`. The neural layer
skips tagged phonemes, so combining `--neural-apply` with emphasis cannot apply
the same change twice. An unsupported level fails before rendering with the
element's byte offset.

## Library API

`formant/affect.h` exposes the same data path:

```cpp
#include <formant/formant.h>

const formant::AffectData data = formant::parseAffectData(
    formant::builtinAffectData(), userOverlay);
const formant::AffectProfile profile =
    formant::resolveAffectProfile(data, "excited");

formant::Voice voice = formant::voicePreset("neutral");
formant::applyAffectStyle(voice, profile);

const formant::SsmlEmphasisLevels levels = profile.emphasis;
const auto phonemes = formant::phonemizeSsml(ssml, frontEnd, levels);
```

`writeAffectData()` serialises a versioned, checksummed file for sharing, and
`loadAffectData()` reads one with the built-in table as its fallback.
`affectFileChecksum()` is the pure hashing primitive used by the CLI inspector.
