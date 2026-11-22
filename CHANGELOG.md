# Changelog

All notable changes to this project are documented here. The format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/).

## [Unreleased]

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
