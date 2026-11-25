# Overtone and subharmonic registers (#140)

Formant's source is still a deterministic stack of sine waves. This document
describes an **opt-in** register layer that uses two ordinary synthesis levers
more aggressively: a very narrow resonance that emphasizes one harmonic, and a
second pulse series at half the fundamental. With neither lever set, every
render is byte-identical to earlier releases. No samples, noise buffers,
clocks, random generators or runtime dependencies enter the engine.

## Context and framing

Overtone singing appears in several traditions. Mongolian and Tuvan practices
include sygyt, a high whistled melody carried by one strongly selected harmonic
over a sustained drone, and kargyraa, a low diplophonic register with a distinct
voice source often described as ventricular participation. The methods here are
**acoustic simulations of arguably relevant mechanisms**, not reconstructions
of a cultural performance and not caricatures. The names are used with respect
for the traditions that developed them; the presets are deliberately small,
editable data files rather than claims of authenticity.

The source-model distinction matters. Studies of Mongolian throat singing using
high-speed imaging, acoustic spectra and inverse filtering report that kargyraa
has voice-source characteristics clearly distinct from modal voice in the same
subject, consistent with an additional ventricular oscillation ([voice source
characteristics in Mongolian throat singing](https://www.sciencedirect.com/science/article/pii/S089219970100008X);
[observation of laryngeal movements for throat singing](https://pubs.aip.org/asa/jasa/article/112/5_Supplement/2264/541364/Observation-of-the-laryngeal-movements-for-throat)).
The higher whistled effect is commonly described as a pinched vocal-tract
resonance amplifying one harmonic of a buzzy source. Both are already in
Formant's parameter space: a Lorentzian formant peak with an unusually small
bandwidth, and another deterministic sine series with its own phase.

The data labels follow the Voice Quality Symbols chart: `V` marks the modal
voice used for the overtone-focused realization, because VoQS has no dedicated
overtone symbol, and `V‼` is the chart's ventricular-phonation label
([Voice Quality Symbols](https://en.wikipedia.org/wiki/Voice_Quality_Symbols)).
They describe the simulated source, not a person or a tradition.

## What the renderer does

For a voiced segment with fundamental `F0`, the normal source is a stack of
sines at `h * F0`. A register preset can add one pinched resonance around
partial `P`:

```
amplification(h) = 1 + (gain - 1) / (1 + ((h * F0 - P * F0) / bandwidthHz)^2)
```

The selected partial receives the full gain; neighbours fall off with a
Lorentzian skirt set by `bandwidthHz`. A narrow 20 Hz bandwidth at 100 Hz F0
makes harmonic 6 several times stronger than its neighbours while preserving
phase continuity, because the multiplier changes amplitude only and never
resets a phase accumulator.

The subharmonic lever adds a second series at `F0/2`:

```
subharmonic(t) = Σ(k = 1..N) response(k * F0/2) / k * sin(k * phase(F0/2))
voiced += mix * subharmonic(t)
```

It is blended, not summed at full level, and uses the same shared sine kernel
and ceiling taper as the normal source. Its odd multiples (`F0/2`, `3F0/2`,
`5F0/2`, ...) place energy where the ordinary harmonic series has none, which
is the measured signature of the kargyraa-style preset. A dedicated
`RenderState::subPhase` carries the half-rate phase across
`Synthesizer::renderContinued()` calls, so splitting a rendering session never
clicks or changes a sample.

Both branches are skipped at their neutral values (`emphasis-partial 0` and
`subharmonic-mix 0`). The existing peak normalisation and 35 ms formant
crossfade are unchanged.

## Shipped data

`data/registers/register_presets.txt` is embedded at configure time and
installed under `share/formant/registers`. Version 1 looks like this:

```
schema 1
name formant-builtin
author Formant contributors
description Overtone and subharmonic register presets for the sine source
license AGPL-3.0-or-later
version 1.0.0

register sygyt 6 12 20 0 0
register kargyraa 0 1 120 0.55 28
voqs sygyt V
voqs kargyraa V‼
about sygyt Whistled overtone melody carried by one emphasised harmonic over a drone
about kargyraa Deep ventricular/diplophonic register with a subharmonic pulse series

checksum 99bcb30b17d81fc2cb80e2270eecb5b3edabc807be63f195efebc6654fea499d
```

A `register` row is `NAME PARTIAL GAIN BANDWIDTH-HZ SUBHARMONIC-MIX
SUBHARMONIC-PARTIALS`. `PARTIAL 0` disables the pinched resonance and
`SUBHARMONIC-MIX 0` disables the second series. All values are bounded
(`PARTIAL` and `SUBHARMONIC-PARTIALS` are integers in `[0, 64]`, gain `[1, 12]`,
bandwidth `[20, 4000]` Hz and mix `[0, 1]`). `voqs` and `about` add labels and
short notes to a previously declared register. Unknown names and malformed
rows fail with a `register line N: ...` message.

The checksum follows the project convention: SHA-256 of the file with the
`checksum` directive removed and CRLF normalized to LF; comments and blank
lines count. A user file layers individual presets over the built-in table, so
a shared table can retune one register without replacing the rest. Changing the
data file requires rerunning `cmake -S . -B build` to refresh the embedded
copy.

## CLI

```sh
# Inspect the shipped table and export a shareable single-preset file.
formant --list-registers
formant --inspect-register data/registers/register_presets.txt
formant --dump-register sygyt > sygyt.register

# Select a built-in (or layered) preset by name.
formant --text "A sustained note." --register sygyt --output sygyt.wav
formant --phonemes "[ee]" --register kargyraa --output kargyraa.wav

# Apply a shared preset file directly. A file must define exactly one preset;
# --dump-register emits this shape.
formant --phonemes "[ee]" --register-file sygyt.register --output shared.wav

# Retune a built-in name in a shareable file and apply it directly.
formant --phonemes "[ee]" --register-file retuned.register --output retuned.wav

# Layer a user table before inspecting or exporting a built-in name.
formant --register-file retuned.register --dump-register sygyt
```

Use **one** of `--register NAME` and `--register-file FILE` for an audio
render; asking for both is an error. `--register NAME` reads the built-in table
with any user overlay applied to dump/inspect commands, while a single-preset
`--register-file` applies that file directly because its rows must describe
exactly one preset. Unknown names, bad checksums and multi-preset files are
hard errors before any output is created.

The five numeric levers are also ordinary voice parameters, so a register can
be shared as a voice file instead of a register table:

```sh
formant --list-voice-parameters | grep emphasis
formant --voice-file examples/warm.voice --register sygyt --output shared.wav
```

A voice file that sets `emphasis-partial`, `emphasis-gain`,
`emphasis-bandwidth`, `subharmonic-mix` and `subharmonic-partials` produces the
same synthesis levers without any new parser. `--register` is applied after
voice overrides because it is an explicit mode selection.

## Composition and verification

Registers are voice levers, not a front end: they compose with text, IPA,
X-SAMPA, phonemes, SSML and the singing mode. A sung drone with an emphasized
harmonic remains one continuous source, with the note resolver supplying F0
targets and the register shaping the same harmonics. Determinism is exact: the
same voice, phonemes and register produce identical bytes, and continued
renders match a single render byte-for-byte including `subPhase`.

The verification harness measures the effect rather than trusting the flags:

- at F0 100 Hz, the `sygyt` preset makes harmonic 6 at least 2x stronger than
  its immediate neighbours and than the same harmonic without a register;
- the `kargyraa` preset makes the 50/150/250/350 Hz F0/2 series at least 2x
  stronger than the plain glottal source at the same frequencies;
- unit tests check data parsing, checksums, layering, neutral byte identity,
  determinism, phase continuity and singing composition;
- the CLI smoke test checks list/inspect/dump, tampering, mutual exclusion,
  shareable files, retuning and determinism;
- `tests/checksum_corpus.txt` gains one additive opt-in case. Default speech,
  the six presets, timing exports and neural prosody are unchanged.

These are acoustic checks, not perceptual claims. The presets are starting
points for exploration; the documentation and data are separable precisely so
they can be revised without changing the engine.
