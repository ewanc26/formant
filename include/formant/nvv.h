#pragma once

// Nonverbal vocalizations (NVVs) as data-defined phoneme sequences (#124).
// A laugh, sigh, breath or throat clear is a short list of ordinary Phonemes
// with contour parameters, so it travels through the same deterministic sine
// engine as speech. No synthesis code is added per event: built-in definitions
// and user overrides share one line-oriented parser.

#include <functional>
#include <map>
#include <string>
#include <string_view>
#include <vector>

#include "formant/phoneme.h"

namespace formant {

// A multiplicative delivery style applied to every segment of an NVV. Styles
// let the same laugh sound nervous or warm without a second definition.
struct NvvStyle {
    std::string name;
    double rateScale = 1.0;      // <1 slows the event down
    double pitchScale = 1.0;     // scales pitchStart/pitchEnd
    double amplitudeScale = 1.0; // scales the per-segment amplitude
};

// One built-in or user-defined nonverbal vocalization.
struct NvvDefinition {
    std::string name;
    std::vector<Phoneme> segments;
};

// Loads built-in NVVs and optional user definitions. User entries override a
// built-in with the same name, so a script can replace a laugh without code.
// Inline [name] tokens in text are expanded into the corresponding segments.
class NvvLibrary {
  public:
    // `builtin` holds the shipped table; `user` is an optional override table.
    // Both use the format documented in docs/nvv.md. Throws
    // std::invalid_argument with a line number on malformed input.
    NvvLibrary(std::string_view builtin, std::string_view user = {});

    bool empty() const { return definitions_.empty(); }
    bool has(std::string_view name) const;
    const std::vector<std::string> &names() const { return orderedNames_; }

    // Returns the styled segment sequence for `name`. Throws with the unknown
    // name quoted when no definition matches.
    std::vector<Phoneme> get(std::string_view name) const;

    // Available style names, with "neutral" first.
    const std::vector<std::string> &styleNames() const {
        return orderedStyles_;
    }
    const std::string &activeStyle() const { return activeStyle_; }
    // Selects a style. Throws std::invalid_argument when it is unknown.
    void setStyle(std::string_view name);

    // Splits `text` on [name] tokens: text runs go through `frontEnd`, while
    // tokens are replaced by their NVV segments. Unknown tokens throw.
    std::vector<Phoneme>
    expand(std::string_view text,
           const std::function<std::vector<Phoneme>(std::string_view)>
               &frontEnd) const;

  private:
    void load(std::string_view source);

    std::map<std::string, NvvDefinition> definitions_;
    std::map<std::string, NvvStyle> styles_;
    std::vector<std::string> orderedNames_;
    std::vector<std::string> orderedStyles_;
    std::string activeStyle_ = "neutral";
};

// The shipped built-in table. Kept as data rather than compiled-in C++ so the
// file can be installed and inspected; the string is generated at build time.
std::string_view builtinNvvData();

} // namespace formant
