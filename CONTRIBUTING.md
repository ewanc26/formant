# Contributing to formant

A customisable sine-wave voice synthesis engine.

## Project context

- Primary language: C++20
- Default branch: main
- No third-party runtime dependencies

## Before submitting changes

- Read the README, `docs/design.md`, CMakeLists.txt and CI workflow before choosing commands.
- Keep the engine dependency-free. New dependencies need an explicit discussion first.
- Preserve determinism: no `rand()`, wall-clock time or global mutable state in rendering.
- Keep phoneme data in the table in `src/phoneme.cpp`; do not hardcode formant values in the synthesiser.
- Add or update tests in `tests/formant_tests.cpp` when behaviour changes.
- Run the build and `ctest --test-dir build --output-on-failure` before opening a PR.
- Keep commits focused. Follow Conventional Commits (`feat:`, `fix:`, `docs:`, `test:`, `build:`, `ci:`).
- Open changes through a pull request with the verification results.

## Recent direction

Initial library and CLI: sine-only formant synthesis, six voice presets, English text fallback, WAV output and a small test suite.

## Licence

AGPL-3.0. See [LICENSE](LICENSE).
