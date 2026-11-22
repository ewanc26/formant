# Changelog

All notable changes to this project are documented here. The format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/).

## [Unreleased]

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
