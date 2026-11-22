# AGENTS.md

Guidance for AI coding agents working in **formant**.

## Project overview

A customisable C++20 voice synthesis engine that produces speech from sine waves only.

- Language: C++
- Default branch: main
- No third-party dependencies

## Working rules

- Inspect the README, `docs/design.md`, CMakeLists.txt and nearby code before editing.
- Preserve the text → phonemes → formant targets → sine rendering → PCM pipeline.
- Keep rendering deterministic: no random seeds from time, no global mutable state.
- Keep formant data in the phoneme table rather than in the synthesiser.
- Update tests and documentation when behaviour changes.
- Use feature branches and pull requests.
- Verify with `cmake --build build -j` and `ctest --test-dir build --output-on-failure`.
- Treat generated audio, credentials and local configuration as untracked.

## Agent attribution

Credit the tool actually used. Do not claim a different agent, and do not add a blanket ban on tools other agents may use.

## Recent history

Initial implementation: additive sine synthesis with formant shaping, six voice presets, a small English lexicon, direct phoneme input and WAV output.
