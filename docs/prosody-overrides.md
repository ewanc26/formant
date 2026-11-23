# Prosody-direct input

Formant normally derives F0 and timing from the text front end, IPA tone marks and the voice preset. A prosody override file lets you specify those values directly, which is useful for dubbing, animation, music and research. The file is layered over the default prosody in the **parameter domain**: the same phoneme sequence, scheduler and sine renderer run as before, so rendering stays deterministic and byte-identical across platforms. No file means no change at all.

## Enabling it

```sh
formant --text "Hello, world." --prosody-file script.prosody --output out.wav
```

`--prosody-file` composes with every input mode (`--text`, `--stdin`, SSML, IPA, X-SAMPA and bracketed phonemes). It is applied **after** the front end (including SSML spans) and **before** the scheduler, so the WAV, timing exports and visemes all describe the same overridden phonemes.

## Format

A line-oriented UTF-8 text file. Blank lines and everything after `#` are comments. Each directive has a kind, a selector and one or two values:

```
# retune one word and lengthen the pause after it
f0 3 180
duration 4 0.35s
amplitude 3 0.8
```

### Selectors

| Selector | Meaning |
| --- | --- |
| `0`, `12` | Zero-based phoneme index in the sequence the front end produced. |
| `@0.25s`, `@250ms`, `@1.5` | The audible segment containing that time, measured in **score seconds** (the sum of `Phoneme::duration` values, before `Voice::rate`). |

Segment indices are the same ones used by `--timing` exports, so a timing JSON makes a convenient map for authoring a file.

### Directives

| Directive | Values | Effect |
| --- | --- | --- |
| `f0` | `180`, `180Hz`, `+200c`, `+2st`, `+10%` | Absolute Hz target, or a relative multiplier on the front-end contour. One value is a flat target; two values make a linear ramp across the segment. |
| `duration` | `0.25s`, `250ms`, `1.5`, `1.5x`, `+50%` | An absolute duration in seconds, or a multiplier of the front-end duration. |
| `amplitude` | `0.8`, `+20%` | A non-negative multiplier on the segment's amplitude. |

Absolute F0 (`180`) replaces the voice-relative contour and declination for that segment only. The phrase-final rise (`?`) and vibrato still apply on top, because they are voice-level effects. Relative F0 (`+200c`, `+2st`, `+10%`) instead multiplies `pitchStart` and `pitchEnd`, so it keeps the language profile's intonation and composes with SSML spans. Mixing absolute and relative values in one directive is an error.

## Partial overrides and precedence

Every value is optional. A file that sets one F0 point leaves every other segment, and every other field, exactly as the front end produced it. With one exception noted below, later directives override earlier ones for the same field.

Layers apply in this order, last winning:

1. Voice preset and numeric flags (`--pitch`, `--rate`, `--gain`, …) set the base synthesis parameters.
2. The language profile, IPA tone marks and affect-free front end set each phoneme's default F0, duration and amplitude.
3. SSML `<prosody>` spans scale the phonemes of their span.
4. The prosody file is applied last, so it can retune a single word inside a longer sentence without touching its neighbours.

Within the file there are two phases, which keeps time selectors independent of directive order:

1. **Duration** directives are resolved against the incoming timeline and applied first.
2. **F0 and amplitude** directives are resolved against the resulting timeline, so `@1.2s` names the segment that is actually at 1.2 s in the finished utterance.

## Errors

Malformed lines are rejected before rendering and report the 1-based line number:

```
formant: prosody line 3: invalid F0 in Hz 'loud'
```

Out-of-range segment indices and times past the end of the utterance are reported when the file is applied, since the parser does not know how many segments the text will produce. F0 must be positive and at most 20 kHz; durations must be positive and at most one hour; amplitudes must not be negative.

## Library API

Embedders can use `formant/prosody_file.h`:

```cpp
formant::ProsodyFile file = formant::loadProsodyFile("script.prosody");
formant::applyProsodyFile(phonemes, file);
```

`parseProsodyFile(std::string_view)` parses text from memory. Both return pure values with no global state. Because the overrides are stored directly on `Phoneme`, the phase-continuation contract still holds: a chain of `renderContinued()` calls is byte-identical to one render of the concatenated, overridden sequence.
