# Changelog

All notable changes to this project are documented here. The format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/).

## [Unreleased]

## [0.17.0] - 2026-11-23

### Added

- Symbolic ToBI-style intonation via `--intonation-file` and `--intonation-tune`, plus the `parseIntonationFile()` / `applyIntonation()` API. Data-driven `ToneInventory` label tables (pitch accents, boundary tones, break-index pause tiers, downstep factor), five overridable named tunes per shipping language, and selectors by phoneme index, score time or vowel nucleus. Events expand into the shared phoneme contour in the parameter domain: unselected segments stay byte-identical, explicit prosody-direct contours win inside a phrase, and rendering stays deterministic and phase-continuation safe. Documented in `docs/intonation.md`, with unit, measured-F0 and CLI smoke coverage.

## [0.16.0] - 2026-11-23

### Added

- Prosody-direct input via `--prosody-file` and the `loadProsodyFile()` / `applyProsodyFile()` API: segment- or time-indexed absolute F0 targets in Hz (including linear ramps), relative F0 in cents/semitones/percent, exact or scaled durations, and amplitude multipliers. Unspecified values fall back to the front-end defaults byte-for-byte, overrides compose after SSML, and absolute targets are honored by both the renderer and the metrics oracle through the shared contour function. Documented in `docs/prosody-overrides.md`, including precedence, validation and the phase-continuation contract.

## [0.15.0] - 2026-11-23

### Added

- Minimal SSML input via `--ssml-file` and the `phonemizeSsml()` API: `<speak>`, `<break>` (time and strength), `<prosody>` (rate, pitch, volume), `<phoneme>` (IPA/X-SAMPA), `<say-as>` (digits, number, date) and `<s>`/`<p>` boundaries. XML entities and numeric character references are decoded; unsupported elements fail loudly with the tag name and byte offset. SSML composes with the selected language/lexicon front end, and inline prosody scales phonemes in the parameter domain so rendering stays deterministic. Documented in `docs/ssml.md`, including precedence and known limitations.

## [0.14.0] - 2026-11-23

### Added

- Parameter-domain transposition in the pure `formant/transpose.h` API: `centsToRatio()`, `shiftPitch()`, `scaleFormants()`, `withVocalTractLength()`, `transposeVoice()` and `detuneVoices()`. Pitch and formants move independently before synthesis, so there is no phase-vocoder artifact and identical arguments render byte-identically. The CLI exposes `--pitch-shift`, `--formant-ratio` and `--vocal-tract-scale`.
- The verification harness now measures the transposed audio: an octave shift doubles measured F0 with formants unchanged, a 1.25 formant ratio moves both resonances with F0 unchanged, and a large shift stays finite and normalised through the #108 taper. Design notes document the axes and known-good ranges.

## [0.13.0] - 2026-11-23

### Added

- `Synthesizer::renderContinued()` and the pure `RenderState`/`RenderResult` value types let embedders chain utterances without a phase reset or a fresh normalisation at every joint. The carried state exposes glottal phase, absolute sine-noise time, combined length and the final-rise flag; a regression fixture proves chained calls are byte-identical to one render of the concatenation and that the splice stays smooth. `Phoneme` and `FormantBand` now have defaulted equality so state assertions are straightforward.

## [0.12.0] - 2026-11-23

### Added

- `Voice::taperCeiling` (on by default) fades the last 4% of the usable band before the Nyquist break and the last 4% of the 4096-partial safety cap, so a gliding fundamental cannot pop a harmonic in or out. The raised-cosine window and its rationale are documented in `docs/design.md`; renders whose partials stay below the band remain byte-identical.

### Fixed

- Voiced partials no longer disappear at full amplitude when a pitch contour, declination, question rise or vibrato pushes them across the sample-rate-derived ceiling. A regression fixture renders a rising fundamental with a strong F4 at the ceiling and fails when the fade is disabled.

## [0.11.0] - 2026-11-23

### Added

- Dependency-free objective metrics in `formant_core`: deterministic F0 tracking by normalised autocorrelation with parabolic refinement, gain-independent mel-envelope distance and mel-cepstral distortion (MCD), and harmonic peak/guard formant-position checks. The new `formant/metrics.h` exposes `estimateF0()`, `expectedF0()`, `compareF0()`, `estimateEnvelope()`, `expectedEnvelope()`, `compareEnvelope()`, `estimateFormantPeaks()` and `compareFormants()`.
- `src/prosody.h` now shares F0 contours, formant interpolation and the resonance response between `Synthesizer::render()` and the metric target tracks, so analysis cannot silently drift from synthesis. Default 16-bit WAV output remains byte-identical.
- `formant_formant_verification` now measures F0 RMSE/correlation, envelope distance, MCD and formant error across a five-case built-in corpus, compares against `tests/metrics_baseline.txt`, and supports `--report`, `--baseline` and `--update-baseline`. A new `formant_metrics_regression` CTest deliberately renders a mistuned voice and passes only when the metrics reject it.
- [docs/metrics.md](docs/metrics.md) documents the analyzers, corpus, formulas, research references and absolute/drift thresholds for external reproduction.

## [0.10.0] - 2026-11-23

### Added

- Additional audio output paths through one dependency-free encoder: `wav24` (packed 24-bit PCM), `wav32f` (IEEE float), `raw16`, `raw24` and `raw32f` alongside the original `wav16`. WAV files use the canonical 44-byte header with the correct format tag, byte rate and block align; raw files contain samples only. Integer samples clamp at their exact full scale and float samples clamp to `[-1, 1]`; byte order is little-endian on every host.
- `--output -` streams audio to stdout in fixed 4096-frame chunks with no temporary file or platform shim. Status and `--trace` messages move to stderr so stdout stays byte-clean for `aplay`, `ffmpeg` or any other consumer. The default 16-bit WAV writer remains byte-identical to earlier releases.
- Deterministic triangular dither for integer output via `--dither SEED` (also `AudioWriteOptions` in the library). A fixed 32-bit seed produces one stable sequence for the whole render, independent of chunk boundaries, previous renders or wall-clock state. Dither lives only in the output encoder, so synthesis remains sine-only; float formats reject it rather than silently ignoring it.
- New `AudioFormat`, `AudioWriteOptions`, `audioFormatSampleBytes()`, `isRawAudioFormat()`, `isFloatAudioFormat()`, `writeAudio()` and `writeAudioFile()` API in [docs/audio-output.md](docs/audio-output.md), with README examples and WAV validation details.
- `formant_formant_verification` CTest target. It renders a steady vowel at 22050, 44100, 48000, 96000 and 192000 Hz, measures F1/F2 with a Hann-windowed Goertzel filter on the harmonic grid, and checks that each peak matches the phoneme table and dominates nearby off-formant harmonics. A 200-harmonic voice reaches past Nyquist at the lowest rate, confirming the rate-derived ceiling composes with the existing tapers.

## [0.9.0] - 2026-11-23

### Added

- Sample-accurate timing output via `planTiming()`, `splitWordsForTiming()` and `planVisemes()`. A timing plan reuses the exact cumulative scheduler that drives `Synthesizer::render()`, so word and phoneme `startSample`/`endSample` boundaries describe the audio that was produced, including fast/slow `--rate` and zero-length renders. Word labels come from the source text for text input and are derived deterministically from phoneme runs for `--phonemes`, `--ipa` and `--xsampa`; pauses and word gaps fold into the preceding word so captions stay contiguous.
- CLI `--timing FILE` writes word/phoneme timing as JSON (`.json`), SRT (`.srt`), WebVTT (`.vtt`) or enhanced word-level LRC (`.lrc`), inferred from the extension. `--visemes FILE` writes a stable lip-sync viseme track as JSON. Unknown extensions are rejected before rendering. Writers use fixed numeric formatting, JSON escaping and `\\n` line endings for byte-identical output across platforms.
- A documented viseme mapping in [docs/timing.md](docs/timing.md) with stable shape names, approximate openness values and a `PhonemeKind` fallback for unknown phonemes. [docs/timing.md](docs/timing.md) also documents the JSON schemas, caption formats and library API; the README has new timing examples.

## [0.8.0] - 2026-11-23

### Added

- User pronunciation lexicons as data. A line-oriented file supplies exact `word <spelling> <IPA>` entries and ordered, context-sensitive `homograph <spelling> <IPA> [before=<word>] [after=<word>]` rules. Matching is case-insensitive, IPA goes through the universal mapping, and malformed lines report their line number. `parsePronunciationLexicon()` exposes the model, and new `phonemize()` overloads apply it on top of any language front end, including `--lang-file` definitions.
- CLI `--lexicon FILE` for text input. Homograph rules take precedence over exact entries, which take precedence over the language front end; unmatched words fall through. `--trace` now works with `--lexicon` (alone or with `--lang-file`) and reports the source layer per word (`homograph`, `lexicon` or `fallback`) as a validation view.
- [docs/lexicons.md](docs/lexicons.md) documents the format, precedence, trace output, composition with language profiles and the documented limits of adjacent-word context matching.

## [0.7.1] - 2026-11-23

### Fixed

- `render()` now returns exactly `round(total phoneme duration * sampleRate / rate)` samples at fast rates. A residual minimum-one-sample clamp forced one silent sample per segment when the cumulative boundary rounded to zero, so `--ipa a --rate 1e6` produced one sample instead of the documented zero and a four-phoneme phrase produced four. Zero-length segments are now allowed; empty renders produce an empty buffer and a valid 44-byte WAV. Normal and slow rates are unchanged.

## [0.7.0] - 2026-11-23

### Added

- Ordered rewrite-rule engine for conlang romanisation and allophony. Language files can declare `class` sets and `grapheme-rule` / `phoneme-rule` bodies of the form `pattern > replacement / before _ after`; rules run in file order, `0` deletes, `#` anchors word edges, and classes match their longest member. Grapheme rules run before grapheme lookup and phoneme rules after mapping, so both phases feed the existing universal IPA and synthesis path.
- `--trace` for `--lang-file` renders, and an optional trace vector on `phonemize(text, definition, trace)`, reporting every rule application with stable rule numbers and positions. `--trace` is rejected without `--lang-file`.
- `examples/velta.lang` now demonstrates context-sensitive romanisation (`c` before front vowels), intervocalic tapping, class-based environments and word-final deletion; `examples/velta.txt` exercises them. `docs/language-files.md` documents the grammar, precedence, comments and the supported SCA-like subset.

## [0.6.0] - 2026-11-23

### Added

- User-defined language definition files. A plain-text file supplies a language as data: grapheme-to-IPA routes with longest-match resolution, a case-insensitive word lexicon, an optional phoneme inventory, and per-phoneme duration, amplitude, pitch and formant-scale overrides. `LanguageDefinition`, `PhonemeOverride` and `parseLanguageDefinition()` expose the model to embedders, while `phonemize(text, definition)` produces the same `Phoneme` sequence as the built-in front ends.
- CLI `--lang-file FILE` for text input. Malformed files report line-aware errors, strict definitions reject undeclared phonemes and unmapped graphemes, and lenient definitions skip unknown graphemes while keeping universal IPA realizations.
- `examples/velta.lang`, `examples/velta.txt` and [docs/language-files.md](docs/language-files.md): a complete conlang example, sample text, formal grammar, precedence rules and documented limits.

## [0.5.0] - 2026-11-23

### Added

- Language-tagged text front ends. `phonemize(text, Language)` and a new `synthesizeText(text, Language, voice, sampleRate)` overload select a G2P module; `Language` covers English (`en`) and Spanish (`es`), `languageFromTag()` accepts BCP-47-style tags such as `es-MX`, and `languageTags()` / `languageTag()` expose the registry. English remains the default, so existing calls are unchanged.
- Rule-based Castilian Spanish G2P: context-sensitive `c`/`z` -> /θ/, `g` before a front vowel -> /x/, silent `u` in `gu`/`qu`, the `b`/`v` merger, `ll` -> /ʝ/, `ñ` -> /ɲ/, word-initial and post-nasal/lateral trill versus intervocalic tap, diphthong glides, and stress from written accents or the penultimate/final rule. UTF-8 input is handled without locale dependence, including accented capitals and inverted `¿`/`¡`; digits are read in Spanish.
- CLI `--lang TAG` and `--list-languages`. `--lang` is rejected with the language-independent transcription modes, and an unsupported tag reports the supported set.

## [0.4.43] - 2026-11-22

### Fixed

- English plural and third-person `-s` endings now voice from the base's final sound instead of always emitting /s/: `dogs` -> `[d o g z]`, `cats` -> `[k a t s]`, `cars` -> `[k aa r z]`, `books` -> `[b oo k s]`. A sibilant base takes a syllabic /ɪz/ (`boxes`, `watches`, `wishes`), consonant + `y` becomes `-ies` with /z/ (`cities`), and silent-e plurals spell their base first so `lines`, `cakes` and `pages` get the correct ending.
- Words ending in `-ss`, `-us` or `-is` keep a voiceless final /s/ (`class`, `bus`, `status`, `analysis`), and common irregular `-s` forms have entries: `says`, `goes`, `toes`, `shoes`, `heroes`, `news`, `series`, `species`, `always` and `his`.
- Plurals of silent-letter words now voice the suffix too: `bombs` -> `[b o m z]`, `hymns` -> `[h i m z]`, `signs` -> `[s i n z]`, and `listens` / `whistles` end in /z/.

## [0.4.42] - 2026-11-22

### Fixed

- English text now keeps an apostrophe inside a word and expands common contractions instead of inserting a word gap. `don't` renders as `[d o n t]` rather than `[d o n | t]`; regular `'s`/`'ll`/`'re`/`'ve`/`'d`/`'m` endings are composed from the base word (so `it's`, `I'm` and `we're` work), while irregular forms such as `can't`, `couldn't`, `they're`, `it'll`, `it'd` and `o'clock` are explicit entries. The typographic apostrophe `’` is accepted, and a possessive `'s` takes /s/, /z/ or a syllabic /ɪz/ from the final sound.
- Silent `l` is now dropped in word-final `alk`/`olk` (`walk` -> `[w o k]`, `talk`, `chalk`, `folk`, `yolk`), `alf` (`half`, `calf`) and `alm` (`calm`, `palm`, `psalm`), including common inflections such as `walks`, `walked`, `talking` and `calmly`. `could`, `would` and `should` are lexicon entries because their vowel is irregular too; `alkaline` and `almanac` keep their /l/.
- Silent `b` is dropped in word-final `bt` and `btle` (`debt`, `doubt`, `subtle`), and silent `m` in word-initial `mn` (`mnemonic`). Medial clusters such as `obtuse` keep the /b/, and `hymnal` keeps both /m/ and /n/.
- Lexicon entries for `hour`, `honest`, `honor`, `honour`, `heir`, `sword`, `answer`, `salmon` and `almond` remove spurious /h/, /w/ and /l/ phonemes.
- A global rise (`↗` or X-SAMPA `<R>`) now takes effect when written before a segment as well as after it. `↗a` previously rendered as plain `a` because the marker had no base to attach to yet.

## [0.4.41] - 2026-11-22

### Fixed

- A final pitch rise now belongs to the end of the utterance only. A `?` in the middle of a phrase (`Hello? I am here.`) no longer raises the last half-second of a later statement; a question mark that closes the utterance still rises.
- `writeWav()` rejects non-finite samples with `std::invalid_argument` before opening the output file, instead of invoking undefined behaviour in `std::llround` and writing a corrupt WAV.
- English G2P now drops the silent `p` in word-initial `pn`, `ps` and `pt` clusters (`pneumonia` -> `[n ...]`, `pseudo`, `pterodactyl`) and the silent `h` in word-initial `rh` (`rhythm`, `rhetoric`, `rhyme`). Medial clusters keep the consonant (`apnea`, `chapter`, `overheat`).

## [0.4.40] - 2026-11-22

### Fixed

- English G2P no longer pronounces the silent `t` in word-final `stle`/`sten` clusters (`listen` -> `[l i s uh n]`, `whistle` -> `[w i s uh l]`, `castle`, `fasten`, `hasten`, `moisten`, and their plurals). The generic letter rules previously emitted a spurious /t/ plosive. A word-final-only rule emits /s/ + reduced vowel + /l/ or /n/, while `tungsten` keeps its pronounced /t/ via a lexicon entry and medial/suffixed clusters (`stencil`, `listened`, `whistled`) are unchanged.

## [0.4.39] - 2026-11-22

### Fixed

- English G2P now drops the silent `n` in word-final `mn` clusters (`hymn` -> `[h i m]`, `autumn` -> `[a uh t uh m]`, `column`, `damn`, `solemn`, `condemn`, `limn`, and their plurals). The generic letter rules previously emitted a spurious /n/, adding 560 samples of bogus audio at 8 kHz. A word-final-only rule, parallel to the existing `mb`/`gn` rules, keeps medial `mn` before a vowel (`hymnal`, `autumnal`, `damnation`, `mnemonic`) unchanged.

## [0.4.38] - 2026-11-22

### Fixed

- English G2P now spells `-ssion` words as a single /ʃən/ (`mission` -> `[m i sh ah n]`, `session`, `passion`, `discussion`, `admission`, `permission`). A longer `ssion` rule now precedes the unconditional `sion` rule, so the doubled `s` no longer leaves a stray /s/ before the voiced /ʒ/. Vowel + `-sion` words such as `vision` and `decision` keep their /ʒən/ unchanged.

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
