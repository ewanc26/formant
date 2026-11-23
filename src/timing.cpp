#include "formant/timing.h"

#include "schedule.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <limits>
#include <locale>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace formant {
namespace {

bool decodeUtf8(std::string_view text, size_t &pos, uint32_t &cp) {
    if (pos >= text.size()) {
        return false;
    }
    const unsigned char first = static_cast<unsigned char>(text[pos]);
    size_t length = 0;
    uint32_t value = 0;
    unsigned char minimumSecond = 0x80;
    unsigned char maximumSecond = 0xBF;
    if (first < 0x80) {
        value = first;
        length = 1;
    } else if (first >= 0xC2 && first <= 0xDF) {
        value = first & 0x1F;
        length = 2;
    } else if (first >= 0xE0 && first <= 0xEF) {
        value = first & 0x0F;
        length = 3;
        if (first == 0xE0) {
            minimumSecond = 0xA0; // reject overlong encodings
        } else if (first == 0xED) {
            maximumSecond = 0x9F; // reject UTF-16 surrogates
        }
    } else if (first >= 0xF0 && first <= 0xF4) {
        value = first & 0x07;
        length = 4;
        if (first == 0xF0) {
            minimumSecond = 0x90; // reject overlong encodings
        } else if (first == 0xF4) {
            maximumSecond = 0x8F; // reject values above U+10FFFF
        }
    } else {
        return false;
    }
    if (pos + length > text.size()) {
        return false;
    }
    for (size_t i = 1; i < length; ++i) {
        const unsigned char byte = static_cast<unsigned char>(text[pos + i]);
        const unsigned char low = i == 1 ? minimumSecond : 0x80;
        const unsigned char high = i == 1 ? maximumSecond : 0xBF;
        if (byte < low || byte > high) {
            return false;
        }
        value = (value << 6) | (byte & 0x3F);
    }
    cp = value;
    pos += length;
    return true;
}

bool isApostrophe(uint32_t cp) {
    return cp == '\'' || cp == 0x2019 || cp == 0x02BC;
}

bool isWordCodePoint(uint32_t cp) {
    if (cp < 0x80) {
        const int c = static_cast<int>(cp);
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
               (c >= '0' && c <= '9');
    }
    // Letter and combining-mark ranges covering the scripts handled by the
    // text front ends, plus IPA extensions. Ranges are deliberately broad but
    // exclude general punctuation, symbols, arrows and emoji.
    if (cp >= 0x0300 && cp <= 0x036F) {
        return true; // combining diacritics
    }
    if (cp >= 0x00C0 && cp <= 0x02AF) {
        return cp != 0x00D7 && cp != 0x00F7; // Latin letters, IPA extensions
    }
    if (cp >= 0x0370 && cp <= 0x03FF && cp != 0x037E) {
        return true; // Greek
    }
    if (cp >= 0x0400 && cp <= 0x052F) {
        return true; // Cyrillic
    }
    if (cp >= 0x0531 && cp <= 0x058F) {
        return true; // Armenian
    }
    if (cp >= 0x0590 && cp <= 0x05FF) {
        return true; // Hebrew
    }
    if (cp >= 0x0600 && cp <= 0x06FF) {
        return true; // Arabic
    }
    if (cp >= 0x0900 && cp <= 0x097F) {
        return true; // Devanagari
    }
    if (cp >= 0x3040 && cp <= 0x30FF) {
        return true; // Hiragana and Katakana
    }
    if (cp >= 0x3400 && cp <= 0x9FFF) {
        return true; // CJK
    }
    if (cp >= 0xAC00 && cp <= 0xD7AF) {
        return true; // Hangul
    }
    return cp >= 0xF900 && cp <= 0xFAFF; // CJK compatibility
}

std::string fixed(double value, int digits) {
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out << std::fixed << std::setprecision(digits) << value;
    return out.str();
}

std::string jsonEscape(std::string_view value) {
    std::string result;
    result.reserve(value.size() + 2);
    for (unsigned char c : value) {
        switch (c) {
        case '"':
            result += "\\\"";
            break;
        case '\\':
            result += "\\\\";
            break;
        case '\b':
            result += "\\b";
            break;
        case '\f':
            result += "\\f";
            break;
        case '\n':
            result += "\\n";
            break;
        case '\r':
            result += "\\r";
            break;
        case '\t':
            result += "\\t";
            break;
        default:
            if (c < 0x20) {
                std::ostringstream out;
                out.imbue(std::locale::classic());
                out << "\\u" << std::hex << std::setw(4) << std::setfill('0')
                    << static_cast<int>(c);
                result += out.str();
            } else {
                result.push_back(static_cast<char>(c));
            }
        }
    }
    return result;
}

// Formats audio time as HH:MM:SS,mmm / HH:MM:SS.mmm for SRT and WebVTT.
std::string clockTime(double seconds, char fractionSeparator) {
    const int64_t milliseconds = std::llround(seconds * 1000.0);
    const int64_t hours = milliseconds / 3600000;
    const int64_t minutes = (milliseconds / 60000) % 60;
    const int64_t secs = (milliseconds / 1000) % 60;
    const int64_t millis = milliseconds % 1000;
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out << std::setfill('0') << std::setw(2) << hours << ':' << std::setw(2)
        << minutes << ':' << std::setw(2) << secs << fractionSeparator
        << std::setw(3) << millis;
    return out.str();
}

// Enhanced-LRC uses centiseconds: [mm:ss.xx].
std::string lrcTime(double seconds) {
    const int64_t centiseconds = std::llround(seconds * 100.0);
    const int64_t minutes = centiseconds / 6000;
    const int64_t secs = (centiseconds / 100) % 60;
    const int64_t cs = centiseconds % 100;
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out << std::setfill('0') << std::setw(2) << minutes << ':' << std::setw(2)
        << secs << '.' << std::setw(2) << cs;
    return out.str();
}

std::string oneLine(std::string_view text) {
    std::string result;
    for (char c : text) {
        result.push_back(c == '\n' || c == '\r' ? ' ' : c);
    }
    return result;
}

struct VisemeMapping {
    const char *shape;
    double openness;
};

// Data table keyed by the short phoneme names from the built-in inventory and
// by common IPA symbols produced by parseIpa(). Unknown names fall back by
// kind in visemeFor() so new phonemes never break a render.
VisemeMapping namedViseme(const std::string &name) {
    static const std::pair<const char *, VisemeMapping> table[] = {
        // Vowels. Openness follows vowel height; front vowels are "spread",
        // back vowels "round", and central/rhotic vowels keep their own shape.
        {"a", {"open", 0.95}},
        {"aɪ", {"open", 0.9}},
        {"aʊ", {"open", 0.9}},
        {"aa", {"open", 1.0}},
        {"ah", {"open", 0.85}},
        {"æ", {"open", 0.9}},
        {"ɑ", {"open", 0.95}},
        {"ɒ", {"open", 0.85}},
        {"ɶ", {"open", 0.95}},
        {"ʌ", {"open", 0.8}},
        {"ɐ", {"central", 0.75}},
        {"e", {"spread", 0.55}},
        {"ɛ", {"spread", 0.6}},
        {"ee", {"spread", 0.5}},
        {"ɪ", {"spread", 0.45}},
        {"i", {"spread", 0.35}},
        {"y", {"spread", 0.4}},
        {"ø", {"spread", 0.5}},
        {"œ", {"spread", 0.6}},
        {"ə", {"central", 0.5}},
        {"ɚ", {"rhotic", 0.5}},
        {"ɜ", {"central", 0.5}},
        {"ɝ", {"rhotic", 0.5}},
        {"ɞ", {"central", 0.6}},
        {"ɤ", {"central", 0.55}},
        {"ɵ", {"round", 0.5}},
        {"o", {"round", 0.45}},
        {"ɔ", {"round", 0.55}},
        {"oo", {"round", 0.3}},
        {"ʊ", {"round", 0.35}},
        {"u", {"round", 0.25}},
        {"ʉ", {"round", 0.3}},
        {"ɯ", {"round", 0.4}},
        {"er", {"rhotic", 0.5}},
        {"or", {"round", 0.45}},
        {"uh", {"central", 0.5}},
        // Consonants. Shape follows place/manner; openness is a stable,
        // approximate mouth aperture rather than an acoustic measurement.
        {"p", {"closed", 0.0}},
        {"b", {"closed", 0.0}},
        {"m", {"closed", 0.0}},
        {"ɱ", {"closed", 0.0}},
        {"f", {"labiodental", 0.2}},
        {"v", {"labiodental", 0.2}},
        {"ɸ", {"labiodental", 0.2}},
        {"β", {"labiodental", 0.2}},
        {"th", {"dental", 0.25}},
        {"dh", {"dental", 0.25}},
        {"θ", {"dental", 0.25}},
        {"ð", {"dental", 0.25}},
        {"t", {"alveolar", 0.25}},
        {"d", {"alveolar", 0.25}},
        {"n", {"alveolar", 0.1}},
        {"s", {"alveolar", 0.3}},
        {"z", {"alveolar", 0.3}},
        {"l", {"alveolar", 0.35}},
        {"ɾ", {"alveolar", 0.3}},
        {"r", {"rhotic", 0.4}},
        {"ɹ", {"rhotic", 0.4}},
        {"sh", {"postalveolar", 0.35}},
        {"zh", {"postalveolar", 0.35}},
        {"ʃ", {"postalveolar", 0.35}},
        {"ʒ", {"postalveolar", 0.35}},
        {"ch", {"postalveolar", 0.2}},
        {"j", {"postalveolar", 0.2}},
        {"ç", {"postalveolar", 0.25}},
        {"ʝ", {"postalveolar", 0.25}},
        {"ɲ", {"closed", 0.05}},
        {"k", {"velar", 0.25}},
        {"g", {"velar", 0.25}},
        {"ɡ", {"velar", 0.25}},
        {"ng", {"velar", 0.05}},
        {"ŋ", {"velar", 0.05}},
        {"x", {"velar", 0.25}},
        {"ɣ", {"velar", 0.25}},
        {"q", {"velar", 0.2}},
        {"ʔ", {"glottal", 0.05}},
        {"h", {"glottal", 0.4}},
        {"w", {"round", 0.25}},
        {"ʍ", {"round", 0.25}},
        // Pauses, word gaps and word-final silence phonemes.
        {"_", {"rest", 0.0}},
        {",", {"rest", 0.0}},
        {".", {"rest", 0.0}},
        {"?", {"rest", 0.0}},
        {"!", {"rest", 0.0}},
        {"|", {"rest", 0.0}},
    };
    for (const auto &entry : table) {
        if (name == entry.first) {
            return entry.second;
        }
    }
    return {nullptr, -1.0};
}

VisemeMapping visemeFor(const std::string &name, PhonemeKind kind) {
    const VisemeMapping named = namedViseme(name);
    if (named.shape != nullptr) {
        return named;
    }
    switch (kind) {
    case PhonemeKind::Silence:
        return {"rest", 0.0};
    case PhonemeKind::Voiced:
        return {"open", 0.6};
    case PhonemeKind::Nasal:
        return {"closed", 0.05};
    case PhonemeKind::Fricative:
        return {"narrow", 0.25};
    case PhonemeKind::Plosive:
        return {"closed", 0.05};
    case PhonemeKind::Approximant:
        return {"narrow", 0.4};
    }
    return {"rest", 0.0};
}

void appendUnitJson(std::ostringstream &out, const TimedUnit &unit,
                    const char *labelKey) {
    out << "    {\"" << labelKey << "\": \"" << jsonEscape(unit.label)
        << "\", \"startSample\": " << unit.startSample
        << ", \"endSample\": " << unit.endSample
        << ", \"start\": " << fixed(unit.startTime, 6)
        << ", \"end\": " << fixed(unit.endTime, 6) << "}";
}

} // namespace

std::vector<std::string> splitWordsForTiming(std::string_view text) {
    std::vector<std::string> words;
    std::string word;
    const auto flush = [&]() {
        if (!word.empty()) {
            words.push_back(word);
            word.clear();
        }
    };

    size_t pos = 0;
    while (pos < text.size()) {
        const size_t start = pos;
        uint32_t cp = 0;
        if (!decodeUtf8(text, pos, cp)) {
            throw std::invalid_argument("invalid UTF-8 in text");
        }
        const bool asciiWord = cp < 0x80 && isWordCodePoint(cp);
        if (asciiWord || cp >= 0x80) {
            word.append(text.data() + start, pos - start);
            continue;
        }
        // An apostrophe between two word characters belongs to the word. Its
        // original bytes are kept so labels preserve the input spelling.
        if (isApostrophe(cp) && !word.empty() && pos < text.size()) {
            size_t lookahead = pos;
            uint32_t next = 0;
            if (decodeUtf8(text, lookahead, next) && isWordCodePoint(next)) {
                word.append(text.data() + start, pos - start);
                continue;
            }
        }
        flush();
    }
    flush();
    return words;
}

TimingPlan planTiming(const std::vector<Phoneme> &phonemes, const Voice &voice,
                      int sampleRate,
                      const std::vector<std::string> &wordLabels) {
    const std::vector<detail::ScheduledSpan> spans =
        detail::schedulePhonemes(phonemes, voice.rate, sampleRate);

    TimingPlan plan;
    plan.sampleRate = sampleRate;
    plan.totalSamples = spans.empty() ? 0 : spans.back().end;
    plan.duration = static_cast<double>(plan.totalSamples) /
                    static_cast<double>(sampleRate);
    plan.voice = voice.name;

    for (size_t i = 0; i < phonemes.size(); ++i) {
        TimedUnit unit;
        unit.label = phonemes[i].name;
        unit.kind = phonemes[i].kind;
        unit.startSample = spans[i].start;
        unit.endSample = spans[i].end;
        unit.startTime = static_cast<double>(unit.startSample) /
                         static_cast<double>(sampleRate);
        unit.endTime = static_cast<double>(unit.endSample) /
                       static_cast<double>(sampleRate);
        plan.phonemes.push_back(std::move(unit));
    }

    // A word is a run of non-gap phonemes. Gaps ("|") delimit words; pauses
    // are folded into the word they follow so captions keep their punctuation.
    struct PendingWord {
        size_t first = 0;
        std::vector<std::string> names;
    };
    std::vector<PendingWord> pending;
    PendingWord current;
    bool haveWord = false;
    for (size_t i = 0; i < phonemes.size(); ++i) {
        const Phoneme &phoneme = phonemes[i];
        if (phoneme.name == "|" || phoneme.kind == PhonemeKind::Silence) {
            if (haveWord) {
                pending.push_back(current);
                haveWord = false;
            }
            continue;
        }
        if (!haveWord) {
            current = {i, {}};
            haveWord = true;
        }
        current.names.push_back(phoneme.name);
    }
    if (haveWord) {
        pending.push_back(current);
    }

    // Labels are only trusted when they describe every word exactly. Silent
    // gaps following a word are folded into that word's span, so captions are
    // contiguous and the last word ends at the final rendered sample.
    const bool useWordLabels = wordLabels.size() == pending.size();
    for (size_t w = 0; w < pending.size(); ++w) {
        TimedUnit unit;
        if (useWordLabels) {
            unit.label = wordLabels[w];
        } else {
            for (const std::string &name : pending[w].names) {
                if (!unit.label.empty()) {
                    unit.label += ' ';
                }
                unit.label += name;
            }
        }
        unit.startSample = plan.phonemes[pending[w].first].startSample;
        unit.endSample = w + 1 < pending.size()
                             ? plan.phonemes[pending[w + 1].first].startSample
                             : plan.totalSamples;
        unit.startTime = static_cast<double>(unit.startSample) /
                         static_cast<double>(sampleRate);
        unit.endTime = static_cast<double>(unit.endSample) /
                       static_cast<double>(sampleRate);
        plan.words.push_back(std::move(unit));
    }
    return plan;
}

std::vector<VisemeFrame> planVisemes(const TimingPlan &plan) {
    std::vector<VisemeFrame> frames;
    frames.reserve(plan.phonemes.size());
    for (const TimedUnit &unit : plan.phonemes) {
        // Phoneme units keep their inventory/IPA name and kind, so visemes
        // come from the same articulatory categories as the rendered sound.
        VisemeFrame frame;
        frame.phoneme = unit.label;
        const VisemeMapping mapping = visemeFor(unit.label, unit.kind);
        frame.shape = mapping.shape;
        frame.openness = mapping.openness;
        frame.startSample = unit.startSample;
        frame.endSample = unit.endSample;
        frame.startTime = unit.startTime;
        frame.endTime = unit.endTime;
        frames.push_back(std::move(frame));
    }
    return frames;
}

std::string timingJson(const TimingPlan &plan,
                       const std::vector<VisemeFrame> &visemes) {
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out << "{\n  \"format\": \"formant-timing\",\n  \"version\": 1,\n"
        << "  \"sampleRate\": " << plan.sampleRate << ",\n"
        << "  \"totalSamples\": " << plan.totalSamples << ",\n"
        << "  \"duration\": " << fixed(plan.duration, 6) << ",\n"
        << "  \"voice\": \"" << jsonEscape(plan.voice) << "\",\n"
        << "  \"words\": [\n";
    for (size_t i = 0; i < plan.words.size(); ++i) {
        appendUnitJson(out, plan.words[i], "label");
        out << (i + 1 < plan.words.size() ? ",\n" : "\n");
    }
    out << "  ],\n  \"phonemes\": [\n";
    for (size_t i = 0; i < plan.phonemes.size(); ++i) {
        appendUnitJson(out, plan.phonemes[i], "label");
        out << (i + 1 < plan.phonemes.size() ? ",\n" : "\n");
    }
    out << "  ],\n  \"visemes\": [\n";
    for (size_t i = 0; i < visemes.size(); ++i) {
        const VisemeFrame &frame = visemes[i];
        out << "    {\"shape\": \"" << jsonEscape(frame.shape)
            << "\", \"openness\": " << fixed(frame.openness, 3)
            << ", \"phoneme\": \"" << jsonEscape(frame.phoneme)
            << "\", \"startSample\": " << frame.startSample
            << ", \"endSample\": " << frame.endSample
            << ", \"start\": " << fixed(frame.startTime, 6)
            << ", \"end\": " << fixed(frame.endTime, 6) << "}";
        out << (i + 1 < visemes.size() ? ",\n" : "\n");
    }
    out << "  ]\n}\n";
    return out.str();
}

std::string timingVisemesJson(const TimingPlan &plan,
                              const std::vector<VisemeFrame> &visemes) {
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out << "{\n  \"format\": \"formant-visemes\",\n  \"version\": 1,\n"
        << "  \"sampleRate\": " << plan.sampleRate << ",\n"
        << "  \"totalSamples\": " << plan.totalSamples << ",\n"
        << "  \"frames\": [\n";
    for (size_t i = 0; i < visemes.size(); ++i) {
        const VisemeFrame &frame = visemes[i];
        out << "    {\"shape\": \"" << jsonEscape(frame.shape)
            << "\", \"openness\": " << fixed(frame.openness, 3)
            << ", \"phoneme\": \"" << jsonEscape(frame.phoneme)
            << "\", \"startSample\": " << frame.startSample
            << ", \"endSample\": " << frame.endSample
            << ", \"start\": " << fixed(frame.startTime, 6)
            << ", \"end\": " << fixed(frame.endTime, 6) << "}";
        out << (i + 1 < visemes.size() ? ",\n" : "\n");
    }
    out << "  ]\n}\n";
    return out.str();
}

std::string timingLrc(const TimingPlan &plan) {
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out << "[ar:formant]\n[re:formant timing export]\n";
    if (plan.words.empty()) {
        return out.str();
    }
    out << '[' << lrcTime(plan.words.front().startTime) << ']';
    for (const TimedUnit &word : plan.words) {
        out << '<' << lrcTime(word.startTime) << '>' << oneLine(word.label)
            << ' ';
    }
    out << '\n';
    return out.str();
}

std::string timingSrt(const TimingPlan &plan) {
    std::ostringstream out;
    out.imbue(std::locale::classic());
    for (size_t i = 0; i < plan.words.size(); ++i) {
        const TimedUnit &word = plan.words[i];
        out << (i + 1) << '\n'
            << clockTime(word.startTime, ',') << " --> "
            << clockTime(word.endTime, ',') << '\n'
            << oneLine(word.label) << "\n\n";
    }
    return out.str();
}

std::string timingVtt(const TimingPlan &plan) {
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out << "WEBVTT\n\n";
    for (const TimedUnit &word : plan.words) {
        out << clockTime(word.startTime, '.') << " --> "
            << clockTime(word.endTime, '.') << '\n'
            << oneLine(word.label) << "\n\n";
    }
    return out.str();
}

} // namespace formant
