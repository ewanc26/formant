# Performance and the benchmark harness

Formant is designed for embedding in scripts and on small devices. This page
records the performance contract, how it is measured, and which hot-path
changes are allowed to ship.

## Definitions

- **Real-time factor (RTF)** is `audio seconds / CPU seconds`. An RTF of 2 means
  one second of audio costs half a second of CPU; larger is faster. The value is
  the median of several timed renders of a fixed corpus, after one untimed
  warmup render.
- **Samples per second** is `rendered samples / median CPU seconds`.
- **Complexity breakdown** reports the configured F0, the effective harmonic
  count after the Nyquist and safety caps, and the number of deterministic sine
  partials used for the unvoiced/noise source.

The harness reuses the engine and the C++ standard library only. Timing uses
`std::chrono` in `benchmark/`, deliberately outside the `src/` and `include/`
trees so the engine and the sine-only scan stay free of wall-clock state.

## Running the benchmark

```sh
formant --benchmark
formant --benchmark --benchmark-repeats 10 --benchmark-report report.tsv
formant --benchmark-expected benchmark/expected.txt
```

`--benchmark` renders a fixed built-in corpus: the sentence "The quick brown
fox jumps over the lazy dog. Formant speaks in sine waves only." under all six
voice presets at 44.1 kHz. The corpus, sample rates and voices are constants in
`benchmark/benchmark.cpp`, so results are comparable across machines and
releases. Each case reports RTF, samples/second, the complexity breakdown, and
a SHA-256 checksum of the canonical 16-bit PCM bytes. Hashing the quantised
stream rather than raw float bits keeps the checksum identical across compilers
and sanitizer builds while still catching real rendering changes; it is the
same byte-identity contract that fixes the default 16-bit WAV hash.

The checksums are compiled into the harness and also published as data in
`benchmark/expected.txt`. A mismatch is a hard failure: it means rendering
changed and the byte-identity contract needs an explicit version decision.
`benchmark/rtf-baseline.txt` records reference RTF values for the CI comparison.

To regenerate the expected-results file after an intentionally versioned audio
change, run `formant --benchmark-expected benchmark/expected.txt` and commit the
result together with the new hashes and an update to this document.

## Performance budget

The budget is a floor, not a target for typical hardware. Measurements are
single-threaded Release builds of the CLI on the stated reference class.

| Reference class | Voice | Floor (RTF) | Interpretation |
| --- | --- | ---: | --- |
| Raspberry Pi 4-class CPU (arm64, ~1.5 GHz) | `neutral`, default flags | 2.0 | A second of audio costs at most half a second of CPU, comfortably under half of real time |
| Desktop/laptop CI runner (ubuntu-latest x86-64) | `neutral`, default flags | 20.0 | At least 20x real time |
| Any reference class | `whisper`, `robot` | above the neutral floor | Noise-only and low-harmonic voices must not regress below the default-voice floor |

The local Apple Silicon reference recorded roughly 80x real time for `neutral`
and 270x for `whisper` at the time of writing; these figures are illustrative,
not contractual. The contractual floor is the table above.

CI runs the benchmark on `ubuntu-latest` and compares each case against
`benchmark/rtf-baseline.txt`. A regression greater than 30% relative is written
to the job summary and exposed as the `regressed` job output, but does not fail
the build: one runner sample is noisy. Determinism mismatches do fail the
comparison step. To recalibrate the baseline, record a quiet run and commit an
updated `rtf-baseline.txt` with the hardware and date noted in its header.

## Byte-identical optimizations

Only equivalence-preserving changes land on the default path. Two hot-path
costs are already hoisted without changing a single output bit:

- `detail::transitionSamples()` depended only on the voice and sample rate, but
  was recomputed twice per sample. It is now computed once per segment and
  passed to both the crossfade and `bandsAtWithTransition()`.
- The four formant centres and widths were recomputed inside the harmonic and
  noise-partial loops. `FormantEnvelope` precomputes them once per sample and
  keeps the original arithmetic order, so each response is bit-for-bit equal.

A future fast path that trades accuracy for speed (a polynomial vibrato
approximation, a sine recurrence, or a precomputed phase table) must be off by
default, documented as non-bit-identical, and cannot change the default PCM or
the benchmark checksums. The benchmark harness is the mechanical guard for that
contract; the unit and CLI byte-identity suites are the others.

## Comparison with published methodology

The fixed corpus, SHA-pinned expectations, median aggregation and relative
change reporting follow the discipline used by reproducible voice benchmarks
such as [Coval's voice-AI benchmarks](https://github.com/coval-ai/benchmarks):
a frozen input set, a published measurement method, and changes reported
relative to a recorded baseline rather than as absolute numbers from one noisy
run. Quality-versus-cost decisions still need a listening test; RTF alone does
not measure naturalness or intelligibility.
