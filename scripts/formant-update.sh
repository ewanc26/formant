#!/bin/sh
# Update a formant install from this project's GitHub releases.
#
#   formant-update           install the newest release
#   formant-update --check   report it without changing anything
#
# It updates the prefix it is installed in (<prefix>/bin/formant-update). The
# archive is checked against the .sha256 published beside it, which catches a
# corrupted download but is not a signature.
set -eu

api=${FORMANT_UPDATE_API:-https://api.github.com/repos/ewanc26/formant/releases/latest}
base=${FORMANT_UPDATE_BASE:-https://github.com/ewanc26/formant/releases/download}

fail() { echo "formant-update: $*" >&2; exit 1; }

check=0
case "${1-}" in
    '') ;;
    --check) check=1 ;;
    -h|--help) echo "usage: formant self-update [--check]"; exit 0 ;;
    *) fail "unknown option $1" ;;
esac

bindir=$(CDPATH='' cd -- "$(dirname -- "$0")" && pwd -P)
prefix=$(dirname -- "$bindir")
[ -x "$bindir/formant" ] || fail "no formant beside this script in $bindir"
current=$("$bindir/formant" --version) || fail "cannot read the installed version"

command -v curl >/dev/null 2>&1 || fail "curl is needed"
latest=$(curl -fsSL --max-time 20 "$api" |
    sed -n 's/.*"tag_name"[[:space:]]*:[[:space:]]*"v\([0-9][0-9]*\.[0-9][0-9]*\.[0-9][0-9]*\)".*/\1/p' |
    head -n 1) || fail "cannot reach GitHub"
[ -n "$latest" ] || fail "no release version found"

newer=$(awk -v a="$latest" -v b="$current" 'BEGIN {
    split(a, x, "."); split(b, y, ".")
    for (i = 1; i <= 3; i++) if (x[i] + 0 != y[i] + 0) { print (x[i] + 0 > y[i] + 0); exit }
    print 0 }')
if [ "$newer" != 1 ]; then
    echo "formant $current is up to date."
    exit 0
fi
echo "formant $latest is available (installed: $current)."
[ "$check" = 0 ] || exit 0

case "$prefix/" in
    */Cellar/*|/nix/store/*) fail "this install belongs to a package manager; update it there" ;;
esac
[ ! -e "$bindir/CMakeCache.txt" ] || fail "this is a build directory; rebuild from source instead"
[ -w "$bindir" ] || fail "$bindir is not writable"

system=$(uname -s | tr '[:upper:]' '[:lower:]')
name="formant-$latest-$system-$(uname -m)"
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
curl -fsSL --max-time 300 -o "$work/$name.tar.gz" "$base/v$latest/$name.tar.gz" ||
    fail "no download for this platform ($name.tar.gz)"
curl -fsSL --max-time 60 -o "$work/sum" "$base/v$latest/$name.tar.gz.sha256" ||
    fail "the release has no checksum for $name.tar.gz"

expected=$(awk '{ print $1; exit }' "$work/sum")
if command -v sha256sum >/dev/null 2>&1; then
    actual=$(sha256sum "$work/$name.tar.gz" | awk '{ print $1 }')
else
    actual=$(shasum -a 256 "$work/$name.tar.gz" | awk '{ print $1 }')
fi
[ -n "$expected" ] && [ "$expected" = "$actual" ] || fail "checksum mismatch; nothing was changed"

tar -tzf "$work/$name.tar.gz" > "$work/list" || fail "cannot read the archive"
if grep -v "^$name/" "$work/list" >/dev/null || grep -E '(^|/)\.\.(/|$)' "$work/list" >/dev/null; then
    fail "the archive has unexpected paths; nothing was changed"
fi
tar -xzf "$work/$name.tar.gz" -C "$work"
new="$work/$name"
[ "$("$new/bin/formant" --version)" = "$latest" ] ||
    fail "the downloaded formant does not run as $latest; nothing was changed"

# Each file is renamed into place, so a running copy keeps its old contents.
replace() {
    mkdir -p "$(dirname -- "$2")"
    cp "$1" "$2.new"
    mv -f "$2.new" "$2"
}
if [ -f "$new/lib/libformant_core.a" ] && [ -f "$prefix/lib/libformant_core.a" ]; then
    replace "$new/lib/libformant_core.a" "$prefix/lib/libformant_core.a"
fi
if [ -d "$prefix/include/formant" ]; then
    for header in "$new"/include/formant/*; do
        replace "$header" "$prefix/include/formant/$(basename -- "$header")"
    done
fi
replace "$new/bin/formant" "$bindir/formant"
if [ -f "$new/bin/formant-update" ]; then
    replace "$new/bin/formant-update" "$bindir/formant-update"
fi
echo "formant updated to $latest in $prefix."
