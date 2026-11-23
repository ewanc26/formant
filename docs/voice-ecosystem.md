# Voice ecosystem

Formant voices are plain text, so sharing one is a matter of copying a file. The
schema 2 **voice package** format adds what a shared voice needs to be useful:
attribution, a content checksum, and an explicit format version. No rebuild or
code change is needed to install one.

```sh
# Install: put the file anywhere and point the engine at it.
formant --text "Hello." --voice-file voices/warm-narrator.voice --output hello.wav

# Inspect what arrived, including integrity status, without rendering.
formant --inspect-voice voices/warm-narrator.voice

# Create a package from a built-in preset or an existing definition file.
formant --package-voice alto \
  --voice-author "Your Name" \
  --voice-description "A warm alto for narration" \
  --voice-license CC0-1.0 \
  --voice-version 1.0.0 > voices/my-alto.voice

# When packaging a draft file, give the voice a readable published name.
formant --package-voice draft.voice --voice-name "Warm Narrator" \
  --voice-author "Your Name" --voice-description "Long-form narration" \
  --voice-license CC0-1.0 --voice-version 1.0.0 > voices/warm-narrator.voice
```

## Format

A package is the schema 1 definition from [voice-files.md](voice-files.md) plus
four required attribution fields and a checksum:

```
# Formant voice package
schema 2
name Warm Narrator
author Ewan Clarke
description A warm, unhurried narrator for long-form reading
license CC0-1.0
version 1.0.0
base alto
pitch 185
...
taper-ceiling true
checksum 55c49e...
```

All five fields (`name`, `author`, `description`, `license`, `version`) must be
non-empty, and a `checksum` line is required. Everything after `#` on a line is
a comment. CRLF and LF line endings are accepted; they hash identically.

### Integrity

The checksum is SHA-256 over the whole file **except** the `checksum` line,
with CRLF normalised to LF and every line emitted with a trailing LF. Verifying
it is a standard-library operation in the loader: a mismatch is rejected before
any audio is written, with an error naming both the stored and computed hashes.

`--inspect-voice` parses with verification disabled so it can report a bad
package rather than refusing to look at it. It prints one `key<TAB>value` pair
per line: `schema`, `name`, `base`, the metadata fields, `parameters` (the
number of numeric overrides), `checksum`, `computed` and `status`. `status` is
`valid`, `invalid` or `unsigned`; schema 1 definitions without a checksum are
`unsigned` and still load normally.

## Examples gallery

[`examples/voices/`](../examples/voices) ships eight packages:

- `neutral`, `alto`, `bass`, `child`, `robot`, `whisper` — the built-in presets
  round-tripped as data. Each renders byte-identically to its compiled preset;
  CLI smoke tests check this for every voice.
- `warm-narrator`, `bright-robot` — original sample voices showing how a small
  set of parameters layers over a preset.

The gallery doubles as a regression suite: the loader parses and verifies every
file, and the preset packages are compared against the compiled voices on every
test run.

## Publishing and licensing

1. Start from a preset (`--package-voice alto ...`) or a schema 1 draft file
   listing the parameters you want to change, then package it.
2. Give the voice a distinct `name`, a real `author`, a one-line `description`,
   a `license` and a `version`. Bump `version` whenever you republish edited
   audio, even if the schema stays at 2.
3. Run `formant --inspect-voice your-voice.voice` and confirm `status: valid`
   before sharing.
4. Share the single `.voice` file. Recipients install it by pointing
   `--voice-file` at it; there is no index or registration step.

Licensing matters because Formant itself is AGPL-3.0. A voice package is data,
so its `license` field describes the voice, not the engine. Packages generated
from a built-in preset default to `AGPL-3.0-or-later` because they embed that
preset's parameters; independently authored settings can use any license you
choose (the samples use `CC0-1.0`). Whatever you choose, state it in the file
and in the place you publish it. A checksum proves the file was not corrupted
or tampered with in transit; it is not a signature and does not prove who
wrote it.
