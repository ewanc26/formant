#!/bin/sh
# scripts/formant-update.sh against a local file:// "release". No network.
set -eu
script=$(CDPATH='' cd -- "$(dirname -- "$0")/.." && pwd)/scripts/formant-update.sh
t=$(mktemp -d)
trap 'rm -rf "$t"' EXIT
name="formant-1.2.0-$(uname -s | tr '[:upper:]' '[:lower:]')-$(uname -m)"

fake() { # <path> <version>
    mkdir -p "$(dirname -- "$1")"
    printf '#!/bin/sh\necho %s\n' "$2" > "$1"
    chmod +x "$1"
}
sum() {
    if command -v sha256sum >/dev/null 2>&1; then sha256sum "$@"; else shasum -a 256 "$@"; fi
}
install() { # <version>
    rm -rf "$t/prefix"
    fake "$t/prefix/bin/formant" "$1"
    cp "$script" "$t/prefix/bin/formant-update"
}
publish() { # <version the archived binary reports>
    rm -rf "$t/stage" "$t/release"
    fake "$t/stage/$name/bin/formant" "$1"
    cp "$script" "$t/stage/$name/bin/formant-update"
    mkdir -p "$t/release/v1.2.0"
    tar -czf "$t/release/v1.2.0/$name.tar.gz" -C "$t/stage" "$name"
    (cd "$t/release/v1.2.0" && sum "$name.tar.gz" > "$name.tar.gz.sha256")
    echo '{"tag_name": "v1.2.0"}' > "$t/latest.json"
}
update() {
    FORMANT_UPDATE_API="file://$t/latest.json" FORMANT_UPDATE_BASE="file://$t/release" \
        "$t/prefix/bin/formant-update" "$@"
}
expect() { # <version>
    got=$("$t/prefix/bin/formant" --version)
    [ "$got" = "$1" ] || { echo "expected $1, found $got" >&2; exit 1; }
}

publish 1.2.0
install 1.1.9
update --check | grep -q '1.2.0 is available'
expect 1.1.9
update >/dev/null
expect 1.2.0
update | grep -q 'up to date'

install 1.10.0
update | grep -q 'up to date'
expect 1.10.0

install 1.1.9
echo "0000  $name.tar.gz" > "$t/release/v1.2.0/$name.tar.gz.sha256"
if update >/dev/null 2>&1; then echo "accepted a bad checksum" >&2; exit 1; fi
expect 1.1.9

publish 9.9.9
install 1.1.9
if update >/dev/null 2>&1; then echo "accepted the wrong binary" >&2; exit 1; fi
expect 1.1.9
echo "formant-update: all checks passed"
