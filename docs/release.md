# Releasing

Formant releases are LLVM-free, dependency-free tarballs plus a source archive, published from a `vX.Y.Z` tag.

## Process

1. Move the pending entries in `CHANGELOG.md` under a new `## [X.Y.Z] - YYYY-MM-DD` heading.
2. Bump `VERSION` in the `project()` block of `CMakeLists.txt` to the same value.
3. Verify locally:

   ```sh
   cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
   cmake --build build -j
   ctest --test-dir build --output-on-failure
   ./build/formant --version
   ```

4. Commit and merge through a pull request.
5. Tag the merge commit and push the tag:

   ```sh
   git tag -a vX.Y.Z -m "vX.Y.Z"
   git push origin vX.Y.Z
   ```

The `Release` workflow checks that the tag matches the CMake version, creates the GitHub release with generated notes, then builds and attaches:

- `formant-X.Y.Z-source.tar.gz`
- `formant-X.Y.Z-linux-x86_64.tar.gz`
- `formant-X.Y.Z-darwin-arm64.tar.gz` (or the relevant macOS architecture)

Each platform archive contains the CLI, the static library, public headers, README, changelog and licence.

## Manual releases

A release can be created without the workflow if needed:

```sh
version=X.Y.Z
git archive --format=tar.gz --prefix="formant-$version/" -o "formant-$version-source.tar.gz" v$version
gh release create v$version --title v$version --generate-notes
gh release upload v$version "formant-$version-source.tar.gz"
```
