# Singing mode

Singing mode (#123) quantises F0 to musical notes and adds deliberate note-to-note transitions and delayed vibrato. It is **opt-in**: a render with no `--song`/`--song-file` constructs none of these fields, so default speech stays byte-identical and the sine-only, dependency-free renderer is unchanged.

Everything musical is parameter-domain data. A song resolves to the same `formant::Phoneme` vector every other front end produces, with absolute-Hz F0 targets on each segment; the renderer's single glottal phase accumulator carries the waveform through every note boundary, so legato joins have no phase discontinuity.

## Quick start

```sh
# Hear the built-in demo ("Twinkle, Twinkle, Little Star")
build/formant --song twinkle --output twinkle.wav

# Retune A4 without editing the file
build/formant --song twinkle --a4 432 --output twinkle-432.wav

# Sing a shared .song file on the built-in quarter-tone scale
build/formant --song-file my.song --scale quarter --output quarter.wav

# Sing on a custom scale table
build/formant --song-file my.song --scale-file my.scl --output custom.wav

# Inspect the data before trusting it
build/formant --list-songs
build/formant --list-scales
build/formant --dump-song twinkle > twinkle.song
build/formant --dump-scale quarter > quarter.scl
build/formant --inspect-song twinkle.song
build/formant --inspect-scale quarter.scl
```

`--a4` accepts 200–1000 Hz and overrides the song's own `a4`. `--scale` selects a built-in scale by name; the song's own `scale` directive is used when neither `--scale` nor `--scale-file` is given. Song mode cannot be combined with text, phoneme, IPA, X-SAMPA, SSML, gibberish or language inputs, and its F0 is authoritative: it rejects `--intonation-*`, `--prosody-file`, `--neural-apply` and `--breathing` rather than letting those layers fight the notes. Use only one of `--song`/`--song-file` and one of `--scale`/`--scale-file`.

## Song files (`.song`)

A song is a versioned, line-oriented text document. `#` starts a comment only at the start of a token, so degree names such as `C#` are legal.

```
schema 1
name Twinkle, Twinkle, Little Star
a4 440
scale chromatic
note C4 0.42 ah transition=legato glide=0.006s vibrato=5.5:10c:0.18s:0.22s
note G4 0.42 ah transition=portamento glide=0.180s vibrato=5.5:12c:0.20s:0.25s
note G4 0.68 ah transition=portamento glide=0.200s overshoot=18c
note C4 0.78 ah transition=portamento glide=0.190s preparation=-30c
checksum 6f89...
```

Directives:

| Directive | Meaning |
|---|---|
| `schema 1` | Document version (required, must be 1). |
| `name <text>` | Human-readable title. |
| `a4 <Hz>` | Reference pitch for the note names (200–1000 Hz). |
| `scale <name>` | Built-in scale to resolve names against (`chromatic` by default). |
| `note <pitch> <seconds> <phonemes> [key=value ...]` | One note. Phonemes are a single name or `-`-separated sequence, scaled to fill the duration. |
| `checksum <sha256>` | Integrity digest; verified on load. |

Pitch spellings, all optionally followed by a signed cents offset (for example `C4+15`, `midi:69-7c`):

| Form | Example | Resolution |
|---|---|---|
| Scale degree and octave | `C4`, `Aq4`, `B#q4` | Degree cents in the scale, rebased so `A4` is the reference pitch. |
| Named MIDI note | `midi:69` | 100 cents per semitone from A4 = 69. |
| Bare MIDI number | `69` | Same as `midi:69`. |
| Absolute cents from A4 | `cents:1200` | `a4 * exp2(cents/1200)`. |
| Scale degree index | `degree:18@4` | The 19th degree of the loaded scale in octave 4. |

Note modifiers:

| Key | Default | Meaning |
|---|---|---|
| `transition` | `legato` | `legato` joins at a near-instant knee; `portamento` glides. |
| `glide` | `0.012s` (`0.090s` for portamento) | Glide duration, capped at 45% of the note. |
| `overshoot` | `0c` | Cents past the target before settling (transition model). |
| `preparation` | `0c` | Drift toward the next note at the tail (signed toward it). |
| `vibrato` | none | `rate:extent:delay:fade`, e.g. `5.5:10c:0.18s:0.22s`. |

Vibrato is exactly zero until `delay` seconds after note onset, then fades in over `fade` seconds with a smoothstep envelope and a single bounded sine. This is what keeps attacks clean: there is no pre-delay modulation. Global voice vibrato and jitter are skipped on note segments, so the note contour is the only F0 authority. Rate and extent are bounded at render time (0–20 Hz, 0–200 cents); the parser rejects values outside the documented safe ranges.

## Scale files (`.scl`)

Non-12-tone tunings are data, not synthesis code:

```
schema 1
name quarter
period 1200
reference 900
degree C 0
degree Cq 50
degree C# 100
degree Aq 850
degree A 900
checksum 182c81...
```

`period` is the octave size in cents (any positive value); `reference` is the position of A4 inside it (900 for a 1200-cent octave). `degree [NAME] <cents>` entries run from 0 up to, but not including, `period`. A degree may be unnamed and addressed with `degree:N`. Names are matched longest-first and case-insensitively, so `C#` and `B#q` both parse. Two scales ship built in: `chromatic` (12 tones) and `quarter` (24 tones).

## Checksums

Both formats use the same convention as voice and affect packages: the checksum is the SHA-256 of the file with the `checksum` directive removed and CRLF line endings normalised to LF. Comments and blank lines are part of the versioned document and therefore count. `--dump-song`/`--dump-scale` emit a correct checksum; `--inspect-song`/`--inspect-scale` verify one without rendering.

## Library API

`formant/song.h` exposes the same model the CLI uses:

```cpp
#include <formant/song.h>

const formant::SongFile song =
    formant::parseSongFile(formant::builtinSongData("twinkle"));
const formant::SongScale scale =
    formant::parseSongScale(formant::builtinScaleData("chromatic"));
const auto phonemes =
    formant::phonemizeSong(song, scale, 432.0);  // optional A4 override
const auto samples = formant::synthesizePhonemes(
    phonemes, formant::voicePreset("neutral"), 44100);
```

`parseSongFile`, `loadSongFile`, `parseSongScale` and `loadSongScale` verify checksums by default; pass `false` only for diagnostic inspection. `writeSongFile` and `writeSongScale` serialise deterministically with a trailing checksum. `songVibratoCents()` is shared by the renderer and tests, so the delayed envelope is measured exactly where it is applied.

## Design and verification notes

The built-in song and scales are embedded at CMake configure time and installed under `share/formant`. Editing a data file therefore requires re-running `cmake -S . -B build` before rebuilding; use `--song-file`/`--scale-file` to load edits without a rebuild.

Unit tests cover pitch resolution (including non-12-tone and MIDI/cents forms), delayed vibrato gating, legato/portamento continuity, checksum rejection and round-trip serialisation. Property tests register both parsers for malformed-input hardening, the fuzz campaign renders generated songs, and the checksum corpus holds a canonical `song` rendering. Because the renderer's deterministic `exp2` kernel is used for pitch resolution, quarter-tone frequencies match that kernel (for example `Aq4` ≈ 427.474 Hz rather than an exact 427.633 Hz) — the same approximation budget documented in `docs/design.md`.
