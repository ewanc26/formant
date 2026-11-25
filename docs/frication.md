# Frication quality (#101)

Formant's unvoiced sounds are still sine waves: there are no samples, noise
buffers, filters with feedback, random generators from time, or other
non-determinism. Issue #101 improves the *shape* of that sine stack without
changing the source model. Everything in this document is **opt-in**: a render
without `--frication` or `--frication-file` produces exactly the same bytes as
before 0.36.0.

## Why the fixed stack was the weak point

Sine-wave speech research (Remez et al., 1981) shows that sonorants survive a
small number of sine tones but frication cues are the first casualty, because
`/s/`, `/ʃ/` and `/f/` are distinguished largely by *broadband spectral shape*
rather than by a pitch. A single fixed stack of twelve detuned sines shaped only
by four vowels' formants cannot express that place contrast well. Perceptual
work on sine-wave vowels also shows that listeners rely on trajectory dynamics,
so the envelope must *evolve*, not just sit at a fixed peak.

The fix stays inside the sine-only constraint:

1. The deterministic seeded partial stack can hold tens of partials (up to 128).
   More detuned sines approximate a broadband spectrum without becoming noise.
2. Each fricative/affricate gets a place-specific spectral envelope: a centre
   frequency and bandwidth, with a separate attack centre/bandwidth.
3. The envelope glides from its attack shape to its steady shape over the
   segment. The existing 35 ms formant crossfade already lets the following
   vowel's formants blend into the tail.
4. A small broadband floor keeps the stack diffuse where a real fricative is
   flat (`f`, `θ`) rather than sharply peaked (`s`, `ʃ`).

One renderer phase accumulator still carries every transition, and peak
normalisation is unchanged. The render is `O(partial count)` and byte-identical
for identical input.

## Data format

The shipped table is `data/frication/frication_profiles.txt`, embedded at CMake
configure time and installed under `share/formant/frication`. It is version 1:

```
schema 1
name formant-builtin
author Formant contributors
description Per-place frication envelopes for the unvoiced sine stack
license AGPL-3.0-or-later
version 1.0.0

defaults 48 0.02 0.012

profile s 64 5400 2400 4400 3200
profile sh 56 2900 1400 2300 1800
profile f 48 4600 6200 4600 6200

checksum <sha256>
```

`defaults` is `PARTIALS BROADBAND-FLOOR ATTACK-SECONDS`. A `profile` row is
`KEY PARTIALS CENTRE BANDWIDTH ATTACK-CENTRE ATTACK-BANDWIDTH`; all six
frequencies/bandwidths are Hz. `KEY` is the exact phoneme short name: ASCII
(`s`, `sh`, `th`, `ch`) or UTF-8 IPA (`ʃ`, `θ`, `ɣ`). A wide bandwidth means a
flat, diffuse fricative; a narrow one means a concentrated spectral peak.

The checksum follows the project convention: SHA-256 of the file with the
`checksum` directive removed and CRLF normalised to LF; comments and blank
lines count. A user file layers individual profiles over the built-ins, so a
shared table can retune one sound without replacing the rest.

## CLI

```sh
# List, inspect and export the versioned data.
formant --list-frication
formant --inspect-frication data/frication/frication_profiles.txt
formant --dump-frication s > my-s.frication

# Apply one built-in profile to its matching phoneme(s).
formant --phonemes "[s ee]" --frication s --output s.wav

# Apply every profile in the built-in table plus a user overlay.
formant --phonemes "[sh ee]" --frication-file my-s.frication --output sh.wav
```

`--frication` and `--frication-file` are mutually exclusive. `--frication NAME`
selects exactly one profile, so unrelated fricatives keep the legacy stack;
`--frication-file` activates the full table after layering the user rows over
the embedded built-ins. Unknown names, malformed rows and checksum mismatches
are hard errors with instructive messages. Changing the data file requires
rerunning `cmake -S . -B build` so the embedded built-in table is refreshed.

## Library

`formant/frication.h` exposes `builtinFricationData()`,
`fricationFileChecksum()`, `parseFricationData()`, `loadFricationData()`,
`writeFricationData()`, `findFricationProfile()`, `fricationProfileNames()` and
`applyFrication()`. The last attaches an optional `Phoneme::frication` control;
the renderer only builds a larger partial stack when at least one control is
present. An unmodified `Phoneme` therefore takes the exact legacy path.

## Verification

`--phonemes "[s ee]"`, `[sh ee]` and `[f ee]` with the data layer active render
to three different byte streams at equal gain, and repeated renders match
bit-for-bit. The unit tests cover the parser, checksum convention, layering,
profile lookup and renderer determinism; property tests add the new file kind
to malformed-input hardening; CLI smoke tests exercise list/inspect/dump and
conflict rejection; the checksum corpus has one additive `frication` case while
every pre-existing case stays unchanged.
