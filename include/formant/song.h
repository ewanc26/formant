#pragma once

// #123: opt-in singing mode.
//
// A song is plain, versioned data. It resolves notes to absolute F0 targets and
// adds a bounded, delayed vibrato envelope on top of those targets. Everything
// here is parameter-domain work: the renderer still sees ordinary Phoneme
// values and one continuous glottal phase accumulator. A render without a song
// never constructs these fields, so default speech stays byte-identical.

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "formant/phoneme.h"

namespace formant {

constexpr int kCurrentSongSchema = 1;
constexpr int kCurrentSongScaleSchema = 1;
constexpr double kSongDefaultA4Hz = 440.0;
constexpr double kSongDefaultPeriodCents = 1200.0;

// Shared by the renderer's F0 oracle and by tests. Returns zero at and before
// the delay, then a smoothstep-faded sine in cents. `NoteF0Control` lives in
// phoneme.h so the renderer can carry it on each segment; this function uses
// the same deterministic sine kernel as speech vibrato, with no second
// oscillator and no wall-clock or random state.
double songVibratoCents(const NoteF0Control &control, double noteTimeSeconds);

enum class SongTransitionKind { Legato, Portamento };

struct SongVibrato {
    double rateHz = 0.0;
    double extentCents = 0.0;
    double delaySeconds = 0.0;
    double fadeSeconds = 0.0;

    bool operator==(const SongVibrato &) const = default;
};

// One note in a song file. `pitch` is kept verbatim for round-tripping and is
// resolved against a SongScale when the song is converted to phonemes. It may
// be a scale degree name with an octave (C4), `midi:69`, a bare MIDI number
// (69), `cents:1200`, `degree:18`, or any of those with a +/- cents suffix.
struct SongNote {
    std::string pitch;
    double durationSeconds = 0.0;
    std::string phonemes; // one phoneme name, or a '-' separated sequence
    SongTransitionKind transition = SongTransitionKind::Legato;
    double glideSeconds = 0.012; // legato is deliberately near-instant
    double overshootCents = 0.0;
    double preparationCents = 0.0;
    SongVibrato vibrato;
    std::size_t line = 0;

    bool operator==(const SongNote &) const = default;
};

// A versioned song document. It is a pure value with no file handles.
struct SongFile {
    int schema = 0;
    std::string name = "song";
    double a4Hz = kSongDefaultA4Hz;
    std::string scale = "chromatic";
    std::vector<SongNote> notes;
    std::optional<std::string> checksum;
};

// A degree in a loadable scale table. `cents` is measured from the C of the
// reference octave, matching ordinary note-name conventions. An unnamed degree
// is still addressable as `degree:N`.
struct SongScaleDegree {
    std::string name;
    double cents = 0.0;
    std::size_t line = 0;

    bool operator==(const SongScaleDegree &) const = default;
};

// Non-12-tone scales are data. The period is normally 1200 cents but any
// positive period is accepted; `referenceCents` names the A4 position inside
// that period so A4 stays the tuning anchor.
struct SongScale {
    int schema = 0;
    std::string name = "scale";
    double periodCents = kSongDefaultPeriodCents;
    double referenceCents = 900.0;
    std::vector<SongScaleDegree> degrees;
    std::optional<std::string> checksum;
};

// Checksum convention matches voice/affect packages: SHA-256 of the file with
// only the `checksum` directive removed and CRLF normalised to LF. Comments and
// blank lines are part of the versioned document.
std::string songChecksumSource(std::string_view text);
std::string songFileChecksum(std::string_view text);
std::string songScaleChecksumSource(std::string_view text);
std::string songScaleChecksum(std::string_view text);

// Strict line-oriented parsers. Malformed input throws std::invalid_argument
// with "song line N: ..." or "scale line N: ...". Checksum verification is on
// by default and can be disabled only for --inspect-style-like diagnostics.
SongFile parseSongFile(std::string_view text, bool verifyChecksum = true);
SongFile loadSongFile(const std::string &path);
SongScale parseSongScale(std::string_view text, bool verifyChecksum = true);
SongScale loadSongScale(const std::string &path);

// Deterministic serialisation with a trailing checksum, for sharing.
std::string writeSongFile(const SongFile &song);
std::string writeSongScale(const SongScale &scale);

// Built-ins are embedded at build time. The default demo is "twinkle"; the
// built-in scales are "chromatic" and "quarter".
std::vector<std::string> builtinSongNames();
std::string_view builtinSongData(std::string_view name);
std::vector<std::string> builtinScaleNames();
std::string_view builtinScaleData(std::string_view name);

// Resolve a parsed song into the ordinary front-end representation. The
// optional a4OverrideHz wins over SongFile::a4Hz, so the CLI can retune a
// shipped song without editing data. Transitions are encoded as contiguous
// absolute-Hz segments; the renderer's single phase accumulator carries the
// waveform through every boundary.
std::vector<Phoneme> phonemizeSong(const SongFile &song, const SongScale &scale,
                                   double a4OverrideHz = 0.0);

} // namespace formant
