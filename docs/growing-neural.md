# Growing neural prosody (#177)

Formant's default prosody is hand-tuned: every pitch and duration comes from a
voice preset, a phoneme table and explicit front-end data. That is predictable,
but a speaker- or style-specific adjustment often arrives as a stream of small
corrections (“a little higher here, a little longer there”) that should
accumulate across runs. This feature adds a **small supervised Growing Neural
Gas** that learns those corrections as bounded pitch and duration multipliers.
It is opt-in, stateful, deterministic and still sine-only: the network only
changes `Phoneme` parameters before rendering. No samples, noise buffers,
random number generators or wall-clock state are involved.

## Training and applying

```sh
# Train a fresh state from line-oriented examples and save it.
formant --neural-train style.samples --neural-save style.state --list-neural

# Continue training from that exact state, then save the grown state.
formant --neural-model style.state --neural-train more.samples \
  --neural-save style-v2.state --list-neural

# Apply the learned corrections to a render. No --neural-apply means no change.
formant --text "Hello, world." --neural-model style.state \
  --neural-apply --output learned.wav

# Blend at half strength, useful for A/B tests.
formant --text "Hello, world." --neural-model style.state \
  --neural-apply --neural-strength 0.5 --output half.wav
```

`--neural-train` may be used without `--neural-model` to start a new network.
`--neural-model` and `--neural-train` may be combined to continue training.
`--neural-save` writes the resulting state in binary mode with fixed `\n`
endings, so the same state always produces the same bytes. `--list-neural`
prints unit, edge, observation, growth and mean-error counts and exits before
any audio render. `--neural-max-units N` caps a **new** model; it is rejected
with `--neural-model` because the cap is already stored in a saved state.
`--neural-apply` requires a model or a training file, and
`--neural-strength` must be in `[0, 1]`.

## Sample format

A sample file contains one `sample` record per line. Blank lines and `#`
comments are ignored. The six fields after `sample` are the four bounded input
features followed by the two bounded targets:

```text
# sample UTTERANCE WORD BOUNDARY STRESS PITCH DURATION
sample 0.000 0.000 1.0 0.50 1.20 1.30
sample 0.500 1.000 0.5 0.67 0.95 1.10
sample 1.000 1.000 1.0 0.50 1.05 0.90
```

- `UTTERANCE` — position through the segment sequence, `0` to `1`.
- `WORD` — position through the current word (between `|` gaps), `0` to `1`.
- `BOUNDARY` — following boundary strength: `0` none, `0.5` comma, `1` a strong
  clause or utterance end. A final segment also gets `1`.
- `STRESS` — accent evidence from amplitude, relative F0 and duration, each
  mapped around neutral values and averaged into `[0, 1]`.
- `PITCH` — target multiplier for `pitchStart` and `pitchEnd`, `[0.5, 2.0]`.
- `DURATION` — target multiplier for `duration`, `[0.5, 2.0]`.

The same four features are produced by `formant::neuralFeatures()` for an
existing `std::vector<Phoneme>`, so training data can be authored by hand or
derived offline from an analysed corpus with the public metrics API. Values
outside the documented ranges are rejected with the source line number, for
example `neural line 3: sample input must be in [0,1] and target in [0.5,2]`.

## What grows

The network keeps a small set of units, each with a centre in the four-feature
space, an output pair, accumulated error and presentation count. For each
sample it finds the nearest unit and runner-up with stable index tie-breaking,
connects them with an age-tracked edge, moves the winner (and its neighbours)
towards the example, and accumulates squared input/output error. After every
`growth-interval` observations, if the highest-error unit carries more error
than `insertion-error` and the `max-units` cap has not been reached, a new unit
is inserted at the current example. Old edges age out;
incident edges are trimmed at `max-neighbors`. Unit insertion is therefore
error-driven, bounded and monotonic: training can add units but never removes
or reorders them, so earlier behaviour is preserved by construction.

Inference is a weighted mean of the `predictionNeighbors` nearest units by
inverse squared distance plus a small epsilon. Ties resolve by ascending unit
index, and results are clamped to `[0.5, 2.0]`. An empty network returns the
neutral output `{1.0, 1.0}`, so loading no model is an exact no-op.
`applyNeuralProsody()` multiplies `pitchStart`, `pitchEnd` and `duration` by the
learned corrections after strength blending. It skips silences, authored NVV
and breath segments (any phoneme with a non-empty `source`), and phonemes with
absolute `f0StartHz` targets.

## Where it sits in the pipeline

The layer runs after every text front end and after breath-group planning, but
**before** symbolic intonation and prosody-direct files. That means a trained
style supplies a soft background tendency, while an author's exact ToBI tune
or `.prosody` Hz target still wins. Because all corrections are made to the
same `Phoneme` objects the scheduler consumes, `--timing`, `--visemes`, sweeps
and A/B pairs automatically see the learned durations. Rendering itself is
unchanged: one additive sine engine, with no new runtime dependency.

## State format

`--neural-save` writes a versioned, line-oriented text state. Doubles are
formatted with 17 significant digits in the classic locale, so a saved state
round-trips exactly and has identical bytes on every platform.

```text
formant-neural 1
config winner-rate 0.1
config neighbor-rate 0.03
config error-decay 0.5
config insertion-error 0.02
config growth-interval 4
config max-units 64
config max-neighbors 4
config max-edge-age 24
config prediction-neighbors 3
observations 12
growths 3
last-growth-observation 10
unit 0 0 0 0 0 1.2 1.3 0 1 0
edge 0 1 0
```

The header, nine config keys and three counters are required; unit indices must
be sequential from zero and edge endpoints must exist. Comments (`#`) and blank
lines are allowed. Every parse error is prefixed with its 1-based line number,
for example `neural line 15: unknown directive 'bogus'`. The state intentionally
contains no audio, timing weight strings or binary blobs.

## Library API

```cpp
#include <formant/formant.h>

formant::NeuralConfig config;
config.maxUnits = 32;
formant::GrowingNeuralNetwork network(config);
network.train(formant::parseNeuralSamples(fileContents));

const auto [pitchScale, durationScale] =
    network.predict(formant::neuralFeatures(phonemes, 7));
formant::applyNeuralProsody(phonemes, network, 0.75);

const std::string state = network.serialize();
auto restored = formant::GrowingNeuralNetwork::parse(state);
```

`GrowingNeuralNetwork::observe()` returns `true` when it inserted a unit;
`train()` is a convenience loop over ordered samples. `summary()` exposes the
same counters printed by `--list-neural`. All operations are value-in,
value-out; there is no global model registry and no hidden state between
renders. The `NeuralUnit`, `NeuralEdge`, `NeuralSample` and `NeuralSummary`
structs are public so embedders can inspect or author state without a binary
format.

## Determinism and limits

Training is ordered and stateful: the same saved state plus the same sample
stream always yields the same state bytes and the same rendered audio. The
architecture is deliberately small — four features, two outputs, at most 4096
units — and is a style adapter, not a speech recogniser or a language model. It
does not infer phonemes, learn new sounds, track identity across processes, or
change the sine-only signal model. New languages and voices can reuse one
learnt adjustment only when their feature vectors are comparable; otherwise
train a separate state file.

## Research

- Fritzke, *A Growing Neural Gas Network Learns Topologies*
  ([overview](https://demogng.de/JavaPaper/node19.html)) — error-driven unit
  insertion and age-tracked topological edges.
- GROWN, *Growing When Necessary*
  ([arXiv:2110.00908](https://arxiv.org/abs/2110.00908)) — grow only when the
  current representation cannot explain a new observation, preserving earlier
  knowledge instead of retraining from scratch.

The implementation follows those ideas in their smallest useful form for a
parameter-domain prosody layer. It is not a claim of biological plausibility
or of general continual learning: the unit cap is hard, insertion is
monotonic, and the only learned outputs are two bounded multipliers.
