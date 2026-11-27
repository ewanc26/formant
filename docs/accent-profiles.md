# Foreign accent profiles (#135)

Formant's text front ends normally render a language as if by a native speaker. An accent profile is an **opt-in**, versioned data layer that rewrites the already-generated `formant::Phoneme` stream: systematic segment substitutions, epenthesis, per-phoneme realization tweaks and bounded F0/rhythm transfer. It adds no sound source; the renderer still sees ordinary phonemes, so coarticulation and the sine-only engine are shared with every other voice. With no accent flag, every render is byte-identical to earlier releases.

A foreign accent profile is **not** a ToBI pitch accent. The `H*`, `L+H*` and boundary labels in `--intonation-file` / `--intonation-tune` are symbolic F0 events on one syllable (see [intonation.md](intonation.md)). An accent profile is an utterance-wide transform of segment quality and global prosody for a language direction. The two can be combined; exact intonation and `--prosody-file` targets run later and win.

## CLI

The 0.39.0 interface for this data layer is:

```sh
# Inspect and share data
build/formant --list-accents
build/formant --dump-accent es-on-en > es-on-en.accent
build/formant --inspect-accent examples/accent/fictional.accent

# Apply a built-in by name, or a shared single-profile file
build/formant --text "Think this through." --accent es-on-en --output es.wav
build/formant --lang es --text "Hola, mundo." --accent en-on-es --output en.wav
build/formant --lang-file examples/velta.lang \
  --accent-file examples/accent/fictional.accent \
  --text "Velta tata." --output starfall.wav
```

Use one of `--accent NAME` and `--accent-file FILE`, not both. Song mode rejects accent: singing owns its F0. A user file defines exactly one profile, is layered over the built-in table, and is parsed and checksum-checked before output is created. Unknown names, malformed rows and checksum mismatches are hard errors.

## Shipped profiles

`data/accent/accent_profiles.txt` is embedded at configure time and installed under `share/formant/accent`. Version 1 ships two small demonstrators; the authoritative rows are in that file.

| Profile | Direction | Segments and realization | Prosody `mean range dur amp` | Rhythm |
| --- | --- | --- | --- | --- |
| `es-on-en` | Spanish L1 → English L2 | `th→t`, `dh→d`, `v→b` full; five-vowel recentring `i→ee .85` (/ɪ/ toward /i/), `ah→/a/ .85`, `a→/a/ .85` (/æ/), `uh→/a/ .75` (/ʌ/), `ay→e .5`; `insert before s e .7 boundary-cluster` (`stop` → `e-stop`, `best` unchanged); `r` +120 cents, duration ×1.1 | `0.97 0.86 1.05 1.0` | `syllable` |
| `en-on-es` | English L1 → Spanish L2 | `t→th .8`, `d→dh .8`, `b→v .7`; diphthong transfer `e→ay .55`, `insert after o w .4 any` (an /oʊ/-like offglide), `a→ah .6`, `ɾ→r .75`; `insert after n ə .45 final`; `s` −60 cents, duration ×0.95 | `1.03 1.12 0.96 1.0` | `stress` |

These are deliberately compact rule sets inspired by published L2 methodology, not transcriptions of a named speaker or corpus; see [data/accent/PROVENANCE.md](../data/accent/PROVENANCE.md).

## Data format

An accent file is UTF-8 text, one directive per line. Blank lines and text after `#` are comments; fields are whitespace-separated. Metadata values run to the end of the line.

```text
schema 1
name formant-accents
author Formant contributors
description Language-direction accent profiles
license AGPL-3.0-or-later
version 1.0.0

accent NAME SOURCE TARGET
about NAME TEXT
prosody NAME F0-MEAN F0-RANGE DURATION-SCALE AMPLITUDE-SCALE
rhythm NAME stress|syllable|mora|unspecified
map NAME SOURCE REPLACEMENT [STRENGTH [CONTEXT]]
insert NAME before|after ANCHOR REPLACEMENT [STRENGTH [CONTEXT]]
override NAME PHONEME SHIFT-CENTS DURATION-SCALE PITCH-SCALE AMP-SCALE

checksum <sha256>
```

| Directive | Meaning |
| --- | --- |
| `schema 1` | Required; only version 1 is supported. |
| `name`, `author`, `description`, `license`, `version` | Optional table metadata, carried through `--dump-accent` and `--inspect-accent`. |
| `accent NAME SOURCE TARGET` | Declares a profile. `NAME`, `SOURCE` and `TARGET` use letters, digits, `-` and `_`; a file needs at least one profile. The language tags are metadata, not automatic language detection. |
| `about NAME TEXT` | Adds or replaces the profile's description. |
| `prosody NAME F0-MEAN F0-RANGE DURATION-SCALE AMPLITUDE-SCALE` | Bounded utterance-wide transfer: mean in `[0.5, 2]`, range in `[0, 3]`, duration in `[0.5, 2]`, amplitude in `[0, 2]`. |
| `rhythm NAME ...` | Convention declared by the profile: `stress`, `syllable`, `mora` or `unspecified`. |
| `map NAME SOURCE REPLACEMENT [STRENGTH [CONTEXT]]` | Ordered substitution; strength in `[0, 1]`. The first matching map wins. |
| `insert NAME before\|after ANCHOR REPLACEMENT [STRENGTH [CONTEXT]]` | Adds a segment next to the anchor; strength scales its duration and amplitude. |
| `override NAME PHONEME SHIFT-CENTS DURATION-SCALE PITCH-SCALE AMP-SCALE` | Adjusts one phoneme only; one override per phoneme is allowed. |
| `checksum HASH` | Optional integrity value; 64 hexadecimal digits. |

Source and replacement fields accept built-in English short phoneme names or IPA symbols; `+` is the sequence separator in phoneme fields. In version 1 each `map` or `insert` replacement slot takes exactly one segment, so write a second rule rather than a multi-segment sequence. Common IPA equivalents (`θ`/`th`, `ð`/`dh`, `ɪ`/`i`, `ʊ`/`oo`, `ɹ`/`r`, `æ`/`a`, …) are canonicalised when matching, so one rule works for text, IPA and X-SAMPA input. The tap `ɾ` and schwa `ə` stay distinct so a profile can target them without also changing `r` or `uh`.

A bare replacement token resolves to the English short name first, then to IPA. Several glyphs are both: bare `a` is English /æ/, `e` and `i` are English /ɛ/ and /ɪ/. Wrap a replacement in slashes (`/a/`) to force the IPA segment. The authored token is kept as written, so `--dump-accent` round-trips it and the profile resolves it the same way when applied.

`CONTEXT` is one of:

| Context | Matches |
| --- | --- |
| `any` | Every eligible segment (the default). |
| `initial` | The first spoken segment. |
| `final` | The last spoken segment. |
| `boundary` | The first spoken segment or a segment directly after silence. |
| `cluster` | An anchor followed, in the same word, by a consonant rather than a vowel. Nasals and liquids count as consonants. |
| `boundary-cluster` | Both `boundary` and `cluster`: a word-initial consonant cluster such as the `s` of `stop`, but not of `best`. |

Silence, breath/NVV events, segments already tagged `source=accent:NAME`, absolute-F0 spans and note-F0 segments are ineligible. `initial` and `final` ignore ineligible padding. A user file layers individual profiles over the built-ins: a single new `accent` block is a complete shareable file, and a repeated name replaces the built-in profile. Checksums belong to one file, not to the merged table.

The checksum follows the project convention: SHA-256 of the file with the `checksum` directive removed and CRLF endings normalised to LF; comments and blank lines count. `--dump-accent NAME` emits a checksummed table with one profile. `--inspect-accent FILE` reports schema, metadata, profile count and checksum status without rendering. The example template ends with `checksum PLACEHOLDER` on purpose: paste the reported `computed` value in place of `PLACEHOLDER` before inspecting or rendering it.

## What the layer can express

| Requested capability | How to author it |
| --- | --- |
| Nearest native inventory phoneme mapping | `map es-on-en th t 1.0 any`. Strength `1` is a full substitution; an intermediate value blends formants between source and replacement while keeping the authored duration and pitch contour. |
| Final devoicing | `map starfall-on-en z s 1.0 final` targets only the last spoken segment; see the fictional template. |
| Vowel quality substitutions | `map es-on-en a /a/ 0.85 any` moves English /æ/ toward the IPA open front vowel without replacing the whole inventory. |
| Epenthetic vowels to break clusters | `insert es-on-en before s e 0.7 boundary-cluster` puts a vowel before a word-initial `s` cluster; `cluster` alone also matches inside a word. `insert en-on-es after n ə 0.45 final` shows the final-context form. Inserted segments receive the profile's prosody transfer together with their anchor, so they do not leave an F0 step. |
| F0 range and rhythm conventions | `prosody NAME F0-MEAN F0-RANGE DURATION-SCALE AMPLITUDE-SCALE` shifts global F0 centre/range, rate and amplitude; `rhythm NAME syllable` declares a timing convention. The profile's rhythm applies only when no explicit `--rhythm` or `--rhythm-file` was selected. |
| Fine-grained per-phoneme overrides | `override NAME PHONEME SHIFT-CENTS DURATION-SCALE PITCH-SCALE AMP-SCALE` shifts all four formant centres and bandwidths and scales duration, pitch and amplitude for that phoneme alone. It runs after substitutions and insertions, so one substitution can be softened or exaggerated without touching others. |

Mappings run first, then insertions, then overrides, then the bounded prosody pass. Applied scales are clamped by the existing voice/phoneme limits, so a bad author value cannot produce a negative duration or infinite band.

## Composition and precedence

1. A voice preset or `--voice-file` is selected. Affect/style modifies the `Voice` before G2P; explicit numeric voice flags (`--pitch`, `--rate`, `--gain`, …) are replayed afterwards and remain the final voice authority.
2. The selected text front end — English, Spanish, a `--lang-file` conlang, SSML, IPA, X-SAMPA or `--phonemes` — produces the phoneme sequence. Rewrite rules, lexicons and language-file defaults are already resolved.
3. The accent pass runs **after every G2P/text front end**: mappings → insertions → overrides → prosody transfer. Inserted segments carry `source=accent:NAME`.
4. Frication data, breath planning and rhythm run later on that transformed stream. Coarticulation is the renderer's 35 ms formant crossfade, so it also sees the transformed stream; there is no second synthesis path.
5. The neural prosody layer (#177) follows. It skips `accent:`-tagged segments, so an epenthetic vowel is not treated as a learned front-end event. An accent profile itself performs no learning.
6. Symbolic intonation and exact `--prosody-file` targets run last and win inside their spans. Explicit `--rhythm` / `--rhythm-file` selection wins over the profile's `rhythm` convention.

## Determinism

`applyAccent()` is a pure value-in/value-out transform of the phoneme vector: no clock, random seed, locale-dependent ordering or mutable global state. Rules are ordered, first-match-wins, and names are canonicalised the same way on every platform. `--dump-accent` writes 17-digit doubles, so a shared file reproduces the same numbers. A repeated render of the same inputs and profile is byte-identical; with no selected profile, no accent code path changes the output.

## Listening check (MRT)

Issue #135 asks for a listen-through under the existing protocol. Use the [Modified Rhyme Test battery](mrt.md) as a segmental-intelligibility floor:

1. Generate one 300-item battery for the baseline and one for the accent with the same voice, rate, pitch, sample rate and Formant version.
2. Play each WAV once, in listed or randomised order, and record responses in `response_sheet.csv`.
3. Score percent correct overall and separately for `initial` and `final` ensembles. The guessing-corrected score is `(correct - (total - correct) / 5) / total * 100`.

Compare the accented score with the baseline under identical settings. MRT covers English CVC initial/final consonant contrasts only: it says nothing about vowels, clusters, rhythm or whether a profile sounds like a particular speaker. Use a short additional listen-through for those qualities.

## Authoring a fictional conlang accent

[`examples/accent/fictional.accent`](../examples/accent/fictional.accent) is an authoring template for a fictional language `sc` (Starfall) rendered as English `en`. To adapt it:

1. **Define the source language first.** Give the conlang a language definition file with a private-use `tag sc`, an inventory and grapheme/word mappings (see [language-files.md](language-files.md#format)). Language tags in an accent file are metadata; they do not select or detect a front end for you.
2. **List systematic correspondences.** Decide which English segments the conlang's nearest native inventory equivalents realize. Put specific maps before general ones: the first matching `map` wins.
3. **Write the block.** Start with `accent starfall-on-en sc en`, then `about`, `prosody` and `rhythm`. The sample's `syllable` convention is a hand-authored target region, not a claim about a real language.
4. **Add segment rules.** The template maps dental fricatives to stops, recentres `æ`, devoices word-final `z`, and inserts `ə` before `s` when a consonant follows in the same word. Adjust strengths to soften or exaggerate each rule.
5. **Add one `override`.** The sample nudges `r` by +90 cents and ×1.08 duration. This affects only `r`, including after mapping or insertion.
6. **Finish the protocol.** Replace `checksum PLACEHOLDER` with the digest reported by `--inspect-accent`, then render with `--accent-file examples/accent/fictional.accent` and compare with the unaccented baseline. Run the MRT check above if word intelligibility matters.

Because the example is a template, its placeholder digest is intentionally invalid until step 6; it is not a shareable, checksummed data file as committed.

## Library API

`formant/formant.h` includes `formant/accent.h`, exposing `AccentData`, `AccentProfile`, `AccentProsody`, `AccentMapping`, `AccentInsertion`, `AccentOverride`, `builtinAccentData()`, `accentFileChecksum()`, `parseAccentData()`, `loadAccentData()`, `writeAccentData()`, `findAccentProfile()`, `accentProfileNames()`, `resolveAccentProfile()` and `applyAccent()`. Profiles are plain data, so embedders can inspect, retune and share them without changing the renderer.

## Verification

Data checks cover schema and line errors, checksum tampering, layering, first-match ordering, context vocabulary, strength bounds, per-phoneme isolation, determinism, eligible-segment exemptions and composition with style, rhythm, neural and exact prosody. CLI smoke checks cover list/dump/inspect/file commands and the mutual-exclusion and song-mode errors. The audio check is the MRT/listen-through above: objective metrics on a sine-only voice cannot by themselves judge accent quality.

## Limits

- The shipped rules are small demonstrators of a method, not transcribed accents, dialect descriptions or learner corpora.
- The layer works on phonemes, not orthography; it cannot repair a missing or wrong G2P output.
- No automatic language detection, tone reassignment or free-stress placement is performed.
- Strength blending interpolates formants, not a solved articulatory model; intermediate values are a tuning convenience.
- MRT covers initial/final consonants in English CVC words only. It is not a measure of accent authenticity.
- No accent flag means no change at all; the default voice remains byte-identical and sine-only.
