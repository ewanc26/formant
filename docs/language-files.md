# Language definition files

A language definition file lets a constructed language or a specialised pronunciation set drive Formant without writing C++. It supplies the text front end as data: grapheme-to-IPA mappings, a word lexicon, an optional phoneme inventory, per-phoneme realization overrides and ordered context-sensitive rewrite rules. The result is still a normal `formant::Phoneme` sequence, so the IPA inventory, prosody and additive synthesiser are shared with every built-in language.

```sh
build/formant --lang-file examples/velta.lang --text "Velta tata, celta, toran'." --output velta.wav
build/formant --lang-file examples/velta.lang --trace --text "tata" --output tata.wav
```

```cpp
#include <formant/formant.h>

const auto language = formant::parseLanguageDefinition(source);
std::vector<std::string> trace;
const auto phonemes = formant::phonemize("Velta tata.", language, &trace);
const auto samples =
    formant::Synthesizer(formant::voicePreset("neutral")).render(phonemes);
```

Passing no trace pointer is the normal path. The complete example is [`examples/velta.lang`](../examples/velta.lang).

## Format

The file is UTF-8 text, one directive per line. Blank lines are ignored. Fields are separated by whitespace. On ordinary lines, `#` starts a comment; rule lines reserve `#` for the word boundary (see below).

```ebnf
definition = { line } ;
line       = [ directive ] [ comment ] newline ;

directive  = "name" text
           | "tag" tag
           | "strict"
           | "lenient"
           | "phoneme" ipa { override }
           | "grapheme" grapheme [ ipa ]
           | "word" word [ ipa ]
           | "class" name { symbol }
           | "grapheme-rule" rule-body
           | "phoneme-rule" rule-body ;

rule-body  = pattern ">" replacement [ "/" environment ] ;
environment = before "_" after ;

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
- `word` maps a whole word to IPA. Word entries take precedence over grapheme mappings and may carry stress marks.
- `class` defines a named set of literal symbols. Classes can be used in rule patterns and environments; they cannot appear in a replacement. A later definition of the same class replaces the earlier one.

### Comments and the boundary token

`#` is the word-boundary token inside a rule. A rule line therefore reserves `#` and does not treat it as a comment. Put comments for rules on their own lines:

```text
# This comment is fine.
phoneme-rule t > ɾ / V _ V
# This one is too.
```

Do not append `# comment` to a rule line; those fields would be read as part of the rule.

## Rewrite rules

Rules are written as `pattern > replacement / before _ after`. They are ordered within each phase by their order in the file. This is the SCA-style notation conlangers already use, restricted here to literal replacements:

```text
class V a e i o u
class C p t k m n s l r v ʃ j ʔ

# Romanisation: c is /s/ before e or i, otherwise /k/.
grapheme-rule c > s / _ e
grapheme-rule c > s / _ i
grapheme-rule c > k

# Allophony: intervocalic t becomes a tap.
phoneme-rule t > ɾ / V _ V

# Delete a word-final glottal stop after a consonant.
phoneme-rule ʔ > 0 / C _ #
```

- Pattern, replacement and environment items are whitespace-separated. A grapheme item may contain several Unicode code points, such as `sh`.
- `0` as the whole replacement deletes the match. A pattern cannot be empty.
- `#` matches the beginning or end of the word. It is only valid in an environment, not in a pattern or replacement.
- `_` marks the locus of the change and must appear exactly once in an environment.
- With no `/`, the rule applies everywhere.
- A class name in a pattern or environment expands to its members. A literal with the same spelling cannot be written where a class name is already defined.
- In a `phoneme-rule` replacement, each item is an IPA phoneme name; the result is parsed through the universal IPA mapping. An invalid replacement is an error even in lenient mode.
- A grapheme rule can emit a spelling that the grapheme table maps to one or more phonemes. In strict mode, a spelling with no mapping is still an error.

### Matching semantics

Each rule scans the stream left to right. At each position the first matching rule is applied once; the replacement is not reprocessed by the same rule, and scanning continues after it. The next rule then runs over the whole result. Within a class, the longest matching member wins.

The `before` environment must end at the pattern start, and `after` must begin at the pattern end. Environments are checked against the input to that rule, not against the partially rewritten output.

Grapheme rules operate on Unicode code points before the grapheme table is consulted. Phoneme rules operate on phoneme names after lexicon or grapheme mapping. Rules apply within one word at a time; `#` provides word-edge context, but there is no cross-word environment.

### Trace mode

`--trace` prints every successful application, in order. Rule numbers are one-based within their phase and positions are zero-based symbol indices:

```text
$ build/formant --lang-file examples/velta.lang --trace \
    --text "Velta tata, celta, toran'." --output out.wav
trace: phoneme-rule 1 at 2: t > ɾ / V _ V
trace: grapheme-rule 1 at 0: c > s / _ e
trace: phoneme-rule 2 at 5: ʔ > 0 / C _ #
```

The trace is stable for a given file and input. It is available to embedders through the optional trace vector on `phonemize()`.

## Matching and precedence

1. The input is split into words at whitespace and punctuation. `, . ; : ? !` and `…` become pauses; quotes, brackets, slashes and dashes only separate words.
2. A word is looked up in the lexicon first. Lookup is ASCII case-insensitive, so `Velta`, `velta` and `VELTA` share an entry. A lexicon hit supplies IPA directly and skips the grapheme phase.
3. Otherwise the ordered `grapheme-rule`s run on the lowercased spelling, then the grapheme table is applied left to right, longest spelling first. With `grapheme sh ʃ`, `grapheme s s` and `grapheme h`, the word `sha` is `ʃ a`, not `s a` with a silent `h`.
4. The ordered `phoneme-rule`s run on the mapped sequence, whether it came from the lexicon or the grapheme phase.
5. If no grapheme matches, `strict` reports the offending spelling; `lenient` skips one UTF-8 character and continues.

A word gap is inserted between pronounced words, and closing `?` still marks the utterance-final rise.

## Realization overrides

Overrides are applied to every parsed phoneme with the same name, after the universal IPA mapping and after allophone rules:

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

`strict` makes the inventory a contract: any phoneme produced after rewrite rules must have a `phoneme` declaration. This catches both a romanisation that accidentally emits a sound outside the planned inventory and an allophone whose replacement was not added to the inventory.

```text
name Strict Example
strict
phoneme a
phoneme t
phoneme ɾ
grapheme a a
grapheme t t
phoneme-rule t > ɾ / V _ V
```

Without `strict`, undeclared IPA symbols use their universal articulatory realisation. This is the graceful fallback for borrowed words and for files that only want to override a few sounds.

## What this format does not do

- It does not implement SCA category substitution, metathesis, optional environment elements, wildcard contexts, rule exceptions or nonce bracket classes. Classes are literal sets; replacements are literal sequences.
- Rule environments do not cross word boundaries or pauses.
- There is no automatic stress or tone assignment. Write stress in the IPA of a word entry, or map a grapheme to an IPA sequence containing stress. An allophone replacement uses the universal realization of the replacement; unchanged phonemes keep their original prosody.
- There is no number expansion or per-feature override. Seeded phonotactic word generation lives in a separate `--gibberish` front end (see [docs/gibberish.md](gibberish.md)); it can draw its C/V/N inventories from this file's `class` directives or phoneme inventory.
- Rule lines cannot carry trailing `#` comments, because `#` is the boundary token.
- An unsupported tag, malformed line or unknown override fails loudly with a line number instead of being ignored.
- `--lang-file`, and therefore `--trace`, applies to text input. `--ipa`, `--xsampa` and `--phonemes` remain language-independent.
