# Formant-trajectory verification

The ordinary tests prove that formant is *functional*: presets parse, WAV
headers are right, renders are bit-for-bit deterministic. This harness adds the
missing acoustic question: **do the rendered resonances sit where the synthesiser
intends them to be?** It is deliberately an opt-in tool, never part of the fast
default `ctest` run.

## Why an offline oracle

The original research for this idea (DeepFormants, Fast Track) uses an LSTM or a
Praat candidate tracker to follow F1–F4 from audio. Those are useful external
cross-checks, but they are not deterministic across machine learning runtimes,
need Python and a model checkout, and cannot be the gate for a dependency-free
C++ project. Formant already ships an equivalent zero-dependency oracle:

- `src/prosody.h` is the single source of truth for intended F0 and the four
  formant bands. The synthesiser and this harness both call it, so the "intended"
  track cannot drift away from the samples that were actually rendered.
- `src/metrics.cpp` provides the harmonic peak picker (`estimateFormantPeaks`),
  the same analyzer behind the F0/formant checks in `formant_formant_verification`.

The harness samples the intended track every 10 ms over a 50 ms window and
measures the rendered audio with the built-in peak picker on the same grid. No
network, Python, model file or clock is involved; the report is reproducible on
every platform. An external tracker can still be layered on top by exporting
audio and comparing its output to the same golden fixture, but it is not needed
to catch a regression.

## What it reports

Every case prints four RMS numbers for F1–F4 in Hz, on one line:

```
# case frames golden_f1 golden_f2 golden_f3 golden_f4 measured_f1 measured_f2 measured_f3 measured_f4 (Hz RMS)
steady-vowel 96 0.000000 ... 0.000000 69.418522 ... 294.865178 # phoneme ee, 100 Hz, 44.1 kHz
```

The two groups answer different questions:

| Group | Comparison | Detects |
|---|---|---|
| `golden_f1..f4` | intended trajectory vs committed fixture | a changed phoneme table or prosody model, deterministically |
| `measured_f1..f4` | rendered audio vs intended trajectory | acoustic drift: a resonance that moved even though the table did not |

The golden comparison tolerance is 1 Hz — the fixture is generated from the
exact same floating-point model, so any difference is real. The acoustic
comparison uses 150 Hz for F1/F2 and 200 Hz for F3; F4 allows 400 Hz because its
table amplitude is small (`0.08` for `ee`) and its weak peak is easily confused
with F3's resonance tail on a harmonic grid. The report prints the raw numbers,
so a gradual trend is visible long before a threshold is crossed.

## Running it

```sh
cmake -S . -B build-verify -DCMAKE_BUILD_TYPE=Release
cmake --build build-verify --target formant_verify
```

The custom target renders the corpus, checks it and writes
`build-verify/formant-verify-report.txt`. It also prints the report to stdout
and exits non-zero on drift. The underlying tool accepts:

| Flag | Meaning |
|---|---|
| `--report FILE` | also write the numeric report to `FILE` |
| `--golden FILE` | compare against a different fixture |
| `--write-golden FILE` | regenerate the fixture (explicit, review the diff) |
| `--self-test-broken` | dry run with a deliberately broken formant table |
| `--help` | print usage |

The golden fixture only changes when `--write-golden` is passed by hand; a normal
run is read-only.

## Broken-table dry run

A harness that never fails is worthless. `--self-test-broken` offsets F1 and F2
by 400 Hz in a copy of every corpus phoneme and requires the golden comparison
to notice:

```sh
./build-verify/formant_verify_tool --self-test-broken
# broken dry run steady-vowel F1=399.254089 Hz F2=399.254089 Hz (96 frames)
# ...
# broken formant table detected: golden comparison is non-vacuous
```

The compiled-in phoneme table is not mutated; this is a dry run over a copied
`Phoneme` vector, so the check is safe to run anywhere.

## Corpus and fixture

Three cases cover the paths most likely to regress:

| Case | Source | Why |
|---|---|---|
| `steady-vowel` | `ee` at 100 Hz, 44.1 kHz | stable, strong F1/F2/F3 — a baseline |
| `diphthong-glide` | `ay` at 110 Hz, 44.1 kHz | moving bands and cross-phoneme inheritance |
| `bass-vowel` | `ee` with the `bass` preset at 48 kHz | low F0 and a different sample rate |

`tests/formant_trajectory_golden.txt` stores one row per usable analysis frame:
case, centre sample, intended F0 and F1–F4 in Hz. Fixture lines are fixed-point
to six decimals so diffs are stable across platforms. If a change to the
phoneme table or prosody model is intentional, regenerate and review it:

```sh
./build-verify/formant_verify_tool --write-golden tests/formant_trajectory_golden.txt
git diff tests/formant_trajectory_golden.txt
```

## What stays out of CI

The tool is built with `EXCLUDE_FROM_ALL` and is not registered with CTest, so
neither the default build nor the fast PR gate changes. The checksum corpus
(`docs/property-testing.md`) already catches every bitwise acoustic change in CI;
this report is the complementary, human-readable acoustic diff for a pre-release
review. It can be added as a separate optional CI job later without affecting
the dependency-free default test suite.
