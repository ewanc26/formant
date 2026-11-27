# Rhythm classes (#144)

Formant's renderer is still a deterministic stack of sine waves. This document
describes an **opt-in** rhythm layer that nudges phoneme durations toward
published rhythm-class target bands. It changes only the duration schedule that
the renderer already consumes: phoneme identity, formant bands, F0 and the
coarticulation crossfades are untouched, and no new sound source enters the
engine. With no rhythm flag selected, every render is byte-identical to earlier
releases.

## Context and research

Rhythm classes are a gradient way of describing how languages time their
vowels and the consonant spans between them. The shipped calibration uses the
Pairwise Variability Index (PVI) measurements reported by Grabe and Low (2002)
for the “North Wind and the Sun” passage, one speaker per language. Vocalic
intervals run from vowel onset to vowel offset; intervocalic intervals run from
vowel offset to vowel onset, excluding pauses and hesitations. The paper reports
normalised vocalic PVI (nPVI) and raw intervocalic PVI (rPVI), and describes the
classes as “weak categorical” rather than discrete boxes.

The generic class bands here are deliberately wider than any single speaker or
study, and the paper's intervocalic measure does not separate stress- and
syllable-timed languages as cleanly as its vocalic measure does. The Japanese
intervocalic band is intentionally higher than the syllable-timed band because
the paper attributes part of that value to devoiced vowels counted inside
intervocalic intervals; Formant does not model devoicing, so the calibration is
documented rather than silently copied as a promise about the synthesised
signal. The full measurement table, source citation and caveat live in
[`data/rhythm/PROVENANCE.md`](../data/rhythm/PROVENANCE.md).

These are acoustic target regions for a sine-only voice, not a reconstruction
of any language, speaker or tradition and not a claim about how a class sounds.

## Definitions and measurement

For consecutive vocalic interval durations `d[k]`:

```
nPVI = 100 * mean( abs(d[k] - d[k+1]) / ((d[k] + d[k+1]) / 2) )
```

For consecutive intervocalic interval durations `d[k]`, with durations in
milliseconds:

```
rPVI = mean( abs(d[k] - d[k+1]) )
```

Formant derives the intervals from the **exact scheduled phoneme durations
used for audio**, not from a waveform analysis: a vocalic interval is a run of
consecutive voiced vocalic phonemes, and an intervocalic interval is the
consonant span between two such runs. Silence, breath and other NVV events,
absolute-F0/prosody-direct spans and note-F0 singing segments are ineligible;
each one splits the intervals around it, so it cannot contribute a spurious
contrast. Fewer than two intervals leaves the corresponding index at zero.

`formant::measureRhythm()` returns both indices and the interval counts. It is
a pure function of a `Phoneme` vector, so the CLI status line, the verification
harness and embedders measure the same timeline. See
[docs/metrics.md](metrics.md#rhythm-pvi) for the metric definitions in context.

## Composition and behavior

`applyRhythm()` returns a copy of the phoneme vector. For each axis it compares
the measured index with the profile's target band and, if the index is outside
it, repeatedly scales individual interval durations toward the band. Changes
are multiplicative, bounded by each profile's `MIN-FACTOR`/`MAX-FACTOR`, and
capped by `strength` and a bounded per-iteration gain; a profile with strength
zero is a true no-op. The same input and profile always produce the same output,
with no clock, random seed or global state.

Only the `duration` field changes. Segment kinds, formants, anti-formants,
voicing, F0 fields, amplitude and source tags stay as authored, and the shared
renderer still handles the 35 ms formant crossfades. Identity and coarticulation
are therefore preserved by construction: there is no second oscillator, filter
or post-processing step. The pass runs after all text front ends, language-file
defaults and breath planning, but before the neural, intonation and
prosody-direct layers, so exact author contours still win. Song mode owns its
F0 and note-F0 segments are exempt, so combining rhythm with `--song` or
`--song-file` is rejected.

An accent profile can declare a `rhythm` convention, but it is auto-selected
only when no explicit `--rhythm` or `--rhythm-file` was given; an explicit
selection always wins (see [accent-profiles.md](accent-profiles.md)).

## Shipped data

`data/rhythm/rhythm_profiles.txt` is embedded at CMake configure time and
installed under `share/formant/rhythm`. Version 1 looks like this (shortened):

```
schema 1
name formant-rhythm
author Formant contributors
description Published rhythm-class targets for stress, syllable and mora timing
license AGPL-3.0-or-later
version 1.0.0

rhythm stress stress 52 66 50 65 1.0 0.6 1.8
about stress Gradient stress-timing: high vocalic and intervocalic variability
rhythm syllable syllable 28 44 46 58 1.0 0.6 1.8
about syllable Gradient syllable-timing: more even vocalic intervals
rhythm mora mora 38 44 56 66 0.8 0.7 1.6
about mora Mora-timing: even vocalic intervals with longer medial consonant spans
rhythm en stress 52 62 58 68 1.0 0.6 1.8
rhythm es syllable 26 34 50 62 1.0 0.6 1.8

checksum <sha256>
```

A `rhythm` row is `NAME CLASS V-NPVI-MIN V-NPVI-MAX I-RPVI-MIN I-RPVI-MAX
STRENGTH MIN-FACTOR MAX-FACTOR`. nPVI is dimensionless (×100) and rPVI is in
milliseconds; `CLASS` is `stress`, `syllable`, `mora` or `unspecified`. An
`about` row adds a note to a previously declared profile. Bounds are validated
while parsing (vocalic nPVI positive and at most 200, intervocalic rPVI
non-negative and at most 500 ms, ordered bands, strength in `[0, 1]`, minimum
factor in `(0, 1]` and maximum factor in `[1, 4]`); bad rows fail with a
`rhythm line N: ...` message.

| Profile | Class | Vocalic nPVI | Intervocalic rPVI | Strength | Duration factors |
|---------|-------|--------------|-------------------|----------|------------------|
| `stress` | stress | 52–66 | 50–65 ms | 1.0 | 0.6–1.8 |
| `syllable` | syllable | 28–44 | 46–58 ms | 1.0 | 0.6–1.8 |
| `mora` | mora | 38–44 | 56–66 ms | 0.8 | 0.7–1.6 |
| `en` | stress | 52–62 | 58–68 ms | 1.0 | 0.6–1.8 |
| `es` | syllable | 26–34 | 50–62 ms | 1.0 | 0.6–1.8 |

The `en` and `es` rows are the built-in language calibrations from the same
source; the other three are broader generic classes.

The checksum follows the project convention: SHA-256 of the file with the
`checksum` directive removed and CRLF line endings normalised to LF; comments
and blank lines count. A user table layers individual profiles over the built-in
table, so a shared file can retune one name without replacing the others.
Checksums belong to one file, not to the merged table. Editing the shipped data
requires rerunning `cmake -S . -B build` to refresh the embedded copy.

## CLI

```sh
# Inspect the shipped table and export a shareable single-profile file.
formant --list-rhythms
formant --inspect-rhythm data/rhythm/rhythm_profiles.txt
formant --dump-rhythm mora > mora.rhythm

# Select a built-in (or layered) profile by name.
formant --text "The quick brown fox." --rhythm stress --output stress.wav
formant --phonemes "[ee s ee]" --rhythm syllable --output syllable.wav

# Built-in --lang en/es can resolve their published calibrations via auto.
formant --lang en --text "A stress-timed line." --rhythm auto --output en.wav
formant --lang es --text "Una línea." --rhythm auto --output es.wav

# Apply a shared single-profile file directly.
formant --text "A line." --rhythm-file mora.rhythm --output shared.wav

# Retune a built-in name in a shareable file and apply it directly.
formant --text "A line." --rhythm-file retuned.rhythm --output retuned.wav

# Layer a user table before inspecting or exporting a built-in name.
formant --rhythm-file retuned.rhythm --dump-rhythm mora
```

`--rhythm NAME` accepts `stress`, `syllable`, `mora`, `en`, `es` and `auto`,
plus any name supplied by a layered table. Use **one** of `--rhythm NAME` and
`--rhythm-file FILE` for a render; asking for both fails with
`use only one of --rhythm and --rhythm-file`. A `--rhythm-file` must define
exactly one profile, which is what `--dump-rhythm NAME` emits; unknown names,
malformed rows, multi-profile files and checksum mismatches are hard errors
before output is created. A render prints the measured before/after indices on
its status line. Song mode rejects rhythm combinations.

`--rhythm auto` resolves in this order: numeric dials from a language file,
then a tag-specific built-in calibration (`en` or `es`), then the generic class
profile named by the language file's `rhythm` directive. Without any of those it
fails with a message pointing at the language-file dials or `--rhythm NAME`.

## Language files

A language definition file can carry the same numeric dials, so a conlang keeps
its rhythm calibration beside its phonology. The existing
`rhythm stress|syllable|mora` directive remains compatible:

```text
name Velta
tag x-velta
rhythm syllable
rhythm-vocalic-npvi 34 44
rhythm-intervocalic-rpvi 50 60
rhythm-strength 0.8
```

```sh
formant --lang-file my.lang --rhythm auto --text "Velta tata." --output velta.wav
```

The dials are validated by the language-file parser but stay inert until
`--rhythm auto` (or the library's `resolveRhythmProfile()`) selects them. See
[docs/language-files.md](language-files.md#typology-defaults) for the grammar.

## Library API

`formant/formant.h` includes `formant/rhythm.h`, which exposes
`RhythmProfile`, `RhythmData`, `builtinRhythmData()`, `rhythmFileChecksum()`,
`parseRhythmData()`, `loadRhythmData()`, `writeRhythmData()`,
`findRhythmProfile()`, `rhythmProfileNames()`, `resolveRhythmProfile()` and
`applyRhythm()`. `formant/metrics.h` adds `RhythmMetrics` and `measureRhythm()`.
Profiles are plain data; embedders can inspect, retune and apply them without
changing the renderer.

## Verification

The verification harness prints measured before/after PVI values for each
built-in class and checks that the adjusted values land inside the profile's
target bands, that durations stay finite and positive, and that repeated renders
are byte-identical. Unit tests cover data parsing, checksum tampering, layering,
conlang dial resolution, PVI movement in both directions, determinism,
exemptions and continued-render stability. Property tests add rhythm to the
malformed-input corpus. CLI smoke checks list/dump/inspect/file commands and
combination errors; the checksum corpus gains one additive
opt-in case, while default renders stay unchanged.

These are objective checks on the duration schedule, not a listening test.
Phoneme identity and coarticulation are maintained because only durations
change and the shared renderer is unchanged.

## Limits

- PVI is computed from the phoneme schedule, not estimated from audio; timing
  exports and audio use the same durations, but a waveform analyzer could report
  different boundaries.
- Class bands are target regions from a small published corpus, not hard
  linguistic categories; rPVI in particular does not separate all classes.
- Formant does not model Japanese devoiced vowels, so that band's provenance is
  documented rather than guaranteed in the output.
- The pass cannot shape spans with absolute F0, note F0, silence, breath/NVV
  events or singing mode; those are exempt or rejected.
- A language file's rhythm metadata does nothing without an explicit profile or
  `--rhythm auto`, and no flag means no change at all.
