# Releasing

Formant releases are dependency-free tarballs plus a source archive, published from a `vX.Y.Z` tag.

## Process

1. Add the pending changes to `CHANGELOG.md` under `## [Unreleased]`.
2. Move that section under a dated `## [X.Y.Z] - YYYY-MM-DD` heading.
3. Bump `VERSION` in the `project()` block of `CMakeLists.txt` to the same value.
4. Verify locally:

   ```sh
   cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
   cmake --build build -j
   ctest --test-dir build --output-on-failure
   ./build/formant --version
   ```

   `formant_cli_render` asserts that `--version` matches the CMake project version.

5. Commit and merge through a pull request.
6. Tag the merge commit and push the tag:

   ```sh
   git tag -a vX.Y.Z -m "vX.Y.Z"
   git push origin vX.Y.Z
   ```

The `Release` workflow checks that the tag matches the CMake version, creates the GitHub release using the matching `CHANGELOG.md` section as notes, then builds and attaches:

- `formant-X.Y.Z-source.tar.gz`
- `formant-X.Y.Z-linux-x86_64.tar.gz`
- `formant-X.Y.Z-darwin-arm64.tar.gz` (or the relevant macOS architecture)
- `SHA256SUMS`
- one `<archive>.sha256` file per archive

Each platform archive contains the CLI, the static library, public headers, README, changelog and licence.

If the matching changelog section is missing, the workflow fails before creating the release. This keeps published notes deliberate rather than generated from commit subjects.

## Verifying a release

Download the archives and checksum file, then run:

```sh
gh release download vX.Y.Z --dir release --pattern 'formant-*.tar.gz' --pattern 'SHA256SUMS'
cd release
sha256sum --check SHA256SUMS
```

## Manual releases

A release can be created without the workflow if needed:

```sh
version=X.Y.Z
git archive --format=tar.gz --prefix="formant-$version/" -o "formant-$version-source.tar.gz" v$version
awk -v v="$version" '
  index($0, "## [" v "]") == 1 { found = 1; next }
  found && index($0, "## [") == 1 { exit }
  found { print }
' CHANGELOG.md > release-notes.md
gh release create v$version --title v$version --notes-file release-notes.md "formant-$version-source.tar.gz"
```

After all archives are attached, regenerate `SHA256SUMS` and the per-archive `.sha256` files from the downloaded assets and upload them with `gh release upload --clobber`.
