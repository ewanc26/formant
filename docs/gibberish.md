# Seeded gibberish generation

`--gibberish` generates phonotactically shaped words. It is a *text* front end:
the generator produces an IPA transcript, which the ordinary `parseIpa()` →
`Synthesizer` pipeline renders. Nothing in the synthesis path is random; the
seeded generator lives in `src/gibberish.cpp` and never feeds audio state.

Three modes are available:

| Mode | Flag | What it does |
|---|---|---|
| Pattern | `--gibberish-style pattern` (default) | Awkwords-compatible explicit grammar |
| Statistical | `--gibberish-style statistical` or a style tag such as `en-us` | weighted phoneme n-gram sampling from a data table |
| Combined | `--gibberish-style combined` | explicit grammar structure, with category atoms weighted by the n-gram table |

```sh
# Pattern mode
formant --gibberish "CVC" --seed 7 --count 4 \
  --transcript babble.txt --output babble.wav

# Statistical mode (English-like, from committed n-gram data)
formant --gibberish "ignored" --gibberish-style en-us --seed 7 --count 4 \
  --transcript stat.txt --output stat.wav

# Combined mode (pattern structure + statistical category weights)
formant --gibberish "CVCV" --gibberish-style combined --seed 7 --count 4 \
  --output combined.wav
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

## Statistical n-gram tables

`--gibberish-style en-us` uses a committed plain-text table derived offline from
CMUdict (Carnegie Mellon University, BSD-2-Clause). The table stores aggregate
start/end/length/unigram/bigram/trigram counts, not the original dictionary
entries. Provenance, mapping, licence text and the regeneration command live in
[`data/gibberish/PROVENANCE.md`](../data/gibberish/PROVENANCE.md).

Custom tables can be supplied with `--gibberish-style-file PATH`. The format is
line-oriented and validated before generation; unknown or out-of-inventory
phonemes are rejected, and statistical/combined generation is constrained to the
active profile's phoneme inventory so output remains renderable.

## Library API

`formant/gibberish.h` exposes `SeededRng`, `PatternGrammar`,
`PatternInventory`, `PhonotacticNgramTable`, `GibberishStyle`,
`GibberishSettings`, `parsePatternGrammar()`, `defaultPatternInventory()`,
`patternInventory()`, `parsePhonotacticNgramTable()`,
`loadPhonotacticNgramTable()`, `builtinPhonotacticNgramTable()`,
`availableGibberishStyles()`, `patternGrammarHelp()` and
`generateGibberish()`. The result carries per-word IPA, per-word phonemes, the
full utterance ready for `Synthesizer::render()` and the joined transcript.
