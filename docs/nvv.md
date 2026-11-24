# Nonverbal vocalizations (#124)

Laughter, sighs, breaths and throat clears are first-class, data-defined units. An NVV is just a short list of ordinary `Phoneme` values with an explicit duration and F0 contour, so it travels through the same deterministic sine engine as speech — there is no samples, noise buffer or per-event synthesis code.

## Built-in events

| Token | Description |
|-------|-------------|
| `[laugh]` | Five short voiced pulses with a rising-then-falling F0 contour. |
| `[sigh]` | A long breath followed by a falling voiced release. |
| `[breath-in]` | Two unvoiced breath segments with rising intensity. |
| `[breath-out]` | Two unvoiced breath segments with falling intensity. |
| `[throat-clear]` | A low glottal clear and short voiced release. |

`formant --list-nvv` prints the available events and styles. The table lives in [`data/nvv/nvvs.txt`](../data/nvv/nvvs.txt), is embedded into the binary at build time (no runtime data dependency) and is installed under `share/formant/nvv` for inspection.

## Inline tokens

Tokens are placed exactly where they occur in `--text` or `--stdin`:

```sh
formant --text "Hello [laugh] — are you serious [sigh]?" --output scene.wav
```

The scanner splits the text into ordinary runs and `[name]` tokens. Runs go through the selected text front end (built-in language, `--lang-file`, `--lexicon` or any combination); tokens are replaced by their segment lists. An unmatched `[` is left as literal text, and a well-formed but unknown name fails with `unknown nonverbal vocalization: 'name'` rather than being silently dropped.

## SSML

The same events are available through an explicit element in the supported SSML subset (see [docs/ssml.md](ssml.md)):

```xml
<speak>Really? <nvv name="laugh"/> I supposed so. <nvv name="sigh"/></speak>
```

`<nvv>` composes with nested `<prosody>` (rate, pitch and volume scale the event's segments) and reports an unknown name with the markup byte offset.

## Styles

A style multiplies each event's duration, F0 contour and amplitude:

```sh
formant --nvv-style nervous --text "Hi [laugh]" --output nervous.wav
formant --nvv-style warm --text "Hi [laugh]" --output warm.wav
```

| Style | Rate | Pitch | Amplitude |
|-------|------|-------|-----------|
| `neutral` | 1.00 | 1.00 | 1.00 |
| `nervous` | 1.18 | 1.22 | 1.04 |
| `warm` | 0.92 | 0.93 | 1.02 |

Styles are data too. A `style` line in a user file with an existing name replaces the built-in preset. The same event therefore renders differently under different styles while remaining byte-identical for identical inputs.

## Custom events

User definitions are plain data and override built-ins by name:

```sh
formant --nvv-file my-events.nvv --text "He [snort] and [laugh]." --output scene.wav
```

```text
# my-events.nvv — whitespace-separated fields, # comments supported
nvv snort | A short surprised snort
s 0.05
a 0.06 1.10 0.80 1.00

nvv laugh | Replace the built-in laugh
e 0.03 1.20 1.40 1.00
e 0.04 1.40 0.90 1.00

style nervous 1.10 1.15 1.00
```

Each segment line is `IPA duration [pitch-start] [pitch-end] [amplitude]`; omitted pitch values default to `1.0` and amplitude to `1.0`. Durations must be positive, and IPA symbols must be recognised by the universal mapper. Malformed lines fail with the line number. A custom event can use any phoneme the engine knows, including IPA diacritics, so creaky or nasal flavours are possible without extending the NVV format.

## Composition and limits

- NVVs are phoneme sequences, so they obey the same scheduler, crossfades, intonation overlays, prosody-direct overrides, timing exports and voice presets as speech.
- `--ipa`, `--xsampa` and `--phonemes` are raw modes and bypass NVV token expansion.
- The library API is `formant::NvvLibrary` (`include/formant/nvv.h`): load built-ins plus an optional user table, select a style, resolve a single name with `get()`, or split mixed text with `expand()`.
- This increment does not add a general affect layer. Styles are explicit named presets; NVVs do not yet infer a style from surrounding emotion. That composition is future work.
- Events are deliberately stylised sine-only approximations. They are recognizable in context but are not recordings or noise-based synthesis.
