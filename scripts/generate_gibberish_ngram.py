#!/usr/bin/env python3
"""Generate formant gibberish n-gram tables from CMUdict.

Offline only. This script downloads/reads CMUdict and emits a compact plain-text
ngram table for formant's --gibberish-style mode. The output is committed; normal
builds and tests do not run this script.

Provenance
----------
Source: CMU Pronouncing Dictionary (cmudict), Carnegie Mellon University.
Upstream: https://github.com/cmusphinx/cmudict
File used: cmudict.dict (ARPABET pronunciations)
License: BSD-2-Clause (see data/gibberish/PROVENANCE.md and CMUdict LICENSE).

The generated table contains derived phoneme transition statistics, not the
original dictionary entries.
"""
from __future__ import annotations

import argparse
import collections
import pathlib
import re
import sys
import urllib.request

CMUDICT_URL = "https://raw.githubusercontent.com/cmusphinx/cmudict/master/cmudict.dict"

# CMUdict ARPABET -> formant phoneme names. Formant's English phoneme set is
# intentionally small; this mapping keeps output inside that inventory.
ARPABET_TO_FORMANT = {
    "AA": "aa",
    "AE": "a",
    "AH": "uh",  # AH0/AH1/AH2 collapsed by stress stripping; schwa-like default
    "AO": "or",
    "AW": "ow",
    "AY": "ay",
    "B": "b",
    "CH": "ch",
    "D": "d",
    "DH": "dh",
    "EH": "e",
    "ER": "er",
    "EY": "ay",  # closest existing diphthong in formant inventory
    "F": "f",
    "G": "g",
    "HH": "h",
    "IH": "i",
    "IY": "ee",
    "JH": "j",
    "K": "k",
    "L": "l",
    "M": "m",
    "N": "n",
    "NG": "ng",
    "OW": "o",
    "OY": "oy",
    "P": "p",
    "R": "r",
    "S": "s",
    "SH": "sh",
    "T": "t",
    "TH": "th",
    "UH": "oo",  # short / book vowel; closest existing rounded vowel
    "UW": "oo",
    "V": "v",
    "W": "w",
    "Y": "y",
    "Z": "z",
    "ZH": "zh",
}

WORD_RE = re.compile(r"^[a-z']+$")


def load_cmudict(path: pathlib.Path | None) -> list[list[str]]:
    if path is None:
        data = urllib.request.urlopen(CMUDICT_URL, timeout=60).read().decode("utf-8", errors="replace")
    else:
        data = path.read_text(encoding="utf-8", errors="replace")
    words: list[list[str]] = []
    for raw in data.splitlines():
        line = raw.strip()
        if not line or line.startswith(";;;"):
            continue
        parts = line.split()
        if len(parts) < 2:
            continue
        word = parts[0]
        phones = " ".join(parts[1:])
        # Skip variant markers like WORD(1), punctuation tokens, abbreviations.
        base = word.split("(")[0].lower()
        if not WORD_RE.match(base):
            continue
        if len(base) < 2:
            continue
        mapped: list[str] = []
        ok = True
        for p in phones.split():
            p = re.sub(r"[0-9]", "", p.upper())
            if p not in ARPABET_TO_FORMANT:
                ok = False
                break
            mapped.append(ARPABET_TO_FORMANT[p])
        if not ok or not mapped:
            continue
        # Bound word length to avoid pathological entries.
        if len(mapped) > 12:
            continue
        words.append(mapped)
    return words


def build_tables(words: list[list[str]]) -> dict:
    starts = collections.Counter()
    ends = collections.Counter()
    unigrams = collections.Counter()
    bigrams: dict[str, collections.Counter] = collections.defaultdict(collections.Counter)
    trigrams: dict[tuple[str, str], collections.Counter] = collections.defaultdict(collections.Counter)
    lengths = collections.Counter()
    for phones in words:
        lengths[len(phones)] += 1
        starts[phones[0]] += 1
        ends[phones[-1]] += 1
        for i, p in enumerate(phones):
            unigrams[p] += 1
            if i + 1 < len(phones):
                bigrams[p][phones[i + 1]] += 1
            if i + 2 < len(phones):
                trigrams[(phones[i], phones[i + 1])][phones[i + 2]] += 1
    return {
        "starts": starts,
        "ends": ends,
        "unigrams": unigrams,
        "bigrams": bigrams,
        "trigrams": trigrams,
        "lengths": lengths,
    }


def prune_counter(counter: collections.Counter, min_count: int, top: int | None = None) -> list[tuple[str, int]]:
    items = [(k, v) for k, v in counter.items() if v >= min_count]
    items.sort(key=lambda kv: (-kv[1], kv[0]))
    if top is not None:
        items = items[:top]
    return items


def emit(path: pathlib.Path, lang: str, tables: dict, source: str) -> None:
    starts = prune_counter(tables["starts"], 5, 64)
    ends = prune_counter(tables["ends"], 5, 64)
    lengths = prune_counter(collections.Counter({str(k): v for k, v in tables["lengths"].items()}), 10, 12)
    unigrams = prune_counter(tables["unigrams"], 1)

    # Keep tables compact but representative.
    bigram_rows = []
    for prev, counter in tables["bigrams"].items():
        items = prune_counter(counter, 10, 16)
        if items:
            bigram_rows.append((prev, items))
    bigram_rows.sort(key=lambda r: (-sum(v for _, v in r[1]), r[0]))

    trigram_rows = []
    for (p1, p2), counter in tables["trigrams"].items():
        items = prune_counter(counter, 8, 12)
        if items:
            trigram_rows.append((p1, p2, items))
    trigram_rows.sort(key=lambda r: (-sum(v for _, v in r[2]), r[0], r[1]))

    with path.open("w", encoding="utf-8", newline="\n") as f:
        f.write("# formant gibberish n-gram table v1\n")
        f.write(f"lang {lang}\n")
        f.write(f"source {source}\n")
        f.write("license BSD-2-Clause (CMUdict)\n")
        f.write("mapping arpa-to-formant-v1\n")
        f.write("note Derived phoneme transition statistics; not original dictionary entries.\n")
        f.write("begin lengths\n")
        for k, v in lengths:
            f.write(f"{k} {v}\n")
        f.write("end lengths\n")
        f.write("begin starts\n")
        for k, v in starts:
            f.write(f"{k} {v}\n")
        f.write("end starts\n")
        f.write("begin ends\n")
        for k, v in ends:
            f.write(f"{k} {v}\n")
        f.write("end ends\n")
        f.write("begin unigrams\n")
        for k, v in unigrams:
            f.write(f"{k} {v}\n")
        f.write("end unigrams\n")
        f.write("begin bigrams\n")
        for prev, items in bigram_rows:
            rhs = " ".join(f"{n}:{c}" for n, c in items)
            f.write(f"{prev}\t{rhs}\n")
        f.write("end bigrams\n")
        f.write("begin trigrams\n")
        for p1, p2, items in trigram_rows:
            rhs = " ".join(f"{n}:{c}" for n, c in items)
            f.write(f"{p1} {p2}\t{rhs}\n")
        f.write("end trigrams\n")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--cmudict", type=pathlib.Path, help="Local cmudict.dict path")
    parser.add_argument("--output", type=pathlib.Path, required=True)
    parser.add_argument("--lang", default="en-us")
    args = parser.parse_args()
    words = load_cmudict(args.cmudict)
    if len(words) < 1000:
        print(f"too few words parsed: {len(words)}", file=sys.stderr)
        return 1
    tables = build_tables(words)
    source = "CMUdict cmudict.dict (Carnegie Mellon University, BSD-2-Clause)"
    emit(args.output, args.lang, tables, source)
    print(f"wrote {args.output} from {len(words)} words")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
