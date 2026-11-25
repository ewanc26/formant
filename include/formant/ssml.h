#pragma once

// Minimal SSML input for universal control. This is a dependency-free reader
// for the subset documented in docs/ssml.md; it produces the same Phoneme
// sequence the other front ends use, so every rendered sample still comes from
// the deterministic sine engine. Unsupported elements are rejected loudly with
// the element name and byte position rather than being silently ignored.

#include <functional>
#include <map>
#include <string>
#include <string_view>
#include <vector>

#include "formant/phoneme.h"

namespace formant {

// Converts plain text inside an SSML document to phonemes. Callers supply this
// so SSML composes with the selected language front end, a language definition
// or a pronunciation lexicon. The callback is invoked once per text run.
using SsmlTextFrontEnd =
    std::function<std::vector<Phoneme>(std::string_view text)>;

// Resolves <nvv name="..."/> to a phoneme sequence. `offset` is the byte
// position of the element, included in error messages for unknown names.
using SsmlNvvResolver =
    std::function<std::vector<Phoneme>(std::string_view name, size_t offset)>;

// Word-level emphasis levels (#125). The affect data file supplies these
// multipliers; callers without an affect profile get the shipped defaults.
struct SsmlEmphasisLevel {
    double pitch = 1.22;
    double duration = 1.30;
    double amplitude = 1.28;
};
using SsmlEmphasisLevels = std::map<std::string, SsmlEmphasisLevel>;

// Parses an SSML document (bare text without a <speak> wrapper is accepted)
// into the shared Phoneme sequence. Inline <prosody> composes on top of the
// voice-level rate/pitch/gain by scaling each phoneme's duration, amplitude and
// pitch contour. Throws std::invalid_argument naming the element and byte
// position for unsupported or malformed markup.
std::vector<Phoneme> phonemizeSsml(std::string_view ssml,
                                   const SsmlTextFrontEnd &phonemizeText);

// As above, with explicit <nvv name="..."/> support for nonverbal events.
std::vector<Phoneme> phonemizeSsml(std::string_view ssml,
                                   const SsmlTextFrontEnd &phonemizeText,
                                   const SsmlNvvResolver &resolveNvv);

// As above, with data-defined <emphasis level="..."> multipliers. Supplied
// levels overlay the shipped defaults, so a file only needs to change one.
std::vector<Phoneme> phonemizeSsml(std::string_view ssml,
                                   const SsmlTextFrontEnd &phonemizeText,
                                   const SsmlEmphasisLevels &emphasisLevels);

std::vector<Phoneme> phonemizeSsml(std::string_view ssml,
                                   const SsmlTextFrontEnd &phonemizeText,
                                   const SsmlNvvResolver &resolveNvv,
                                   const SsmlEmphasisLevels &emphasisLevels);

// Convenience overload using one of the built-in language front ends.
std::vector<Phoneme> phonemizeSsml(std::string_view ssml,
                                   Language language = Language::English);

} // namespace formant
