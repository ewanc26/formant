# Language definition files

A language definition file lets a constructed language or a specialised pronunciation set drive Formant without writing C++. It supplies the text front end as data: grapheme-to-IPA mappings, a word lexicon, an optional phoneme inventory and per-phoneme realization overrides. The result is still a normal `formant::Phoneme` sequence, so the IPA inventory, prosody and additive synthesiser are shared with every built-in language.

```sh
build/formant --lang-file examples/velta.lang --text "Velta shael, toran." --output velta.wav
```

```cpp
#include <formant/formant.h>

const auto language = formant::parseLanguageDefinition(source);
const auto phonemes = formant::phonemize("Velta shael.", language);
const auto samples =
    formant::Synthesizer(formant::voicePreset("neutral")).render(phonemes);
```

The complete example is [`examples/velta.lang`](../examples/velta.lang).

## Format

The file is UTF-8 text, one directive per line. Blank lines are ignored. `#` starts a comment. Fields are separated by whitespace.

```ebnf
definition = { line } ;
line       = [ directive ] [ comment ] newline ;

directive  = "name" text
           | "tag" tag
           | "strict"
           | "lenient"
           | "phoneme" ipa { override }
           | "grapheme" grapheme [ ipa ]
           | "word" word [ ipa ] ;

override   = key "=" number ;
key        = "duration-scale"
           | "amplitude"
           | "pitch-start"
           | "pitch-end"
           | "formant-scale" ;
```

- `name` is required and may contain spaces.
- `tag` is an optional BCP-47-style identifier, such as `x-velta`.
- `strict` rejects text that cannot be pronounced completely. `lenient` is the default and skips unknown graphemes.
- `phoneme` declares an IPA symbol. The optional overrides change how that symbol is realized. A declaration with no overrides is still useful in strict mode because it adds the symbol to the inventory.
- `grapheme` maps a spelling to IPA. The IPA is the rest of the line and may contain several phonemes. Omitting it makes the grapheme silent.
- `word` maps a whole word to IPA. Word entries take precedence over grapheme rules and may carry stress marks.

## Matching and precedence

1. The input is split into words at whitespace and punctuation. `, . ; : ? !` and `…` become pauses; quotes, brackets, slashes and dashes only separate words.
2. A word is looked up in the lexicon first. Lookup is ASCII case-insensitive, so `Velta`, `velta` and `VELTA` share an entry.
3. Otherwise the grapheme rules are applied left to right, longest spelling first. With `grapheme sh ʃ`, `grapheme s s` and `grapheme h`, the word `sha` is `ʃ a`, not `s a` with a silent `h`.
4. If no grapheme matches, `strict` reports the offending spelling; `lenient` skips one UTF-8 character and continues.
5. Later definitions of the same grapheme, word or phoneme replace earlier ones.

A word gap is inserted between pronounced words, and closing `?` still marks the utterance-final rise.

## Realization overrides

Overrides are applied to every parsed phoneme with the same name, after the universal IPA mapping has produced its bands:

| Key | Effect |
|-----|--------|
| `duration-scale` | Multiplies the phoneme duration (must be positive). |
| `amplitude` | Sets the per-phoneme amplitude (must not be negative). |
| `pitch-start` | Sets the starting F0 multiplier. |
| `pitch-end` | Sets the ending F0 multiplier. |
| `formant-scale` | Multiplies F1–F4, including a diphthong's end bands. |

For example:

```text
phoneme a formant-scale=1.04 duration-scale=1.15
phoneme ʃ duration-scale=1.10
```

These are deliberate approximations. They do not change the universal IPA definition, so another language or a plain `--ipa` render is unaffected.

## Strict inventories

`strict` makes the inventory a contract: any phoneme produced from a word must have a `phoneme` declaration. This is useful for conlangs because it catches a romanisation that accidentally emits a sound outside the planned inventory.

```text
name Strict Example
strict
phoneme a
phoneme t
grapheme a a
grapheme t t
word ta ˈta
```

Without `strict`, undeclared IPA symbols use their universal articulatory realisation. This is the graceful fallback for borrowed words and for files that only want to override a few sounds.

## What this format does not do

- Ordered, environment-sensitive rewrite rules (romanisation allophony) are a separate layer; the language file only has longest-match grapheme routes.
- There is no automatic stress or tone assignment. Write stress in the IPA of a word entry, or map a grapheme to an IPA sequence containing stress.
- There is no phonotactic word generation, number expansion or per-feature override.
- An unsupported tag, malformed line or unknown override fails loudly with a line number instead of being ignored.
- `--lang-file` applies to text input. `--ipa`, `--xsampa` and `--phonemes` remain language-independent.

The format is intentionally a small, dependency-free first step: it makes transcription, vocabulary and realization data-driven while leaving the rule engine and generation work to later releases.
