# Seeded gibberish generation

`--gibberish` generates phonotactically shaped words from an Awkwords-compatible
pattern. It is a *text* front end: the generator produces an IPA transcript,
which the ordinary `parseIpa()` → `Synthesizer` pipeline renders. Nothing in the
synthesis path is random; the seeded generator lives in `src/gibberish.cpp` and
never feeds audio state.

```sh
formant --gibberish "CVC" --seed 7 --count 4 \
  --transcript babble.txt --output babble.wav
```

The transcript is printed to stdout (or stderr when `--output -` streams audio)
and, with `--transcript`, written one word per line. Those lines can be fed
back through `--ipa` to re-render the same "language" with a different voice.

## Grammar

Run `formant --list-pattern-help` for the inline reference. The main pattern
describes one word; lowercase letters and IPA symbols are literal text and
capital letters refer to subpatterns.

| Syntax | Meaning |
|---|---|
| `a/b/c` | choose one alternative |
| `*n` | weight an alternative, integer 1..128 (default 1) |
| `[...]` | group, so following text sees one unit |
| `(...)` | optional group, present or absent with equal probability |
| `"..."` | literal text, escaping every pattern character |
| `X=a/b` | define subpattern `X`, one definition per line |
| `]^abc` | exclude the fixed string `abc` from the preceding fragment; chain `^a^b` |

Whitespace outside quotes is ignored. Brackets nest up to 32 levels and
recursive subpattern definitions are rejected. A word that violates an
exclusion is discarded and regenerated from the same seeded stream, so a given
seed always yields the same words.

C, V and N have built-in IPA inventories (consonants, vowels and nasals). With
`--lang-file`, a conlang definition supplies them from its `class` directives;
if those are absent, its `phoneme` inventory is classified with the shared IPA
parser. Inline definitions override the inventory for that letter.

## Determinism and prosody

`--seed` accepts an unsigned 32-bit integer (default 1); `--count` defaults to
5. The same seed, pattern and profile reproduce the transcript and the
byte-identical WAV. Different seeds differ.

Words are joined into one utterance with short inter-word pauses. With prosody
enabled (the default) each word gets a seeded statement, yes/no-question or
list contour plus phrase-final lengthening. Word generation and contour
selection use independent streams derived from the seed, so enabling or
disabling contours never changes the generated transcript.

## Library API

`formant/gibberish.h` exposes `SeededRng`, `PatternGrammar`,
`PatternInventory`, `GibberishSettings`, `parsePatternGrammar()`,
`defaultPatternInventory()`, `patternInventory()`, `patternGrammarHelp()` and
`generateGibberish()`. The result carries per-word IPA, per-word phonemes, the
full utterance ready for `Synthesizer::render()` and the joined transcript.
