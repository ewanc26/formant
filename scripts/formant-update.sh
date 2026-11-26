#!/bin/sh
# Update a formant install from this project's GitHub releases.
#
#   formant-update           install the newest release
#   formant-update --check   report it without changing anything
#   formant-update --notify VERSION
#                            run by formant itself: print a cached notice and,
#                            at most once a day, look for a release in the
#                            background
#
# It updates the prefix it is installed in (<prefix>/bin/formant-update). The
# archive is checked against the .sha256 published beside it, which catches a
# corrupted download but is not a signature.
set -eu

api=${FORMANT_UPDATE_API:-https://api.github.com/repos/ewanc26/formant/releases/latest}
base=${FORMANT_UPDATE_BASE:-https://github.com/ewanc26/formant/releases/download}

fail() { echo "formant-update: $*" >&2; exit 1; }

latest_release() {
    curl -fsSL --max-time 20 "$api" |
        sed -n 's/.*"tag_name"[[:space:]]*:[[:space:]]*"v\([0-9][0-9]*\.[0-9][0-9]*\.[0-9][0-9]*\)".*/\1/p' |
        head -n 1
}

# Prints 1 when version $1 is higher than version $2.
is_newer() {
    awk -v a="$1" -v b="$2" 'BEGIN {
        split(a, x, "."); split(b, y, ".")
        for (i = 1; i <= 3; i++) if (x[i] + 0 != y[i] + 0) { print (x[i] + 0 > y[i] + 0); exit }
        print 0 }'
}

check=0
notify=0
case "${1-}" in
    '') ;;
    --check) check=1 ;;
    --notify) notify=1; installed=${2-} ;;
    -h|--help) echo "usage: formant self-update [--check]"; exit 0 ;;
    *) fail "unknown option $1" ;;
esac

bindir=$(CDPATH='' cd -- "$(dirname -- "$0")" && pwd -P)
prefix=$(dirname -- "$bindir")

# Whether this install is one that self-update may overwrite.
refusal() {
    case "$prefix/" in
        */Cellar/*|/nix/store/*)
            echo "this install belongs to a package manager; update it there"; return ;;
    esac
    if [ -e "$bindir/CMakeCache.txt" ]; then
        echo "this is a build directory; rebuild from source instead"
    elif [ ! -w "$bindir" ]; then
        echo "$bindir is not writable"
    fi
}

if [ "$notify" = 1 ]; then
    [ -z "${CI-}${NO_UPDATE_NOTIFIER-}${FORMANT_NO_UPDATE_CHECK-}" ] || exit 0
    [ -n "$installed" ] && [ -z "$(refusal)" ] || exit 0
    state=${FORMANT_STATE_DIR:-${XDG_CACHE_HOME:-${HOME:?}/.cache}/formant}
    latest=$(cat "$state/latest" 2>/dev/null) || latest=
    if [ -n "$latest" ] && [ "$(is_newer "$latest" "$installed")" = 1 ]; then
        echo "formant $latest is available (installed: $installed). Run \`formant self-update\`." >&2
    fi
    now=$(date +%s)
    checked=$(cat "$state/checked" 2>/dev/null) || checked=0
    case $checked in ''|*[!0-9]*) checked=0 ;; esac
    [ $((now - checked)) -ge 86400 ] || exit 0
    # Recorded first, so that a failing check is not retried on every run.
    mkdir -p "$state" 2>/dev/null && echo "$now" 2>/dev/null > "$state/checked" || exit 0
    (
        # Outlive the terminal if formant was the last thing running in it.
        trap '' HUP
        found=$(latest_release) && [ -n "$found" ] && echo "$found" > "$state/latest"
    ) </dev/null >/dev/null 2>&1 &
    exit 0
fi

[ -x "$bindir/formant" ] || fail "no formant beside this script in $bindir"
current=$("$bindir/formant" --version) || fail "cannot read the installed version"

command -v curl >/dev/null 2>&1 || fail "curl is needed"
latest=$(latest_release) || fail "cannot reach GitHub"
[ -n "$latest" ] || fail "no release version found"

if [ "$(is_newer "$latest" "$current")" != 1 ]; then
    echo "formant $current is up to date."
    exit 0
fi
echo "formant $latest is available (installed: $current)."
[ "$check" = 0 ] || exit 0

reason=$(refusal)
[ -z "$reason" ] || fail "$reason"

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
