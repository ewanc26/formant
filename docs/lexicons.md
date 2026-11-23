# User lexicons

A user lexicon is a plain-text file that supplies pronunciations as data. It
lets place names, personal names, brand names and homographs be pronounced
correctly without rebuilding the engine or editing its built-in G2P rules.

## Format

One directive per line. Blank lines are ignored and `#` starts a comment that
runs to the end of the line.

```
word <spelling> <IPA>
homograph <spelling> <IPA> [before=<word>] [after=<word>]
```

- `spelling` is matched ASCII case-insensitively against a whole token. An IPA
  transcription may contain spaces (for example `ˈziːlənt` is one entry).
- `before=` and `after=` constrain a homograph rule to the preceding or
  following word. Both are optional; an omitted constraint matches any
  neighbour. Context words are matched case-insensitively and punctuation
  between words is ignored.
- IPA is parsed through the same universal mapping as `--ipa`, so every
  symbol the engine recognises is available. Unknown symbols, duplicate or
  empty context options, missing fields and unknown directives are rejected
  with a `lexicon line N:` error.

Example:

```
# names
word Ewan juːən
word Zeeland ˈziːlənt

# homographs: the first matching rule wins
homograph lead led after=us
homograph lead liːd before=the
homograph bass beɪs before=the
homograph bass bæs after=player
```

## Precedence

For each word, the layers are tried in this order:

1. Homograph rules, in file order. The first rule whose spelling and context
   constraints match wins.
2. Exact `word` entries.
3. The selected language front end (English by default, Spanish with
   `--lang es`, or a `--lang-file` definition).

A match in an earlier layer replaces the word's phonemes entirely; unmatched
words fall through to the next layer. Punctuation is preserved from the input
text, and words are separated by the same short pause the built-in front ends
use.

## Command line

```
formant --text "Ewan lead us" --lexicon names.lex --output out.wav
formant --text "Ewan" --lang es --lexicon names.lex --trace --output out.wav
formant --text "velta" --lang-file examples/velta.lang --lexicon names.lex
```

`--lexicon` is a text-input option and is rejected together with
`--phonemes`, `--ipa` or `--xsampa`, which are language-independent.
`--trace` works with `--lexicon` (with or without `--lang-file`) and prints one
line per word of the form `N:word: layer`, where `layer` is `homograph`,
`lexicon` or `fallback`. That is the validation view: it shows exactly which
layer fixed each word.

## Library API

```cpp
#include <formant/formant.h>

using namespace formant;
PronunciationLexicon lex = parsePronunciationLexicon(source);
std::vector<std::string> trace;
auto phonemes = phonemize(text, Language::English, lex, &trace);

// Composes with a user-defined language definition.
LanguageDefinition def = parseLanguageDefinition(definitionSource);
auto conlang = phonemize(text, def, lex, &trace);
```

## Limits

Context tests are deliberately simple: only the immediate neighbouring words,
with no part-of-speech tags or learned disambiguation. This resolves the common
homographs but cannot handle every ambiguity in running text; an exact `word`
entry or a more specific pair of context rules is the escape hatch. Inline
SSML phoneme tags are not part of this layer.

Lexicons are deterministic: the same text, language front end and lexicon file
always produce the same phoneme sequence and byte-identical audio.
