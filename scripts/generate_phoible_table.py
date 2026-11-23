#!/usr/bin/env python3
"""Build the committed PHOIBLE conformance table (issue #98).

This is an offline, development-time script. It never runs as part of the
engine or its tests. Supply the PHOIBLE 2.0 segment-feature TSV:

    python3 scripts/generate_phoible_table.py \
        --features /path/to/phoible-segments-features.tsv \
        --output data/phoible/segments.tsv

The TSV is the "phoible-segments-features.tsv" file from the PHOIBLE dev
repository (https://github.com/phoible/dev). See data/phoible/PROVENANCE.md.

For every PHOIBLE segment the script records:
  glyph     the original PHOIBLE segment symbol (UTF-8)
  category  vowel | pulmonic | non-pulmonic | diacritic (from PHOIBLE features)
  ipa       the transcription used for the conformance render
  source    "direct" when glyph is used unchanged, "approx" otherwise

"direct" means the Formant IPA front end accepts the PHOIBLE symbol itself;
"approx" rows apply the documented, deterministic approximation policy below.
The C++ conformance test is authoritative: it parses every `ipa` value and
requires a non-silent render, so an incorrect approximation fails CI.

Approximation policy (all rules are plain, reversible text substitutions):
  1. PHOIBLE-specific notation is dropped or mapped to a nearby IPA letter:
       *  -> ""        (donor-database placeholder)
       R  -> "ʁ"       (capital R used by UPSID for a uvular rhotic)
       ᴅ -> "d"       (small capital D)
       ᴱ -> ""        (modifier capital E, an editorial suprasegmental)
       ‼ -> ""        (donor ejective marker; IPA ʼ is handled directly)
       ↓ -> ""        (donor "downstep" marker)
  2. Unsupported secondary articulations and diacritics are dropped, with the
     base letter kept (the acoustic distinction is not modelled):
       ʱ (breathy), ˀ (glottalized), ᶣ (labial-velar),
       U+0327 cedilla, U+0347-0349 and U+0353 (rare placing diacritics).
  3. Tone- and stress-only symbols (which carry no segment in PHOIBLE) are
     given a neutral carrier vowel "a", e.g. "˦" -> "a˦", "˥˩" -> "a˥˩".
  4. A segment that becomes empty after the rules above uses "ə" (schwa).

The policy is deliberately conservative: it never invents new base letters,
it only removes unsupported modifiers or supplies a neutral carrier.
"""

from __future__ import annotations

import argparse
import csv
import unicodedata
from pathlib import Path

# PHOIBLE-specific notation and unsupported secondary articulations.
_DROP = {
    "*": "",
    "ᴱ": "",
    "‼": "",
    "↓": "",
    "ʱ": "",
    "ˀ": "",
    "ᶣ": "",
    "\u0327": "",  # COMBINING CEDILLA
    "\u0347": "",  # COMBINING EQUALS SIGN BELOW
    "\u0348": "",  # COMBINING DOUBLE VERTICAL LINE BELOW
    "\u0349": "",  # COMBINING LEFT ANGLE BELOW
    "\u0353": "",  # COMBINING X BELOW
}
_MAP = {
    "R": "ʁ",
    "ᴅ": "d",
}

# Codepoints that carry no base segment on their own: the Spacing Modifier
# Letters block (secondary articulations, stress, tone bars, length marks).
# If a transformed glyph contains only these plus combining marks, it needs a
# neutral carrier vowel.
_SUPRA = set(range(0x02B0, 0x0300))


def category(row: dict[str, str]) -> str:
    """Map PHOIBLE distinctive features to the four reported classes."""
    if row.get("tone") == "+":
        return "diacritic"
    if (row.get("click") == "+"
            or row.get("raisedLarynxEjective") == "+"
            or row.get("loweredLarynxImplosive") == "+"):
        return "non-pulmonic"
    if row.get("consonantal") == "+":
        return "pulmonic"
    if row.get("syllabic") == "+":
        return "vowel"
    return "diacritic"


def is_suprasegmental_only(text: str) -> bool:
    """True when text has no base segment (tone/stress/length marks only)."""
    for char in text:
        code = ord(char)
        if code in _SUPRA or unicodedata.category(char) == "Mn":
            continue
        return False
    return bool(text)


def approximate(glyph: str) -> tuple[str, str]:
    """Return (ipa, source) for one PHOIBLE glyph."""
    out = "".join(_MAP.get(char, _DROP.get(char, char)) for char in glyph)
    changed = out != glyph
    if is_suprasegmental_only(out):
        out = "a" + out
        changed = True
    if not out:
        out = "ə"
        changed = True
    return out, ("approx" if changed else "direct")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--features", required=True, type=Path,
                        help="PHOIBLE phoible-segments-features.tsv")
    parser.add_argument("--output", default=Path("data/phoible/segments.tsv"),
                        type=Path, help="committed table to write")
    parser.add_argument("--report", default=Path("data/phoible/coverage.txt"),
                        type=Path, help="committed coverage summary to write")
    args = parser.parse_args()

    with args.features.open(encoding="utf-8") as handle:
        rows = list(csv.DictReader(handle, delimiter="\t"))
    if not rows:
        raise SystemExit("no segments in features file")

    args.output.parent.mkdir(parents=True, exist_ok=True)
    seen: set[str] = set()
    summary: list[tuple[str, str, str, str]] = []
    with args.output.open("w", encoding="utf-8", newline="\n") as handle:
        writer = csv.writer(handle, delimiter="\t", lineterminator="\n")
        writer.writerow(["glyph", "category", "ipa", "source"])
        for row in rows:
            glyph = row["segment"]
            if glyph in seen:
                raise SystemExit(f"duplicate segment: {glyph!r}")
            seen.add(glyph)
            ipa, source = approximate(glyph)
            writer.writerow([glyph, category(row), ipa, source])
            summary.append((glyph, category(row), ipa, source))

    args.report.parent.mkdir(parents=True, exist_ok=True)
    with args.report.open("w", encoding="utf-8", newline="\n") as handle:
        handle.write("# PHOIBLE 2.0 segment coverage (generated)\n")
        handle.write("# category\ttotal\tdirect\tapprox\tdirect_percent\n")
        for name in ("vowel", "pulmonic", "non-pulmonic", "diacritic", "ALL"):
            group = summary if name == "ALL" else [
                item for item in summary if item[1] == name]
            if not group:
                continue
            total = len(group)
            direct = sum(item[3] == "direct" for item in group)
            handle.write(f"{name}\t{total}\t{direct}\t{total - direct}\t"
                         f"{100.0 * direct / total:.1f}\n")
    print(f"wrote {len(seen)} segments to {args.output}")
    print(f"wrote coverage summary to {args.report}")


if __name__ == "__main__":
    main()
