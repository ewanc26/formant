# Modified Rhyme Test (MRT) battery

The Modified Rhyme Test (House, Williams, Hecker & Kryter, 1965) is a
closed-set segmental intelligibility test. It measures whether listeners can
distinguish single initial or final consonants in CVC words. It is useful for
comparing Formant voices, parameter changes and releases on a repeatable
historical scale, not as an online listening platform.

## Generate the battery

```sh
formant --mrt /tmp/mrt-neutral --voice neutral --rate 1.0 --pitch 120
```

The output directory contains:

- `mrt_###_eNN_<focus>_<word>.wav` - 300 stimulus WAV files (50 ensembles × 6 words)
- `response_sheet.csv` - listener response template
- `answer_key.csv` - scoring key with the target and six alternatives per trial
- `README.txt` - administration and scoring notes

The rendering is deterministic for a given voice, language, sample rate and
Formant version. Use the same voice and rate when comparing releases.

## Administration

1. Play each WAV once, in listed or randomized order. Avoid repeating items
   unless the protocol explicitly allows it.
2. For each trial, the listener selects one of the six alternatives in that
   ensemble. The answer key includes the alternatives.
3. Record responses in `response_sheet.csv`.
4. Score percent correct overall and separately for `initial` and `final`
   ensembles. A guessing-corrected score is:

   `(correct - (total - correct) / 5) / total * 100`

## Scope

This battery covers English CVC initial/final consonant contrasts from the
standard MRT. It does not cover clusters, vowels, prosody, sentences, or
non-English minimal pairs. The CLI generates reproducible stimuli and templates;
it does not collect responses or compute statistics.

## Provenance

The word list is the canonical 50-ensemble MRT list reproduced from the
public-domain NIST MRT software (`usnistgov/MRT`).
