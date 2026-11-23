# Objective metrics

Formant's verification harness measures what the renderer intended to produce, not how close it sounds to a recording. Every analyzer lives in `formant_core`, uses only the C++ standard library and is a pure function of its inputs. No audio files, random numbers, wall-clock state or third-party DSP library is involved.

The same shared model that drives `Synthesizer::render()` supplies the target tracks: `src/prosody.h` owns fundamental frequency, formant interpolation and the Lorentzian resonance response, and `src/schedule.cpp` owns sample boundaries. A metric regression therefore means the rendered signal moved away from the synthesis parameters, not that a reference file changed.

## Running the checks

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
ctest --test-dir build --output-on-failure
```

`formant_formant_verification` renders the five-case corpus below, writes `build/metrics-report.txt`, compares it with `tests/metrics_baseline.txt` and prints a compact report. `formant_metrics_regression` renders a deliberately mistuned voice and passes only when the thresholds reject it. To write new baselines after an intentional change:

```sh
./build/formant_formant_verification --update-baseline tests/metrics_baseline.txt
./build/formant_formant_verification --report /tmp/metrics-report.txt
```

Use `--baseline PATH` to check against another data file and `--help` for all options. The report and baseline use one space-delimited row per case with six fixed-precision numbers:

```
name f0_rmse_hz f0_rmse_cents f0_correlation envelope_log_distance mcd_db formant_error_hz formant_contrast
```

## Corpus

| Name | Input | Voice | Rate |
|------|-------|-------|------|
| `steady-vowel` | sustained `ee`, 200 harmonics | flat 100 Hz test voice | 44.1 kHz |
| `hello-world` | `Hello, world.` | `neutral` | 44.1 kHz |
| `question` | `Are you there?` | `neutral` | 44.1 kHz |
| `alto-vibrato` | `Formant is a voice engine.` | `alto` | 44.1 kHz |
| `bass-vowel` | sustained `ee` | `bass` | 48 kHz |

The first half of the harness also checks F1 and F2 at 22050, 44100, 48000, 96000 and 192000 Hz with the Goertzel peak/guard method described below.

## F0 error

`estimateF0()` uses normalised autocorrelation on a Hann window: for each candidate lag it divides the cross-correlation by the square root of the two frame energies, giving a scale-independent score in `[-1, 1]`. For each frame it searches lags from `sampleRate / maxF0` to `sampleRate / minF0`, picks the earliest local maximum within 94% of the best score, and refines it with a parabolic peak estimate. Frames below a root-mean-square threshold or with a score under 0.55 are unvoiced. The default window is 50 ms with a 10 ms hop and a 70–500 Hz search range.

`expectedF0()` samples the shared prosody model at each frame centre, including declination, per-phoneme pitch contours, the utterance-final rise and vibrato. `compareF0()` ignores frames that either track calls unvoiced and reports:

- `rmseHz` — root mean square of `estimated - expected`, in hertz.
- `rmseCents` — root mean square of `1200 * log2(estimated / expected)`, in cents.
- `correlation` — Pearson's r between the two voiced contours. A perfectly constant contour has undefined variance, so the metrics report 1.0 when both flat tracks agree within 1 Hz and 0.0 otherwise.

RMSE captures local pitch error; correlation captures whether the contour moves in the same direction at the same time. Both are standard objective prosody measures in TTS evaluation.

## Spectral envelope and MCD

`estimateEnvelope()` measures each harmonic of the tracker's F0 with a Hann-windowed Goertzel filter, dividing by the window gain to recover amplitude. `expectedEnvelope()` evaluates the same formant response at those harmonic frequencies, applying the renderer's `1/h` source tilt and per-phoneme amplitude. It also applies the same interpolated spectral zeros and `Voice::nasality` notch as the renderer, so nasal cases are measured against their intended anti-formants rather than a peak-only envelope. Both tracks resample the harmonic log power to a 32-point mel grid between 80 Hz and `min(8000 Hz, 0.45 * sampleRate, maxHarmonic * F0)`, then apply an orthonormal DCT-II to produce 14 mel-cepstral coefficients (`c0 … c13`).

`compareEnvelope()` ignores frames where either track is silent and reports:

- `logDistance` — RMS difference of the two log-power vectors after removing each frame's mean. This makes the number independent of global gain and therefore of `render()`'s peak normalisation.
- `mcdDb` — the mean over frames of `6.14185 * sqrt(Σ(c_estimated[n] - c_expected[n])²)` for `n = 1 … 13`. The constant is `10 * sqrt(2) / ln(10)`, the historical decibel scaling from Kominek, Schultz and Black (2008). The gain coefficient `c0` is deliberately excluded for the same reason.

These are *relative* envelope metrics: the reference is the synthesiser's intended resonance envelope, not natural speech. A sine-only voice is not expected to score like a vocoder reconstructing a human recording. The values are useful as regression signals and for comparing changes inside this engine.

## Formant position

`estimateFormantPeaks()` scans the harmonic grid that a voiced source can actually produce. For each expected formant it takes the strongest harmonic within ±300 Hz, and the strongest harmonic in the 400–600 Hz guard bands on either side. `compareFormants()` reports the absolute frequency error for each band, the maximum error, and the minimum peak-to-guard contrast. A real resonance must both land near its target and dominate nearby off-formant harmonics.

## Thresholds

| Metric | Quality ceiling / floor | Baseline drift |
|--------|-------------------------|----------------|
| F0 RMSE | ≤ 5 Hz | +1 Hz |
| F0 correlation | ≥ 0.95 | −0.01 |
| Envelope log distance | ≤ 0.65 | +0.08 |
| MCD | ≤ 20 dB | +2 dB |
| Formant error | ≤ 150 Hz | +30 Hz |
| Formant contrast | ≥ 1.50 | not drift-checked |

A case fails if an absolute threshold is crossed or if it moves beyond the checked-in baseline by more than the drift allowance. The absolute limits catch a broadly broken synthesiser; the drift limits catch a small change that is still technically "in spec". `formant_metrics_regression` combines a 25% pitch shift with a 30% vocal-tract scaling and verifies that both the prosody and spectral checks reject it.

Report numbers are written with six decimal places. Floating-point implementations may differ in the last bits across platforms, so the drift allowances are wider than that rounding. The checks do not depend on a particular FFT, audio device or locale.

## References

- Kominek, Schultz and Black, [*Synthesizer voice quality of new languages calibrated with mean mel cepstral distortion*](https://www.cs.cmu.edu/~awb/papers/sltu2008/kominek_black.sltu2008.pdf), SLTU-2008. Defines the MCD scaling used above and the rationale for ignoring `c0`.
- [*A review on subjective and objective evaluation of synthetic speech*](https://www.jstage.jst.go.jp/article/ast/45/4/45_e24.12/_pdf/-char/en), Acoustical Science and Technology, 2024. Summarises F0 RMSE, F0 correlation and MCD as standard complementary objective measures.
- Morise, Yokomori and Ozawa, [WORLD](https://github.com/mmorise/World) and its [analysis overview](https://deepwiki.com/mmorise/World/6.1-analysis-and-synthesis-examples), which motivates decomposing verification into F0 plus spectral envelope rather than a single scalar.
