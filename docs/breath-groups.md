# Breath-group planning (#143)

Long passages need respiratory phrasing: readers plan an inhale against the length of the upcoming clause, and F0 gently declines within a breath group before resetting at the next one. Formant implements this as an **opt-in, data-defined planning layer** over the ordinary phoneme stream. It adds no new signal model: audible breaths are the existing `[breath-in]` NVV from [docs/nvv.md](nvv.md), and declination is a deterministic multiplier on the existing F0 contour.

## Enabling it

Default rendering is unchanged (`--breathing off`). Three modes are available:

```sh
# No planner: byte-identical to the front-end output.
formant --text "$chapter" --output plain.wav

# Plan groups, insert short pauses and apply declination; no audible breath.
formant --text "$chapter" --breathing silent --output silent.wav

# Plan groups and insert scaled breath-in NVVs at boundaries.
formant --text "$chapter" --breathing auto --output breathed.wav
```

`--breathing auto|silent|off` composes with every text front end (`--text`, `--stdin`, `--ssml-file`, `--lang`, `--lang-file`, `--lexicon`), with gibberish and with raw `--phonemes`/`--ipa`/`--xsampa` input. It runs **after** the front end and **before** symbolic intonation and prosody-direct overrides, so an author's exact F0 targets still win. Timing and viseme exports see the inserted events because they consume the final phoneme vector.

`--list-breath` prints the built-in languages, styles and the policy resolved for `en` with the selected `--breath-style`.

## Where breaths go

The planner scans clause-final phonemes (`strong`: `. ! ? ; :`) and soft boundaries (`weak`: `,`). A strong boundary becomes a breath group boundary once at least `min-words` words have accumulated. A comma needs `medium-words` words. A group is also forced at `max-words` words or `max-seconds` seconds of speech so unpunctuated long text still breathes. The final boundary is not followed by a breath.

An author-placed breath wins. NVV tokens such as `[breath-in]` are tagged with provenance (`nvv:breath-in`) by `NvvLibrary`, and the planner treats that segment as the boundary itself. No automatic event is inserted within a small window around it. Callers building phonemes directly can mark their own breath segments with a `source` beginning `breath:`.

## Depth and declination

The upcoming group's word count selects one of three depth tiers:

| Tier | Words | Duration scale (default) |
|------|-------|--------------------------|
| short | below `short-words` | 0.82 |
| medium | `short-words` to `medium-words` - 1 | 1.00 |
| long | `medium-words` and above | 1.18 |

In `auto` mode the selected `breath-in` event is copied from the NVV library, its segment durations are multiplied by the depth scale, and amplitude follows `0.75 + 0.25 * scale`. In `silent` mode a `|` pause of `pause-seconds * scale` is inserted instead. This implements the prepared-inhale finding from the breath-group research: a longer, more demanding upcoming phrase gets a longer breath.

Within each group, the planner multiplies every voiced segment's `pitchStart` and `pitchEnd` by a linear ramp from `1.0` to `declination` (default `0.965`). The first group starts at `1.0`; each later group starts at `reset` (default `1.045`), producing a measurable F0 rise across the respiratory event followed by another decline. Absolute `f0StartHz`/`f0EndHz` targets are skipped, and later intonation/prosody directives overwrite relative values. The engine's existing whole-utterance `--declination` still applies underneath; it is independent of this local contour.

The harness records each group's phoneme range, score-time range, word count, tier and depth scale in `formant::BreathPlan`, so tests verify the downtrend and reset directly instead of relying on listening alone.

## Data format

The shipped table is [`data/breath/breath_groups.txt`](../data/breath/breath_groups.txt), embedded into the binary at build time and installed under `share/formant/breath`. It is line-oriented, with `#` comments and whitespace-separated fields:

```text
language default
  min-words 4
  max-words 12
  max-seconds 5.0
  short-words 4
  medium-words 8
  declination 0.965
  reset 1.045
  depth-short 0.82
  depth-medium 1.00
  depth-long 1.18
  pause-seconds 0.08
  strong . ! ? ; :
  weak ,
  event-short breath-in
  event-medium breath-in
  event-long breath-in

language es
  min-words 3
  declination 0.972

style narrative
  max-words 14
  depth-long 1.22
```

Sections start with `language TAG` or `style NAME`; `default` retunes the base policy. A style overlays the resolved language, so `es` plus `narrative` keeps Spanish limits and narrative depth. Unknown language tags and styles fall through to lower layers without error. Malformed lines fail with `breath line N: ...`.

User files layer field by field over the built-in table:

```sh
formant --text "$chapter" --breathing auto \
  --breath-style narrative --breath-file my-breath.txt --output chapter.wav
```

```text
# my-breath.txt — only the fields you want to change
language default
  max-words 16
  declination 0.950

style narrative
  depth-long 1.30
```

The library API is `formant::BreathPolicySet` and `formant::planBreathGroups()` (`include/formant/breath.h`). `parseBreathData(builtin, user)` performs the same layering used by the CLI; `planBreathGroups()` accepts an event loader so embedders can substitute their own breath definitions. Everything is a pure function of the phoneme vector, policy and options: no randomness, no clock, and identical output on every platform.

## Research and limits

- Inhalation depth in narrative speech is planned against the length of the upcoming utterance ([preverbal message planning and inhalation depth](https://www.sciencedirect.com/science/article/pii/S0095447026000720)).
- Reading and spontaneous speech have measurable, task-dependent breath groups ([breath group analysis](https://digitalcommons.unl.edu/specedfacpub/65/)).

This is a stylised planner, not a respiratory model. It does not infer affect, track a speaker's lung capacity over time, or place breaths by semantic focus. Breath events remain sine-only NVV approximations, and the default path is untouched so existing checksums and voices are unaffected.
