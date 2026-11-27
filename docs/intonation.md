# Symbolic intonation (ToBI-style tags)

Formant can render an exact F0 contour (`docs/prosody-overrides.md`) or a numeric emphasis factor, but linguists and language designers usually think in symbols: a pitch accent here, a rising boundary there, a downstepped peak. This layer expands a small, data-driven ToBI-style vocabulary into the same phoneme contours the renderer and the metrics oracle already share. It is opt-in: no tags means no change at all.

## Enabling it

```sh
# A built-in tune by name
formant --text "Are you there?" --intonation-tune yesno --output question.wav

# An annotation file (which may itself name a tune)
formant --text "Hello, world." --intonation-file script.intonation --output out.wav

# Both: the file's own events layer over the named tune
formant --ssml-file script.ssml --intonation-tune statement \
  --intonation-file corrections.intonation --output out.wav
```

`--intonation-tune` and `--intonation-file` compose with every input mode. Expansion runs after the front end (including SSML) and before the scheduler, so the WAV, timing exports and visemes all describe the same phonemes.

## Format

A line-oriented UTF-8 text file. Blank lines and everything after `#` are comments. Four directives are supported:

```
tune yesno                 # optional built-in tune, expanded before the lines below
accent vowel:0 L+H*        # pitch accent on a syllable
accent vowel:1 !H*         # one leading '!' lowers the peak by the downstep factor
boundary L-H%              # phrase-final boundary tone, applied to the final vowel
break vowel:last 2         # break index 0..4 inserts a pause tier
```

### Selectors

| Selector | Meaning |
| --- | --- |
| `0`, `12` | Zero-based phoneme index, matching timing exports. |
| `@0.35s`, `@350ms` | The segment at a score time (sum of durations before `Voice::rate`). |
| `vowel:0`, `vowel:last` | The Nth or final vowel nucleus. Only `kind == Voiced` phonemes count, so a voiced approximant such as `l` is not a syllable. |
| `vowels` | Every vowel nucleus in the utterance. |

A tune's targets that do not exist in a short utterance are skipped, so one tune serves any length. An explicit `accent` or `break` selector that matches nothing is an error with the source line number.

### Labels

The built-in inventory is data, defined in `defaultToneInventory()`:

| Kind | Labels |
| --- | --- |
| Pitch accents | `H*` (high), `L*` (low), `L+H*` (rising), `H+L*` (falling) |
| Boundary tones | `L-`, `H-`, `L%`, `H%`, `L-L%`, `L-H%`, `H-L%`, `H-H%` |
| Break indices | `0` (none), `1`, `2`, `3`, `4`, mapped to 0 / 0.10 / 0.25 / 0.50 / 0.80 s |

Accent and boundary targets are multipliers of the voice's base pitch at the start and end of the segment. They replace the local contour but still pass through declination, the phrase-final rise and vibrato. Prefix any accent with one or more `!` operators to lower it by one downstep factor per prefix; `!!H*` is two steps down.

## Default tunes

Every shipping language profile has five named tunes, all pure data and all overridable by explicit lines in an annotation file:

| Name | Intended use |
| --- | --- |
| `statement` | Falling declarative (`L-L%`). |
| `yesno` | Rising yes/no question (`L-H%`). |
| `wh` | Wh-question with a high initial peak and falling boundary (`H-L%`). |
| `list` | Successively downstepped accents (`H*`, `!H*`, `!!H*`). |
| `exclamation` | High rise (`H-H%`). |

`intonationTuneNames(Language)` lists them and `findIntonationTune()` returns an immutable tune. `languageToneInventory(Language)` returns that language's inventory; the Spanish profile uses slightly shallower peaks and a tighter downstep. Every number is data, so a conlang or a style file can replace an inventory wholesale.

## Precedence and determinism

Layers apply in this order, last winning:

1. Voice preset and numeric flags set the base synthesis parameters.
2. The front end (text, IPA, SSML, …) sets each phoneme's default contour.
3. A foreign-accent profile, if selected, transforms that phoneme stream after every front end (see [accent-profiles.md](accent-profiles.md)).
4. Named tunes expand their symbolic events.
5. The annotation file's own lines override the tune for the same target.
6. A `--prosody-file` applied afterwards wins inside a phrase: absolute Hz targets take precedence in `fundamentalAt()` because they are stored on separate `Phoneme` fields. The symbol remains on the phoneme, but the numeric contour is what renders.

The neural prosody layer follows accent and skips segments inserted by it
(`source=accent:NAME`), so an epenthetic vowel is not treated as a learned
front-end event; exact intonation and prosody targets still win last.

There is no randomness, clock or global state. The same file, language and inventory render byte-identically, and expanded contours honour the phase-continuation contract: a chain of `renderContinued()` calls matches one render of the concatenated, expanded sequence.

## Library API

Embedders use `formant/intonation.h`:

```cpp
formant::IntonationFile file = formant::loadIntonationFile("script.intonation");
formant::applyIntonation(phonemes, formant::Language::English, file);

// Or edit the data directly:
auto inventory = formant::languageToneInventory(formant::Language::English);
inventory.accents["H*"] = {1.4, 1.4};
formant::applyIntonation(phonemes, formant::Language::English, file, inventory);
```

Validation is strict and reports the 1-based line number for malformed files, unknown labels and selectors that match no segment. Unknown named tunes are rejected with the tune name and language tag.
