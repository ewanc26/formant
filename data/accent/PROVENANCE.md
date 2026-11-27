# Foreign accent profile provenance (#135)

The shipped `accent_profiles.txt` table contains two version-1 profiles:
`es-on-en` (Spanish phonology applied to English) and `en-on-es` (the inverse
direction). Both are deliberately small demonstrators inspired by the
methodology of the two papers cited in issue #135. They are **not**
transcriptions of a particular speaker, corpus, dialect or classroom learner,
and their numbers are hand-authored target regions for Formant's sine-only
voice, not measurements copied from the papers.

## Sources

The issue cites two papers:

1. **“Learning-free L2-Accented Speech Generation using Phonological Rules”** —
   <https://ar5iv.labs.arxiv.org/html/2603.07550>. The USC work combines
   phonological rules with a multilingual synthesis model: rules transform
   phoneme sequences with no accented training data, modelling systematic
   consonant, vowel and syllable-structure differences from phonotactic
   constraints, with explicit phoneme-level controllability.

2. **“Prosodically Enhanced Foreign Accent Simulation”** —
   <https://ar5iv.labs.arxiv.org/html/2505.16191>. This work shows that the
   prosodic layer matters alongside the segmental one: accentedness is carried
   by rhythm and intonation transfer, not only by phoneme substitution.

## What the shipped data takes from them

- **Rules at the phoneme level, without accented training data.** An accent
  file rewrites the phoneme stream after the text front end and before
  coarticulation, so correspondences are explicit, inspectable and editable as
  data. This follows the learning-free rule methodology rather than fitting a
  model to speech.
- **Explicit phoneme-level controllability.** Ordered `map`, `insert` and
  `override` directives let an author choose a full substitution, a partial
  formant blend or a one-phoneme adjustment without changing the renderer.
- **Prosody alongside segments.** `prosody` dials transfer bounded F0 mean and
  range, duration and amplitude; `rhythm` declares a timing convention. This
  reflects the second paper's point that segment substitution alone is not an
  accent.

## What it does not claim

- No rule set, numeric value or target band is presented as a result measured
  from either paper's experiments. The papers motivate the design; the shipped
  rows are compact examples chosen to exercise the format.
- The profiles are not recordings, transcriptions or phonetic reconstructions
  of Spanish or English speakers. They do not model tone, free stress, dialect
  variation or social meaning, and they do not assert that one accent sounds
  like a named community.
- `rhythm stress|syllable|mora|unspecified` is a convention declared by the
  profile and applied only when no explicit `--rhythm` or `--rhythm-file` was
  selected. It is not an automatic classification of the input language.
- The [Modified Rhyme Test](../docs/mrt.md) is a segmental-intelligibility
  check, not evidence that a profile matches a real accent.

## Relationship to #177

The neural prosody layer (#177) remains the learning-based layer and is
orthogonal to this one. An accent profile is deterministic rule-based transfer:
it runs before the neural pass and performs no training or online adaptation.
Segments inserted by an accent carry the `source=accent:NAME` provenance tag, so
the neural layer skips them rather than applying a learned correction twice to
a unit the text front end never produced. Exact intonation and prosody-direct
files still win after both layers.

## Editing the table

`accent_profiles.txt` is versioned and checksummed using the project
convention: compute SHA-256 over the file with the `checksum` directive removed
and CRLF line endings normalised to LF. Comments and blank lines are included.
A normal render never rewrites the checksum; `--inspect-accent` reports the
value that must be pasted in. Editing the shipped file requires rerunning
`cmake -S . -B build` to refresh the embedded copy. User files layer individual
profiles over the built-in table, and each file's checksum covers that file
alone, not the merged result.
