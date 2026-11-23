#!/bin/sh
# Compare a benchmark report against the checked-in expected results and RTF
# baseline. Determinism mismatches are hard failures; performance drift is only
# reported, because a single CI run is noisy. Called by .github/workflows/ci.yml.
set -eu

REPORT=${1:?usage: compare.sh REPORT EXPECTED BASELINE [THRESHOLD]}
EXPECTED=${2:?usage: compare.sh REPORT EXPECTED BASELINE [THRESHOLD]}
BASELINE=${3:?usage: compare.sh REPORT EXPECTED BASELINE [THRESHOLD]}
THRESHOLD=${4:-0.30}
SUMMARY=${GITHUB_STEP_SUMMARY:-}
OUTPUT=${GITHUB_OUTPUT:-}

RESULT=$(awk -F '\t' -v threshold="$THRESHOLD" '
    FILENAME == ARGV[1] && $1 !~ /^#/ && $1 != "case" && NF > 1 {
        expectedChecksum[$1] = $12
        expectedF0[$1] = $9
        next
    }
    FILENAME == ARGV[2] && $1 !~ /^#/ && NF > 1 {
        baselineRtf[$1] = $2 + 0
        next
    }
    FILENAME == ARGV[3] && $1 !~ /^#/ && $1 != "case" && NF > 1 {
        name = $1
        checksum = $12
        status = $13
        rtf = $7 + 0
        if (status != "ok" || checksum != expectedChecksum[name] ||
            $9 != expectedF0[name]) {
            printf "FAIL\t%s\tchecksum or complexity mismatch\n", name
            failed = 1
        }
        if (!(name in baselineRtf)) {
            printf "FAIL\t%s\tno baseline entry\n", name
            failed = 1
            next
        }
        base = baselineRtf[name]
        delta = base > 0 ? (rtf - base) / base : 0
        printf "ROW\t%s\t%.2f\t%.2f\t%+.1f%%\n", name, base, rtf, 100 * delta
        if (delta < -threshold) {
            printf "FLAG\t%s\n", name
            flagged = 1
        }
    }
    END {
        if (failed) exit 1
        if (flagged) print "REGRESSED\tyes"
        else print "REGRESSED\tno"
    }
' "$EXPECTED" "$BASELINE" "$REPORT")

printf '%s\n' "$RESULT"

if [ -n "$SUMMARY" ]; then
    {
        printf '### Benchmark real-time factor\n\n'
        printf '| case | baseline RTF | current RTF | change |\n'
        printf '| --- | ---: | ---: | ---: |\n'
        printf '%s\n' "$RESULT" | awk -F '\t' '$1 == "ROW" {
            printf "| %s | %s | %s | %s |\n", $2, $3, $4, $5
        }'
        if printf '%s\n' "$RESULT" | grep -q '^REGRESSED	yes$'; then
            printf '\nRelative RTF regression beyond %.0f%% detected; see job log.\n' \
                "$(awk -v t="$THRESHOLD" 'BEGIN { print 100 * t }')"
        fi
    } >> "$SUMMARY"
fi

if [ -n "$OUTPUT" ]; then
    if printf '%s\n' "$RESULT" | grep -q '^REGRESSED	yes$'; then
        printf 'regressed=true\n' >> "$OUTPUT"
    else
        printf 'regressed=false\n' >> "$OUTPUT"
    fi
fi

if printf '%s\n' "$RESULT" | grep -q '^FAIL	'; then
    exit 1
fi
