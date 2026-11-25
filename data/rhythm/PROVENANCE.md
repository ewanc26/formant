# Rhythm profile provenance (#144)

The shipped `rhythm_profiles.txt` table uses the Pairwise Variability Index
(PVI) measurements published by Grabe and Low (2002). Those measurements are
the reference for the `vocalic-npvi` and `intervocalic-rpvi` dials in this
repository.

## Source

Esther Grabe and Ee-Ling Low, “Durational variability in speech and the rhythm
class hypothesis”, in *Papers in Laboratory Phonology 7*, edited by Carlos
Gussenhoven and Natasha Warner, 515–546, Berlin/New York: Mouton de Gruyter,
2002.

The authors measured the “North Wind and the Sun” passage from one speaker per
language. Vocalic intervals were measured from vowel onset to vowel offset,
and intervocalic intervals from vowel offset to vowel onset, excluding pauses
and hesitations. The paper reports normalised vocalic PVI (nPVI) and raw
intervocalic PVI (rPVI).

## Definitions

For vocalic interval durations `d[k]`, nPVI is:

`100 * mean( abs(d[k] - d[k+1]) / ((d[k] + d[k+1]) / 2) )`

For intervocalic interval durations `d[k]`, rPVI is:

`mean( abs(d[k] - d[k+1]) )`

Formant reports both indices in the same units as the table: nPVI as a
dimensionless percentage and rPVI in milliseconds. The harness derives its
intervals from the scheduled phoneme durations that drive rendering, so the
metric checks the same timeline used for audio.

## Published values used for calibration

Adapted from Table 1 of Grabe and Low (2002), rounded to the precision used in
this table:

| Language / class | Vocalic nPVI | Intervocalic rPVI (ms) | Class in the paper |
|---|---:|---:|---|
| British English | 57.2 | 64.1 | stress-timed |
| Dutch | 65.5 | 57.4 | stress-timed |
| German | 59.7 | 55.3 | stress-timed |
| French | 43.5 | 50.4 | syllable-timed |
| Spanish | 29.7 | 57.7 | syllable-timed |
| Luxembourgish | 37.7 | 55.4 | syllable-timed-like |
| Japanese | 40.9 | 62.5 | mora-timed |
| Thai | 65.8 | 56.5 | stress-timed |
| Mandarin | 27.0 | 52.0 | syllable-timed-like |

The generic class bands are deliberately broader than any single speaker: the
paper describes rhythm classes as gradient (“weak categorical”), and its
intervocalic measure does not separate stress- and syllable-timed languages as
cleanly as its vocalic measure. The shipped bands therefore encode a target
region, not a claim that one number defines a language.

The Japanese intervocalic band is intentionally higher than the syllable-timed
band. Grabe and Low attribute this to devoiced vowels being counted inside
intervocalic intervals; Formant does not model devoicing, so the calibration is
documented rather than silently copied as a promise about the synthesised
signal.

## Editing the table

`rhythm_profiles.txt` is versioned and checksummed using the same convention as
the other Formant data tables: compute SHA-256 over the file with the
`checksum` directive removed and CRLF line endings normalised to LF. Comments
and blank lines are included. A normal run never rewrites the checksum; the
explicit update script reports the value that must be pasted into the file.
