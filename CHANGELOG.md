# Changelog

All notable changes to this project are documented here. The format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/).

## [Unreleased]

## [0.4.21] - 2026-11-22

### Fixed

- A run of prefix tone bars (for example `˥˩a` or X-SAMPA `<HL>a`) now produces a falling/rising F0 contour matching its postfix equivalent (`a˥˩` / `a<HL>`). Previously each prefix bar overwrote the last, leaving a flat tone.

## [0.4.20] - 2026-11-22

### Fixed

- A standalone `,` token in a bracketed phoneme sequence (for example `[h ee l o , w er l d .]`) is no longer discarded, so the documented comma pause is rendered. A comma attached to a phoneme name (for example `[a, b]`) still acts only as a separator.

## [0.4.19] - 2026-11-22

### Fixed

- IPA and X-SAMPA diacritics written after a suprasegmental (for example `aː̃`) now modify their own base rather than being applied to the following segment or dropped at end of input. Modifier order no longer changes audio duration (e.g. `a_X:` and `a:_X` now match).

## [0.4.18] - 2026-11-22

### Fixed

- `Synthesizer::render()` no longer silently clamps slow rates to 0.1. Rates below 0.1 now produce the documented `round(total duration * sampleRate / rate)` sample count, and non-finite, non-positive or out-of-range rates are rejected explicitly instead of overflowing.

## [0.4.17] - 2026-11-22

### Fixed

- Numeric CLI options now accept finite subnormal doubles such as `--gain 1e-310`. These were previously rejected because `std::stod` treats the `ERANGE` set on underflow as an error, even though the value is valid and finite.

## [0.4.16] - 2026-11-22

### Fixed

- Digits embedded in alphanumeric words (e.g. `B2`, `H2O`, `Room 101B`) are now spelled out instead of being silently dropped by the English text front end.

## [0.4.15] - 2026-11-22

### Fixed

- Peak normalisation no longer overflows when the raw audio peak is subnormal (e.g. a tiny finite `--gain` with weak harmonics). Samples are now divided by the peak before scaling, so output stays finite and respects the 0.9 ceiling instead of becoming inf/NaN and clipping to full scale.

## [0.4.14] - 2026-11-22

### Changed

- The Release workflow is now idempotent: re-running it for a tag whose release already exists refreshes the release notes instead of failing with an HTTP 422 error.

## [0.4.13] - 2026-11-22

### Fixed

- The CLI no longer drains standard input when a transcription mode (`--phonemes`, `--ipa`, `--xsampa`) supplies the phonemes, so combining a transcription with `--stdin` returns immediately instead of blocking.

## [0.4.12] - 2026-11-22

### Fixed

- Peak normalisation now accumulates samples in double precision, so a large finite gain can no longer overflow a float and produce NaN output. Non-finite intermediate samples are rejected explicitly.

## [0.4.11] - 2026-11-22

### Fixed

- Voiced rendering now caps the number of glottal harmonics. A near-zero fundamental can no longer make the Nyquist break unreachable and cause rendering to run indefinitely with a large `--harmonics` value.

## [0.4.10] - 2026-11-22

### Fixed

- The CLI now range-checks integer options before converting, avoiding undefined behaviour on out-of-range values while still rejecting them with the existing error.

## [0.4.9] - 2026-11-22

### Fixed

- `wavHeader()` now rejects sample counts whose `36 + dataBytes` RIFF chunk size would overflow 32 bits, instead of writing a corrupt size field.

## [0.4.8] - 2026-11-22

### Fixed

- Segmental diacritics on affricate ligatures such as `ʧ̬` now qualify the whole consonant: voicing, place and ejective modifiers apply to both the stop and fricative components. The tie-bar spelling `t͡ʃ̬` still applies the diacritic only to the symbol it follows.

## [0.4.7] - 2026-11-22

### Fixed

- The CLI now rejects non-finite numeric values such as `nan`, `inf`, `infinity` and `-inf` for every numeric option instead of silently rendering corrupted or silent audio.

## [0.4.6] - 2026-11-22

### Fixed

- Precomposed rhotic vowels ɚ (U+025A) and ɝ (U+025D) now lower F3 and strengthen its band like the hooked `ə˞`/`ɜ˞` forms, instead of rendering identically to plain `ə`/`ɜ`. The X-SAMPA `@\`` alias now renders as a rhotic schwa too.

## [0.4.5] - 2026-11-22

### Fixed

- X-SAMPA `_r` now maps to the raised diacritic (U+031D) instead of the global-rise arrow, so `a_r` raises vowel height like its IPA equivalent `a\u031D`. Global rise remains available via `<R>`.

## [0.4.4] - 2026-11-22

### Changed

- CI and Release workflows now use `actions/checkout@v7`, which targets the Node 24 runtime. This removes the Node 20 deprecation annotations and keeps the workflows compatible with current GitHub-hosted runners.

## [0.4.3] - 2026-11-22

### Fixed

- The CLI error for conflicting transcription modes now describes the rule that is actually enforced; `--text` and `--stdin` are documented as being overridden rather than reported as conflicts.
- CLI smoke tests now cover conflicting transcription modes and `--text` / `--ipa` precedence.

## [0.4.2] - 2026-11-22

### Fixed

- Apical and laminal diacritics (U+033A/U+033B), used by X-SAMPA `_a` and `_m`, are now recognised instead of being rejected as unknown IPA symbols.
- X-SAMPA tie-bar underscore forms such as `k_p`, `N_m` and `t_s` now parse as double articulations and affricates, matching their no-underscore equivalents.

## [0.4.1] - 2026-11-22

### Fixed

- X-SAMPA `I\` and `U\` now map to the near-close central vowels ᵻ and ᵿ instead of being rejected.
- Replaced two unreachable multi-codepoint IPA table entries with the actual single-codepoint characters they described, so `--list-ipa` only advertises symbols the parser can match.
- Removed the incorrect `ᵗ` (MODIFIER LETTER SMALL T) schwa alias.

## [0.4.0] - 2026-11-22

### Added

- Full IPA input: all official vowels and pulmonic/non-pulmonic consonants, common extensions, diacritics, tie-bar affricates, stress, length, tones and suprasegmentals.
- UTF-8 `--ipa` input, ASCII `--xsampa` input, `--list-ipa` discovery and the `parseIpa()` / `parseXsampa()` library API.
- Per-segment amplitude and pitch-contour fields on `Phoneme`, so IPA prosody reaches the sine renderer without a second synthesis path.
- Coverage tests for every advertised IPA symbol, X-SAMPA translation, diacritics, tones and CLI rendering.

### Changed

- IPA is now the language-independent front end; the English lexicon remains an optional convenience layer and existing `--text` / `--phonemes` behaviour is unchanged.

## [0.3.0] - 2026-11-22

### Added

- Releases publish `SHA256SUMS` and a `.sha256` file for every archive.
- CTest checks that `formant --version` matches the CMake project version.

### Changed

- Release notes are taken from the matching changelog section instead of generated commit subjects.

## [0.2.0] - 2026-11-22

### Added

- Diphthong phonemes interpolate from a start to an end formant target.
- Grapheme-to-phoneme rules for `-tion`, `-sion` and `-ing`.
- Generated version header, so `formant --version` matches the CMake project version.
- Tagged releases build and attach source and platform CLI archives.

### Fixed

- WAV samples at full negative scale now map to `-32768` instead of `-32767`.

## [0.1.0] - 2026-11-22

### Added

- C++20 `formant_core` library with voice presets, phoneme table, deterministic additive synthesiser and WAV writer.
- CLI with text and bracketed-phoneme input, voice selection and parameter overrides.
- Small English lexicon with rule-based fallback for unknown words.
- CTest unit suite and a CLI smoke test.
- CMake install rules and CI on Linux and macOS.
