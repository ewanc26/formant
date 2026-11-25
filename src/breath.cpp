#include "formant/breath.h"

#include "formant/breath_data.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>

namespace formant {
namespace {

std::string trim(std::string_view text) {
    std::size_t begin = 0;
    std::size_t end = text.size();
    while (begin < end &&
           std::isspace(static_cast<unsigned char>(text[begin])) != 0) {
        ++begin;
    }
    while (end > begin &&
           std::isspace(static_cast<unsigned char>(text[end - 1])) != 0) {
        --end;
    }
    return std::string(text.substr(begin, end - begin));
}

std::vector<std::string> tokens(const std::string &line) {
    std::istringstream stream(line);
    std::vector<std::string> result;
    std::string token;
    while (stream >> token) {
        result.push_back(token);
    }
    return result;
}

[[noreturn]] void fail(std::size_t line, const std::string &message) {
    throw std::invalid_argument("breath line " + std::to_string(line) + ": " +
                                message);
}

double number(const std::string &token, std::size_t line, const char *field) {
    try {
        std::size_t used = 0;
        const double value = std::stod(token, &used);
        if (used != token.size() || !std::isfinite(value)) {
            fail(line, std::string("invalid ") + field + ": " + token);
        }
        return value;
    } catch (const std::invalid_argument &) {
        fail(line, std::string("invalid ") + field + ": " + token);
    } catch (const std::out_of_range &) {
        fail(line, std::string("invalid ") + field + ": " + token);
    }
}

std::size_t integer(const std::string &token, std::size_t line,
                    const char *field) {
    if (token.empty() ||
        !std::all_of(token.begin(), token.end(),
                     [](unsigned char c) { return std::isdigit(c) != 0; })) {
        fail(line, std::string("invalid ") + field + ": " + token);
    }
    try {
        const unsigned long long value = std::stoull(token);
        if (value > static_cast<unsigned long long>(SIZE_MAX)) {
            fail(line, std::string("invalid ") + field + ": " + token);
        }
        return static_cast<std::size_t>(value);
    } catch (const std::exception &) {
        fail(line, std::string("invalid ") + field + ": " + token);
    }
}

struct RawData {
    BreathOverlay base;
    std::map<std::string, BreathOverlay> languages;
    std::map<std::string, BreathOverlay> styles;
};

enum class Section { Base, Language, Style };

void parseSource(std::string_view source, RawData &data) {
    Section section = Section::Base;
    BreathOverlay *current = &data.base;
    std::size_t lineNumber = 0;
    std::size_t pos = 0;
    while (pos <= source.size()) {
        std::size_t end = source.find('\n', pos);
        if (end == std::string_view::npos) {
            end = source.size();
        }
        const std::string line = trim(source.substr(pos, end - pos));
        ++lineNumber;
        pos = end + 1;
        if (line.empty() || line[0] == '#') {
            continue;
        }
        if (line.rfind("language ", 0) == 0) {
            const std::string name = trim(std::string_view(line).substr(9));
            if (name.empty()) {
                fail(lineNumber, "language needs a tag");
            }
            // "language default" edits the base overlay, not a section named
            // "default". Named language sections stay available for listings.
            current = name == "default" ? &data.base : &data.languages[name];
            section = Section::Language;
            continue;
        }
        if (line.rfind("style ", 0) == 0) {
            const std::string name = trim(std::string_view(line).substr(6));
            if (name.empty()) {
                fail(lineNumber, "style needs a name");
            }
            current = name == "default" ? &data.base : &data.styles[name];
            section = Section::Style;
            continue;
        }
        if (current == nullptr) {
            fail(lineNumber, "directive outside a section");
        }
        const std::vector<std::string> fields = tokens(line);
        if (fields.empty()) {
            continue;
        }
        const std::string &key = fields[0];
        const auto need = [&](std::size_t count) {
            if (fields.size() < count) {
                fail(lineNumber,
                     key + " needs " + std::to_string(count - 1) + " value(s)");
            }
        };
        if (key == "min-words") {
            need(2);
            current->minWords = integer(fields[1], lineNumber, "min-words");
        } else if (key == "max-words") {
            need(2);
            current->maxWords = integer(fields[1], lineNumber, "max-words");
        } else if (key == "short-words") {
            need(2);
            current->shortWords = integer(fields[1], lineNumber, "short-words");
        } else if (key == "medium-words") {
            need(2);
            current->mediumWords =
                integer(fields[1], lineNumber, "medium-words");
        } else if (key == "max-seconds") {
            need(2);
            current->maxSeconds = number(fields[1], lineNumber, "max-seconds");
            if (!(current->maxSeconds > 0.0)) {
                fail(lineNumber, "max-seconds must be positive");
            }
        } else if (key == "declination") {
            need(2);
            current->declination = number(fields[1], lineNumber, "declination");
            if (!(*current->declination > 0.0 && *current->declination < 1.0)) {
                fail(lineNumber, "declination must be between 0 and 1");
            }
        } else if (key == "reset") {
            need(2);
            current->reset = number(fields[1], lineNumber, "reset");
            if (!(*current->reset >= 1.0)) {
                fail(lineNumber, "reset must be at least 1");
            }
        } else if (key == "depth-short") {
            need(2);
            current->depthShort = number(fields[1], lineNumber, "depth-short");
            if (!(*current->depthShort > 0.0)) {
                fail(lineNumber, "depth-short must be positive");
            }
        } else if (key == "depth-medium") {
            need(2);
            current->depthMedium =
                number(fields[1], lineNumber, "depth-medium");
            if (!(*current->depthMedium > 0.0)) {
                fail(lineNumber, "depth-medium must be positive");
            }
        } else if (key == "depth-long") {
            need(2);
            current->depthLong = number(fields[1], lineNumber, "depth-long");
            if (!(*current->depthLong > 0.0)) {
                fail(lineNumber, "depth-long must be positive");
            }
        } else if (key == "pause-seconds") {
            need(2);
            current->pauseSeconds =
                number(fields[1], lineNumber, "pause-seconds");
            if (!(*current->pauseSeconds >= 0.0)) {
                fail(lineNumber, "pause-seconds must be non-negative");
            }
        } else if (key == "strong" || key == "weak") {
            need(2);
            std::vector<std::string> values(fields.begin() + 1, fields.end());
            for (const std::string &value : values) {
                if (value.size() != 1) {
                    fail(lineNumber,
                         key +
                             " entries must be single phoneme names: " + value);
                }
            }
            if (key == "strong") {
                current->strongBoundaries = std::move(values);
            } else {
                current->weakBoundaries = std::move(values);
            }
        } else if (key == "event-short" || key == "event-medium" ||
                   key == "event-long") {
            need(2);
            if (fields.size() != 2) {
                fail(lineNumber, key + " needs one event name");
            }
            if (key == "event-short") {
                current->eventShort = fields[1];
            } else if (key == "event-medium") {
                current->eventMedium = fields[1];
            } else {
                current->eventLong = fields[1];
            }
        } else {
            fail(lineNumber, "unknown directive '" + key + "'");
        }
        // Keep the compiler honest about the section being used only to
        // choose the overlay pointer above.
        (void)section;
    }
}

void applyOverlay(BreathGroupPolicy &policy, const BreathOverlay &overlay) {
    if (overlay.minWords) {
        policy.minWords = *overlay.minWords;
    }
    if (overlay.maxWords) {
        policy.maxWords = *overlay.maxWords;
    }
    if (overlay.maxSeconds) {
        policy.maxSeconds = *overlay.maxSeconds;
    }
    if (overlay.shortWords) {
        policy.shortWords = *overlay.shortWords;
    }
    if (overlay.mediumWords) {
        policy.mediumWords = *overlay.mediumWords;
    }
    if (overlay.declination) {
        policy.declination = *overlay.declination;
    }
    if (overlay.reset) {
        policy.reset = *overlay.reset;
    }
    if (overlay.depthShort) {
        policy.depthShort = *overlay.depthShort;
    }
    if (overlay.depthMedium) {
        policy.depthMedium = *overlay.depthMedium;
    }
    if (overlay.depthLong) {
        policy.depthLong = *overlay.depthLong;
    }
    if (overlay.pauseSeconds) {
        policy.pauseSeconds = *overlay.pauseSeconds;
    }
    if (overlay.strongBoundaries) {
        policy.strongBoundaries = *overlay.strongBoundaries;
    }
    if (overlay.weakBoundaries) {
        policy.weakBoundaries = *overlay.weakBoundaries;
    }
    if (overlay.eventShort) {
        policy.eventShort = *overlay.eventShort;
    }
    if (overlay.eventMedium) {
        policy.eventMedium = *overlay.eventMedium;
    }
    if (overlay.eventLong) {
        policy.eventLong = *overlay.eventLong;
    }
}

void validate(const BreathGroupPolicy &policy) {
    if (policy.minWords == 0 || policy.maxWords < policy.minWords ||
        policy.shortWords == 0 || policy.mediumWords < policy.shortWords) {
        throw std::invalid_argument(
            "breath policy needs positive word limits with "
            "min-words <= max-words and short-words <= medium-words");
    }
    if (!(policy.maxSeconds > 0.0) || !(policy.pauseSeconds >= 0.0)) {
        throw std::invalid_argument(
            "breath policy needs positive max-seconds and non-negative "
            "pause-seconds");
    }
    if (!(policy.declination > 0.0 && policy.declination < 1.0) ||
        !(policy.reset >= 1.0)) {
        throw std::invalid_argument(
            "breath policy needs 0 < declination < 1 and reset >= 1");
    }
    if (!(policy.depthShort > 0.0 && policy.depthMedium > 0.0 &&
          policy.depthLong > 0.0)) {
        throw std::invalid_argument("breath depth scales must be positive");
    }
    if (policy.strongBoundaries.empty() || policy.eventShort.empty() ||
        policy.eventMedium.empty() || policy.eventLong.empty()) {
        throw std::invalid_argument(
            "breath policy needs at least one strong boundary and three "
            "event names");
    }
}

bool contains(const std::vector<std::string> &values,
              const std::string &wanted) {
    return std::find(values.begin(), values.end(), wanted) != values.end();
}

bool isPunctuation(const Phoneme &phoneme,
                   const std::vector<std::string> &strong,
                   const std::vector<std::string> &weak) {
    return contains(strong, phoneme.name) || contains(weak, phoneme.name);
}

bool isExplicitBreath(const Phoneme &phoneme) {
    return phoneme.source.rfind("nvv:breath-", 0) == 0 ||
           phoneme.source.rfind("breath:", 0) == 0;
}

// An author-placed breath within this window marks the same boundary; the
// planner must not add a second one on top of it.
bool explicitNear(const std::vector<Phoneme> &phonemes, std::size_t index) {
    const std::size_t begin = index > 4 ? index - 4 : 0;
    const std::size_t end = std::min(phonemes.size(), index + 5);
    for (std::size_t i = begin; i < end; ++i) {
        if (isExplicitBreath(phonemes[i])) {
            return true;
        }
    }
    return false;
}

struct Boundary {
    std::size_t after = 0;
    std::size_t words = 0;
    double seconds = 0.0;
};

int depthTier(const BreathGroupPolicy &policy, std::size_t words) {
    if (words >= policy.mediumWords) {
        return 2;
    }
    if (words >= policy.shortWords) {
        return 1;
    }
    return 0;
}

double depthScale(const BreathGroupPolicy &policy, int tier) {
    if (tier == 2) {
        return policy.depthLong;
    }
    if (tier == 1) {
        return policy.depthMedium;
    }
    return policy.depthShort;
}

const std::string &eventName(const BreathGroupPolicy &policy, int tier) {
    if (tier == 2) {
        return policy.eventLong;
    }
    if (tier == 1) {
        return policy.eventMedium;
    }
    return policy.eventShort;
}

} // namespace

BreathPolicySet parseBreathData(std::string_view builtin,
                                std::string_view user) {
    RawData raw;
    parseSource(builtin, raw);
    if (!user.empty()) {
        parseSource(user, raw);
    }

    BreathPolicySet result;
    applyOverlay(result.defaultPolicy, raw.base);
    validate(result.defaultPolicy);
    // The base overlay is already folded into defaultPolicy, so each section
    // only needs to layer its own fields on top.
    for (const auto &entry : raw.languages) {
        BreathGroupPolicy policy = result.defaultPolicy;
        applyOverlay(policy, entry.second);
        validate(policy);
        result.languages[entry.first] = policy;
        result.languageOverlays[entry.first] = entry.second;
    }
    for (const auto &entry : raw.styles) {
        BreathGroupPolicy policy = result.defaultPolicy;
        applyOverlay(policy, entry.second);
        validate(policy);
        result.styles[entry.first] = policy;
        result.styleOverlays[entry.first] = entry.second;
    }
    return result;
}

BreathPolicySet loadBreathData(const std::string &path) {
    std::ifstream file(path);
    if (!file) {
        throw std::runtime_error("cannot open breath file: " + path);
    }
    std::ostringstream source;
    source << file.rdbuf();
    if (file.bad()) {
        throw std::runtime_error("cannot read breath file: " + path);
    }
    return parseBreathData(builtinBreathData(), source.str());
}

BreathMode breathModeFromName(std::string_view name) {
    if (name == "off") {
        return BreathMode::Off;
    }
    if (name == "silent") {
        return BreathMode::Silent;
    }
    if (name == "auto") {
        return BreathMode::Auto;
    }
    throw std::invalid_argument("invalid breathing mode '" + std::string(name) +
                                "' (expected off, silent or auto)");
}

BreathGroupPolicy resolveBreathPolicy(const BreathPolicySet &set,
                                      std::string_view languageTag,
                                      std::string_view style) {
    BreathGroupPolicy policy = set.defaultPolicy;
    if (!languageTag.empty()) {
        const auto overlay =
            set.languageOverlays.find(std::string(languageTag));
        if (overlay != set.languageOverlays.end()) {
            applyOverlay(policy, overlay->second);
        } else {
            const auto resolved = set.languages.find(std::string(languageTag));
            if (resolved != set.languages.end()) {
                policy = resolved->second;
            }
        }
    }
    if (!style.empty() && style != "neutral") {
        const auto overlay = set.styleOverlays.find(std::string(style));
        if (overlay != set.styleOverlays.end()) {
            applyOverlay(policy, overlay->second);
        } else {
            const auto resolved = set.styles.find(std::string(style));
            if (resolved != set.styles.end()) {
                policy = resolved->second;
            }
        }
    }
    return policy;
}

std::string_view builtinBreathData() { return detail::kBuiltinBreathData; }

BreathPlan planBreathGroups(const std::vector<Phoneme> &phonemes,
                            const BreathGroupPolicy &policy,
                            const BreathEventLoader &eventLoader,
                            const BreathPlanOptions &options) {
    BreathPlan plan;
    if (options.mode == BreathMode::Off) {
        plan.phonemes = phonemes;
        return plan;
    }

    // Pre-scan clause/length boundaries in the source vector. Boundaries next
    // to an author breath are skipped because that breath already marks them.
    std::vector<Boundary> boundaries;
    bool inWord = false;
    std::size_t words = 0;
    double seconds = 0.0;
    for (std::size_t i = 0; i < phonemes.size(); ++i) {
        const Phoneme &phoneme = phonemes[i];
        if (isExplicitBreath(phoneme)) {
            // An author breath starts a new planning region here. The main
            // pass copies it verbatim; resetting here keeps later boundaries
            // from inheriting the previous clause's word/duration totals.
            inWord = false;
            words = 0;
            seconds = 0.0;
            continue;
        }
        if (phoneme.name == "|") {
            if (inWord) {
                ++words;
                inWord = false;
            }
            if ((words >= policy.maxWords || seconds >= policy.maxSeconds) &&
                i + 1 < phonemes.size() && !explicitNear(phonemes, i)) {
                boundaries.push_back({i, words, seconds});
                words = 0;
                seconds = 0.0;
            }
            continue;
        }
        if (isPunctuation(phoneme, policy.strongBoundaries,
                          policy.weakBoundaries)) {
            if (inWord) {
                ++words;
                inWord = false;
            }
            const bool strong = contains(policy.strongBoundaries, phoneme.name);
            const bool weak = contains(policy.weakBoundaries, phoneme.name);
            const bool forced =
                words >= policy.maxWords || seconds >= policy.maxSeconds;
            const bool eligible =
                words >= policy.minWords &&
                (strong || forced || (weak && words >= policy.mediumWords));
            if (eligible && i + 1 < phonemes.size() &&
                !explicitNear(phonemes, i)) {
                boundaries.push_back({i, words, seconds});
                words = 0;
                seconds = 0.0;
            }
            continue;
        }
        if (phoneme.kind != PhonemeKind::Silence) {
            inWord = true;
            seconds += phoneme.duration;
        }
    }

    std::map<std::size_t, Boundary> boundaryAt;
    for (const Boundary &boundary : boundaries) {
        boundaryAt[boundary.after] = boundary;
    }

    std::vector<Phoneme> &output = plan.phonemes;
    output.reserve(phonemes.size() + boundaries.size() * 2);
    std::size_t groupStart = 0;
    inWord = false;
    words = 0;
    seconds = 0.0;

    const auto appendGroup = [&](std::size_t end, std::size_t groupWords,
                                 double groupSeconds, bool reset) {
        if (end <= groupStart) {
            return;
        }
        BreathGroup group;
        group.begin = groupStart;
        group.end = end;
        group.words = groupWords;
        group.seconds = groupSeconds;
        group.depthTier = depthTier(policy, groupWords);
        group.depthScale = depthScale(policy, group.depthTier);
        group.reset = reset;

        double time = 0.0;
        for (std::size_t i = 0; i < groupStart; ++i) {
            time += output[i].duration;
        }
        group.startSeconds = time;
        for (std::size_t i = groupStart; i < end; ++i) {
            time += output[i].duration;
        }
        group.endSeconds = time;

        // Count voiced nuclei first so the contour runs from the group's own
        // first to its last voiced segment, independent of unvoiced onsets.
        std::vector<std::size_t> voiced;
        for (std::size_t i = groupStart; i < end; ++i) {
            if (output[i].voiced && !output[i].f0StartHz.has_value()) {
                voiced.push_back(i);
            }
        }
        const double resetFactor = reset ? policy.reset : 1.0;
        for (std::size_t ordinal = 0; ordinal < voiced.size(); ++ordinal) {
            const double progress =
                voiced.size() > 1 ? static_cast<double>(ordinal) /
                                        static_cast<double>(voiced.size() - 1)
                                  : 0.0;
            const double factor =
                resetFactor * (1.0 + (policy.declination - 1.0) * progress);
            Phoneme &phoneme = output[voiced[ordinal]];
            phoneme.pitchStart *= factor;
            phoneme.pitchEnd *= factor;
        }
        plan.groups.push_back(group);
    };

    const auto appendBreath = [&](int tier) {
        const double scale = depthScale(policy, tier);
        if (options.mode == BreathMode::Auto) {
            if (!eventLoader) {
                throw std::invalid_argument(
                    "breath planner needs an event loader in auto mode");
            }
            std::vector<Phoneme> event = eventLoader(eventName(policy, tier));
            for (Phoneme &phoneme : event) {
                phoneme.duration *= scale;
                phoneme.amplitude *= 0.75 + 0.25 * scale;
                phoneme.source = "breath:auto";
                output.push_back(std::move(phoneme));
            }
        } else {
            Phoneme pause = findPhoneme("|") ? *findPhoneme("|") : Phoneme{};
            pause.name = "|";
            pause.kind = PhonemeKind::Silence;
            pause.duration = policy.pauseSeconds * scale;
            pause.source = "breath:pause";
            output.push_back(std::move(pause));
        }
        ++plan.insertedEvents;
    };

    for (std::size_t i = 0; i < phonemes.size(); ++i) {
        const Phoneme &phoneme = phonemes[i];
        if (isExplicitBreath(phoneme)) {
            // The author's breath is the boundary: finish the group before
            // it, copy it verbatim, and start a fresh group afterwards.
            appendGroup(output.size(), words, seconds, !plan.groups.empty());
            output.push_back(phoneme);
            groupStart = output.size();
            inWord = false;
            words = 0;
            seconds = 0.0;
            continue;
        }
        output.push_back(phoneme);

        if (phoneme.name == "|") {
            if (inWord) {
                ++words;
                inWord = false;
            }
        } else if (isPunctuation(phoneme, policy.strongBoundaries,
                                 policy.weakBoundaries)) {
            if (inWord) {
                ++words;
                inWord = false;
            }
        } else if (phoneme.kind != PhonemeKind::Silence) {
            inWord = true;
            seconds += phoneme.duration;
        }

        const auto found = boundaryAt.find(i);
        if (found == boundaryAt.end()) {
            continue;
        }
        const int tier = depthTier(policy, found->second.words);
        appendGroup(output.size(), found->second.words, found->second.seconds,
                    !plan.groups.empty());
        appendBreath(tier);
        groupStart = output.size();
        inWord = false;
        words = 0;
        seconds = 0.0;
    }
    appendGroup(output.size(), words, seconds, !plan.groups.empty());
    return plan;
}

} // namespace formant
