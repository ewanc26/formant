# SSML input

Formant accepts a small, dependency-free subset of the W3C Speech Synthesis Markup Language through `--ssml-file`. The reader turns markup into the same `Phoneme` sequence the text, IPA and lexicon front ends produce, so every sample still comes from the deterministic sine engine. There is no audio post-processing and no hidden state.

## Supported subset

| Element | Attributes | Effect |
| --- | --- | --- |
| `<speak>` | — | Root container. Optional: bare text is accepted. |
| `<break/>` | `time="300ms"` or `time="0.5s"`, or `strength="weak\|medium\|strong"` | Inserts a silence phoneme. `time` is exact; the strengths map to 0.12 s, 0.28 s and 0.5 s. `time` takes precedence over `strength`. |
| `<prosody>` | `rate`, `pitch`, `volume` | Scales the span's phonemes. Spans may nest and compose multiplicatively. |
| `<phoneme/>` | `ph`, optional `alphabet="ipa\|x-sampa"` | Inline transcription, parsed by the same IPA/X-SAMPA reader as `--ipa`/`--xsampa`. Alphabet defaults to `ipa`. |
| `<nvv/>` | `name` | Inserts a data-defined nonverbal vocalization (see [docs/nvv.md](nvv.md)). Composes with `<prosody>`; unknown names fail with the byte offset. |
| `<say-as>` | `interpret-as="digits\|number\|date"` | Expands the contained text to words, then applies the text front end. `digits` and `date` spell each digit; `number` reads the integer. |
| `<s>`, `<p>` | — | Containers that add a 0.28 s sentence or 0.55 s paragraph pause at their close. |

XML comments, processing instructions and a doctype are skipped. The five predefined entities (`&amp; &lt; &gt; &quot; &apos;`) and numeric character references are decoded in text and attribute values.

### Prosody values

| Attribute | Accepted forms |
| --- | --- |
| `rate` | A positive multiplier (`1.25`) or percent (`+25%`), or `x-slow`/`slow`/`medium`/`fast`/`x-fast`. Duration is divided by the resulting rate. |
| `pitch` | A relative unit only: percent (`+10%`) or semitones (`+2st`). The value multiplies each phoneme's F0 contour. |
| `volume` | A non-negative multiplier, percent, or `silent`/`x-soft`/`soft`/`medium`/`loud`/`x-loud`. Multiplies each phoneme's amplitude. |

Absolute pitches such as `150Hz` are intentionally rejected: SSML prosody is relative, so it composes with any voice preset without needing that voice's base F0. Use `--pitch` for an absolute fundamental.

## Example

```xml
<speak>
  <p>Hello. <say-as interpret-as="digits">123</say-as>.
  <phoneme alphabet="ipa" ph="ˈhəˈloʊ"/>
  <prosody rate="1.2" pitch="+2st" volume="1.1">Wide.</prosody>
  <break time="250ms"/>Done.</p>
</speak>
```

```sh
formant --ssml-file script.ssml --voice alto --output out.wav
```

## Precedence

Layers apply in this order, last winning where they overlap:

1. The voice preset and numeric flags (`--voice`, `--pitch`, `--rate`, `--gain`, …) set the base synthesis parameters.
2. `--lang`, `--lang-file` and `--lexicon` select the text front end used for SSML text runs and `<say-as>` expansions. `<phoneme>` bypasses it.
3. Inline `<prosody>` scales the phonemes of its span (duration, F0 contour, amplitude) on top of the voice-level values. Nested spans multiply.

`--ssml-file` replaces `--text`, `--stdin` and the transcription modes; combining it with any of them is an error. `--trace` works with SSML when a language definition or lexicon supplies the text front end. Timing exports use phoneme-derived word labels for SSML because the markup is not plain prose.

## Errors

Unsupported elements, unknown attributes' invalid values, malformed markup and unclosed elements are rejected before rendering with the element name and its byte offset, for example:

```
formant: SSML at byte 8: unsupported element <b>
```

## Library API

Embedders can parse SSML with `phonemizeSsml()` from `formant/ssml.h`. Pass a text front-end callback to compose with a language definition or lexicon, or use the overload that takes a built-in `Language`.
