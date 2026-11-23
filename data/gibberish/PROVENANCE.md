# Gibberish n-gram table provenance

This directory contains **derived** phoneme transition statistics used by
`--gibberish-style`. The committed `.ngram` files store aggregate counts and
transition frequencies, not the original dictionary entries.

## `en-us.ngram`

- **Source**: CMU Pronouncing Dictionary (`cmudict.dict`)
- **Upstream**: <https://github.com/cmusphinx/cmudict>
- **Copyright**: Copyright (C) 1993-2015 Carnegie Mellon University. All rights reserved.
- **License**: BSD-2-Clause-style license; the full CMUdict license text is reproduced below.
- **Generation**: `scripts/generate_gibberish_ngram.py --cmudict /path/to/cmudict.dict --output data/gibberish/en-us.ngram --lang en-us`
- **Transform**: dictionary words were filtered to plain alphabetic/apostrophe entries, pronunciations were mapped from ARPABET to Formant's English phoneme inventory, and aggregate start/end/unigram/bigram/trigram/length counts were emitted. No original word list is included.

### ARPABET to Formant mapping

The mapping intentionally targets Formant's existing phoneme inventory so
generated words can be rendered by the normal pipeline:

| ARPABET | Formant | Note |
|---|---|---|
| AA | `aa` | |
| AE | `a` | |
| AH | `uh` | stress collapsed; schwa-like default |
| AO | `or` | |
| AW | `ow` | |
| AY | `ay` | |
| B/D/F/G/H/K/L/M/N/P/R/S/T/V/W/Y/Z | same | direct consonants |
| CH | `ch` | |
| DH | `dh` | |
| EH | `e` | |
| ER | `er` | |
| EY | `ay` | nearest existing diphthong |
| IH | `i` | |
| IY | `ee` | |
| JH | `j` | |
| NG | `ng` | |
| OW | `o` | |
| OY | `oy` | |
| SH | `sh` | |
| TH | `th` | |
| UH | `oo` | nearest existing rounded vowel |
| UW | `oo` | |
| ZH | `zh` | |

## CMUdict license

```text
Copyright (C) 1993-2015 Carnegie Mellon University. All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions
are met:

1. Redistributions of source code must retain the above copyright
   notice, this list of conditions and the following disclaimer.
   The contents of this file are deemed to be source code.

2. Redistributions in binary form must reproduce the above copyright
   notice, this list of conditions and the following disclaimer in
   the documentation and/or other materials provided with the
   distribution.

This work was supported in part by funding from the Defense Advanced
Research Projects Agency, the Office of Naval Research and the National
Science Foundation of the United States of America, and by member
companies of the Carnegie Mellon Sphinx Speech Consortium. We acknowledge
the contributions of many volunteers to the expansion and improvement of
this dictionary.

THIS SOFTWARE IS PROVIDED BY CARNEGIE MELLON UNIVERSITY ``AS IS'' AND
ANY EXPRESSED OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL CARNEGIE MELLON UNIVERSITY
NOR ITS EMPLOYEES BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
```
