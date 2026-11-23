# PHOIBLE conformance data — provenance and licence

This directory contains a derived conformance table for issue #98. It is used
only by the offline test `formant_phoible_tests` and by the documentation; the
Formant engine has no dependency on PHOIBLE at build or run time.

## Source

- **Dataset**: PHOIBLE 2.0 — Moran, Steven & McCloy, Daniel (eds.), 2019.
  *PHOIBLE 2.0*. Jena: Max Planck Institute for the Science of Human History.
  DOI [10.5281/zenodo.2626687](https://doi.org/10.5281/zenodo.2626687).
- **Upstream repository**: <https://github.com/phoible/dev>, file
  `raw-data/FEATURES/phoible-segments-features.tsv` (2,162 distinct segment
  symbols with distinctive-feature columns), read from the `master` branch.
- **Data licence**: PHOIBLE data is distributed under
  [CC BY 4.0](https://creativecommons.org/licenses/by/4.0/) (see upstream
  `LICENSE-DATA`). The PHOIBLE development repository's software is MIT
  licensed. We redistribute only a small derived table of segment glyphs and
  coarse feature categories, with attribution here, which CC BY 4.0 permits.

## What is committed

- `segments.tsv` — one row per PHOIBLE segment: `glyph`, `category`, `ipa`,
  `source`. The `glyph` and feature-derived `category` values come from
  upstream; `ipa` and `source` are produced by our offline generator.
- `coverage.txt` — the per-category coverage summary written by the same
  generator, used as the CI no-regression baseline.

The full PHOIBLE dataset, donor databases and per-language inventories are
**not** committed.

## Regenerating offline

`scripts/generate_phoible_table.py` is a development-time script. Obtain the
PHOIBLE features file from upstream, then run:

```sh
python3 scripts/generate_phoible_table.py \
    --features /path/to/phoible-segments-features.tsv \
    --output data/phoible/segments.tsv \
    --report data/phoible/coverage.txt
```

The script never downloads anything and uses only the Python standard library.
The C++ conformance test is authoritative: it parses every `ipa` value with
`parseIpa()` and requires a non-silent render, so a bad approximation fails CI.

## Approximation policy

PHOIBLE includes notation that the sine-only front end does not model, such as
glottalized and breathy modifiers, donor-database ejective markers and rare
placing diacritics. The generator applies a deterministic, documented
substitution policy (see the script header): drop unsupported secondary
articulations, map a few donor letters to nearby IPA symbols, give tone-only
symbols a neutral carrier vowel, and never invent new base letters. Rows using
this policy are marked `source=approx`; all other rows are `source=direct` and
must work with the PHOIBLE symbol verbatim.

Clicks, trills, ejectives and other rare segments are *approximated* by sine
partials, consistent with the limits already documented in the README. The
numbers in `coverage.txt` measure native symbol support, with every remaining
segment covered by an explicit approximation.
