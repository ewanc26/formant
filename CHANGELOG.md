# Changelog

All notable changes to this project are documented here. The format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/).

## [Unreleased]

## [0.4.37] - 2026-11-22

### Fixed

- English G2P now treats word-initial `wh` as a single /w/ (`when` -> `[w e n]`, `which` -> `[w i ch]`, `wheel` -> `[w ee l]`). It previously emitted a spurious /h/ because no `wh` digraph rule existed. A closed /h/ family (`who`, `whom`, `whose`, `whoever`, `whomever`, `whosever`, `wholly`, `wholeness`, `whore`) now has explicit lexicon entries with the correct /uː/ or /o/ vowel; `whorl`, `whoa` and `whopper` keep /w/, and medial `wh` in compounds such as `cowhide` is unchanged.

## [0.4.36] - 2026-11-22

### Fixed

- English G2P now treats `sc` before `e`, `i` or `y` as a single /s/ (`scent` -> `[s e n t]`, now a homophone of `sent` and `cent`; also `scene`, `science`, `scissors`, `ascent`, `descent`, `crescent`). It previously emitted a doubled /s/ and added an extra 90 ms fricative. `sc` before other letters (`school`, `screen`, `score`) stays /sk/, and `sceptic` keeps its /sk/ exception.

## [0.4.35] - 2026-11-22

### Fixed

- English G2P now treats `gn` as silent /n/ word-initially and word-finally, including plural `-s` (`gnome` -> `[n o m]`, `sign` -> `[s i n]`, `campaigns` -> `[k a m p ay n s]`). It previously emitted a spurious /g/ plosive, rendering `gnome` byte-identically to `[g n o m]`. Medial `gn` in words like `signal`, `magnet` and `signature` stays /g/ + /n/.

## [0.4.34] - 2026-11-22

### Fixed

- English G2P now treats `dg` before `e`, `i` or `y` as a single /dʒ/ (`bridge` -> `[b r i j]`, `budget` -> `[b uh j e t]`, `budgie` -> `[b uh j ee]`). It previously emitted a spurious /d/ stop, rendering `bridge` byte-identically to `[b r i d j]`. Only the two letters are consumed, so following vowels and the silent final `e` rule are preserved; `dg` before other letters (`handgun`) is unchanged.

## [0.4.33] - 2026-11-22

### Fixed

- English G2P now treats `tch` as a single /tʃ/ (`catch` -> `[k a ch]`, `kitchen` -> `[k i ch e n]`). It previously emitted a spurious /t/ stop, rendering `catch` byte-identically to `[k a t ch]`. Plain `ch` words such as `rich` and `teacher` are unchanged.

## [0.4.32] - 2026-11-22

### Fixed

- English G2P now maps word-initial `sch` to /s/ + /k/ for Greek-derived words (`school` -> `[s k oo l]`, `scheme` -> `[s k ee m]`). It previously produced the affricate /tʃ/, rendering `school` byte-identically to `[s ch oo l]`. Medial `sch` in words like `mischief` and `discharge` remains /stʃ/, which is correct.

## [0.4.31] - 2026-11-22

### Fixed

- English G2P now treats `ck` as a single /k/ instead of spelling it as two stops. `back` and `quick` previously rendered byte-identically to `[b a k k]` and `[k w i k k]`, with a spurious second burst and 40 ms of extra audio; they now match `[b a k]` and `[k w i k]`. Standalone `k` is unchanged.

## [0.4.30] - 2026-11-22

### Fixed

- An exclamation mark no longer receives the question-style final pitch rise. `!` now maps to a distinct silence phoneme with the same 0.30 s pause as `?` but without `pitchRise`, so `hello!` renders differently from `hello?` while questions keep their rising contour.

## [0.4.29] - 2026-11-22

### Fixed

- English G2P now handles the `gh` digraph instead of spelling it as /g/ + /h/. Word-initial `gh` is /g/ (`ghost` -> `[g o s t]`), `eigh`/`aigh` mark a long vowel (`weigh` -> `[w ay]`), and other `gh` is silent unless the word is in the irregular `ough`/`augh` set. Common words such as `though`, `through`, `thought`, `laugh`, `cough`, `rough`, `tough` and `enough` now have correct lexicon entries.

## [0.4.28] - 2026-11-22

### Fixed

- English G2P now drops the silent `b` in word-final `mb` clusters, so `bomb` renders as `[b o m]` instead of `[b o m b]` (and likewise `thumb`, `lamb` and plurals such as `bombs`). A medial `mb` as in `number` or `timber` keeps its /b/. `climb` and `comb`, whose vowels are also irregular, now render as `[k l ay m]` and `[k ow m]`.

## [0.4.27] - 2026-11-22

### Fixed

- English G2P now treats the `w` in a `wr-` cluster as silent, so `wrong` renders as `[r o ng]` instead of `[w r o ng]` (and likewise `write`, `wrist`, `wreck`, `wring`, `wrap` and medial `wr` in `rewrite`). Words with a pronounced `w` such as `row`, `warm` and `water` are unchanged.

## [0.4.26] - 2026-11-22

### Fixed

- `render()` now returns exactly `round(total phoneme duration * sampleRate / rate)` samples for multi-phoneme sequences. Previously each phoneme's length was rounded independently, so the total drifted by up to one sample per segment (e.g. `[t e s t]` at 44.1 kHz returned 12790 instead of 12789). Cumulative boundaries now match the documented contract at all supported sample rates and speaking rates.

## [0.4.25] - 2026-11-22

### Fixed

- English G2P now treats the `k` in a `kn-` cluster as silent, so `knee` renders as `[n ee]` instead of `[k n ee]` (and likewise `knight`, `know`, `knock`, `knew`, `knowledge` and medial `kn` in `unknown`). Words with a pronounced `k` such as `kite` and `skin` are unchanged.

## [0.4.24] - 2026-11-22

### Fixed

- English G2P now uses the hard velar `/g/` for common `g`-before-`e`/`i` exceptions such as `get`, `give`, `girl`, `gift`, `begin` and `forget`. Previously the generic soft-g rule produced the affricate `/dʒ/`, making `get` render identically to `jet`. Soft-g words such as `gem`, `gym` and `page` are unchanged.

## [0.4.23] - 2026-11-22

### Fixed

- Voice parameter overrides (`--pitch`, `--rate`, `--harmonics`, etc.) now apply regardless of whether they appear before or after `--voice`. Previously an override seen before `--voice` was discarded when the preset replaced the whole `Voice` struct, making audio depend on flag order.

## [0.4.22] - 2026-11-22

### Fixed

- A phoneme following a diphthong now crossfades from the diphthong's final formant target (`endBands`) instead of its initial bands, removing a spurious backward formant jump at the boundary.

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
