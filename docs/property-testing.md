# Property-based and fuzz testing

The ordinary unit tests check named examples. This harness checks *invariants*
over thousands of seeded inputs instead, turning formant's determinism promise
into a CI-failing proof. It adds no dependencies: the generator is the same
`SeededRng` used by the gibberish front end, and every case prints the seed that
reproduces it.

## What runs where

| Suite | Registered as | When | Volume |
|---|---|---|---|
| Property tests | `formant_property_tests` (CTest) | every PR, Linux and macOS | hundreds of cases per property |
| Fuzz campaign | `formant_fuzz` (manual / `nightly.yml`) | nightly on `main`, or by hand | 50,000 seeded scripts by default |
| Checksum corpus | `formant_checksum_corpus` (CTest) | every PR, Linux and macOS | one render per input kind |

The nightly campaign is kept out of the fast PR gate because tens of thousands
of renders take minutes, not seconds. It runs on both platforms and can be
started from the Actions tab. Every failure prints `seed=N`, so a flaky-looking
failure is immediately reproducible with:

```sh
./build/formant_fuzz --scripts 1 --seed-base N
```

## The five properties

1. **Determinism** — the same input and flags render identical bytes, both
   across repeated renders and across seeds that should not change the output.
   This complements the byte-identity checks by exercising generated inputs
   rather than fixed strings.
2. **Bounds** — no NaN, infinity or out-of-range samples for any bounded input.
   Every generated render checks the full PCM buffer, not a sample of it.
3. **Input rejection** — malformed and adversarial data files are rejected
   early with an instructive error and never crash or throw an unexpected
   exception. Seed-derived byte strings are fed to every public parser; the
   language, voice, affect, neural, breath, prosody, intonation, lexicon, SSML,
   IPA, X-SAMPA, bracketed-phoneme and gibberish-pattern front ends are all
   covered. Song,
   accent and dialogue files do not exist yet; when they are introduced their
   parsers must be appended to the `kParsers` table alongside the structured
   malformed cases so they gain the same hardening guarantee.
4. **Idempotence** — text normalization is stable under repeated application,
   so feeding a normalized string back through the front end cannot change the
   phoneme sequence a second time.
5. **Unicode safety** — arbitrary Unicode, invalid UTF-8 and embedded NULs are
   handled without crashing; invalid encodings produce a clear error.

## Seeded scripts

The fuzz campaign reuses the gibberish grammar machinery to build multi-word
utterances from randomly chosen patterns, categories and seeds, then renders
each one through the full phoneme-to-PCM pipeline under random voices, sample
rates and format settings. Nothing is drawn from a clock or global state, so a
campaign can be replayed exactly. The corpus deliberately includes extreme but
*bounded* parameter values; unbounded values are covered by the CLI's own
validation tests.

## Checksum corpus

`tests/checksum_corpus.txt` stores a SHA-256 for one canonical render of every
input kind: text, bracketed phonemes, IPA, X-SAMPA, gibberish, language tags,
conlang files, voice files, intonation, SSML, prosody files, user lexicons,
alternate formats, sample rates and dither. The CTest hook renders each case and
diffs the digest. A mismatch fails
the build the same way a benchmark checksum does, so an unintended acoustic
change is caught even when all the unit tests still pass.

An intended change must regenerate the data file **deliberately**, review the
diff and commit it with the code change:

```sh
cmake -DFORMANT_CLI=build/formant \
      -DFORMANT_SOURCE_DIR=. \
      -DFORMANT_UPDATE_CHECKSUMS=ON \
      -P tests/checksum_corpus.cmake
git diff tests/checksum_corpus.txt
```

Regeneration is never part of a normal test run — the update flag must be
passed explicitly.
