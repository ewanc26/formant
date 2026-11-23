# PHOIBLE conformance

Formant claims broad IPA coverage. Issue #98 makes that claim mechanically
testable against [PHOIBLE 2.0](https://phoible.org/), a cross-linguistic
database of 3,020 phonological inventories whose segment set covers 2,162
distinct symbols.

## What is checked

The committed table `data/phoible/segments.tsv` lists every distinct segment
symbol from the PHOIBLE feature table, with a coarse feature category and the
Formant transcription used to render it. The `formant_phoible_conformance`
CTest case (binary `formant_phoible_tests`) checks that:

1. Every listed segment parses with `parseIpa()` and renders non-silence.
2. Every `source=direct` row still works with the PHOIBLE symbol verbatim, so a
   parser regression on a previously covered symbol fails CI.
3. The measured table matches the committed `data/phoible/coverage.txt`
   summary exactly, so the coverage numbers cannot silently shrink.

PHOIBLE is never downloaded or read by the engine or the tests. The data is
development-time only; see `data/phoible/PROVENANCE.md` for source, licence and
the offline regeneration script.

## Coverage

Current direct (verbatim symbol) coverage, from `data/phoible/coverage.txt`:

| Category       | Segments | Direct | Approximated | Direct coverage |
| -------------- | -------: | -----: | -----------: | --------------: |
| vowel          |      573 |    564 |            9 |            98.4% |
| pulmonic       |     1095 |    894 |          201 |            81.6% |
| non-pulmonic   |      297 |    236 |           61 |            79.5% |
| diacritic      |      197 |    129 |           68 |            65.5% |
| **All**        | **2162** | **1823** |       **339** |       **84.3%** |

Every one of the 2,162 segments renders non-silence: the 339 that are not
supported verbatim are covered by an explicit, documented approximation
(`source=approx`). The approximation policy is conservative — it removes
secondary articulations the sine-only model cannot distinguish and supplies a
neutral carrier vowel for tone-only symbols; it never invents new base
letters. Clicks, trills and ejectives are approximated by sine partials,
consistent with the README's stated limits.

## Regenerating

After a PHOIBLE data update, regenerate the table and summary offline:

```sh
python3 scripts/generate_phoible_table.py \
    --features /path/to/phoible-segments-features.tsv \
    --output data/phoible/segments.tsv \
    --report data/phoible/coverage.txt
```

Review the diff before committing: category counts or direct/approx changes
are expected when upstream data changes, but the conformance test must still
pass with all segments rendering non-silence.
