#include "formant/ipa.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include "formant/synthesizer.h"

namespace formant {
namespace {

// ---------------------------------------------------------------------------
// UTF-8 helpers. The parser never relies on the process locale.
// ---------------------------------------------------------------------------

bool decodeUtf8(std::string_view text, size_t &pos, uint32_t &codePoint) {
    if (pos >= text.size()) {
        return false;
    }
    const auto byte = [&](size_t at) {
        return static_cast<unsigned char>(text[at]);
    };
    const uint32_t first = byte(pos);
    if (first < 0x80) {
        codePoint = first;
        ++pos;
        return true;
    }
    int extra = 0;
    uint32_t value = 0;
    if (first >= 0xC2 && first <= 0xDF) {
        extra = 1;
        value = first & 0x1Fu;
    } else if (first >= 0xE0 && first <= 0xEF) {
        extra = 2;
        value = first & 0x0Fu;
    } else if (first >= 0xF0 && first <= 0xF4) {
        extra = 3;
        value = first & 0x07u;
    } else {
        return false;
    }
    if (pos + static_cast<size_t>(extra) >= text.size()) {
        return false;
    }
    for (int i = 1; i <= extra; ++i) {
        const uint32_t next = byte(pos + static_cast<size_t>(i));
        if ((next & 0xC0u) != 0x80u) {
            return false;
        }
        value = (value << 6) | (next & 0x3Fu);
    }
    if ((extra == 2 && value < 0x800) || (extra == 3 && value < 0x10000) ||
        value > 0x10FFFF || (value >= 0xD800 && value <= 0xDFFF)) {
        return false;
    }
    codePoint = value;
    pos += static_cast<size_t>(extra) + 1;
    return true;
}

std::string encodeUtf8(uint32_t codePoint) {
    std::string result;
    if (codePoint < 0x80) {
        result.push_back(static_cast<char>(codePoint));
    } else if (codePoint < 0x800) {
        result.push_back(static_cast<char>(0xC0u | (codePoint >> 6)));
        result.push_back(static_cast<char>(0x80u | (codePoint & 0x3Fu)));
    } else if (codePoint < 0x10000) {
        result.push_back(static_cast<char>(0xE0u | (codePoint >> 12)));
        result.push_back(static_cast<char>(0x80u | ((codePoint >> 6) & 0x3Fu)));
        result.push_back(static_cast<char>(0x80u | (codePoint & 0x3Fu)));
    } else {
        result.push_back(static_cast<char>(0xF0u | (codePoint >> 18)));
        result.push_back(
            static_cast<char>(0x80u | ((codePoint >> 12) & 0x3Fu)));
        result.push_back(static_cast<char>(0x80u | ((codePoint >> 6) & 0x3Fu)));
        result.push_back(static_cast<char>(0x80u | (codePoint & 0x3Fu)));
    }
    return result;
}

std::vector<uint32_t> codePoints(std::string_view text) {
    std::vector<uint32_t> result;
    size_t pos = 0;
    uint32_t cp = 0;
    while (pos < text.size()) {
        if (!decodeUtf8(text, pos, cp)) {
            throw std::invalid_argument("invalid UTF-8 in IPA transcription");
        }
        result.push_back(cp);
    }
    return result;
}

// ---------------------------------------------------------------------------
// Articulatory model. Each IPA letter maps to a Sound; the renderer turns the
// Sound into the same four formant bands used by the English phoneme table.
// ---------------------------------------------------------------------------

enum class Manner {
    Silence,
    Vowel,
    Plosive,
    Nasal,
    Fricative,
    LateralFricative,
    Approximant,
    LateralApproximant,
    Trill,
    Tap,
    Click,
    Implosive,
    Ejective,
};

enum class Place {
    Bilabial,
    Labiodental,
    Dental,
    Alveolar,
    Postalveolar,
    Retroflex,
    Palatal,
    Velar,
    Uvular,
    Pharyngeal,
    Glottal,
    LabialVelar,
    LabialPalatal,
    Alveolopalatal,
    Epiglottal,
};

struct Sound {
    Manner manner = Manner::Silence;
    Place place = Place::Alveolar;
    bool voiced = false;
    // Vowels: 0 close, 1 near-close, 2 close-mid, 3 mid, 4 open-mid,
    // 5 near-open, 6 open. Consonants ignore height/back/rounded.
    int height = 2;
    int back = 0; // -1 front, 0 central, 1 back
    bool rounded = false;
    bool rhotic = false;
    double duration = 0.1;
};

Sound vowel(int height, int back, bool rounded = false) {
    Sound result;
    result.manner = Manner::Vowel;
    result.voiced = true;
    result.height = std::clamp(height, 0, 6);
    result.back = std::clamp(back, -1, 1);
    result.rounded = rounded;
    result.duration = 0.14;
    return result;
}

Sound consonant(Manner manner, Place place, bool voiced) {
    Sound result;
    result.manner = manner;
    result.place = place;
    result.voiced = voiced;
    switch (manner) {
    case Manner::Plosive:
        result.duration = 0.035;
        break;
    case Manner::Nasal:
        result.duration = 0.08;
        break;
    case Manner::Fricative:
    case Manner::LateralFricative:
        result.duration = 0.09;
        break;
    case Manner::Trill:
        result.duration = 0.10;
        break;
    case Manner::Tap:
        result.duration = 0.025;
        break;
    case Manner::Click:
        result.duration = 0.04;
        break;
    case Manner::Implosive:
    case Manner::Ejective:
        result.duration = 0.045;
        break;
    default:
        result.duration = 0.07;
        break;
    }
    return result;
}

struct Prosody {
    double duration = 1.0;
    double amplitude = 1.0;
    double pitchStart = 1.0;
    double pitchEnd = 1.0;
    bool breathy = false;
    bool creaky = false;
    bool nasalized = false;
    bool pitchRise = false;
};

std::array<FormantBand, 4> consonantBands(const Sound &sound) {
    struct PlaceBands {
        std::array<double, 4> frequencies;
    };
    static const std::unordered_map<Place, PlaceBands> places = {
        {Place::Bilabial, {{300, 800, 2200, 3200}}},
        {Place::Labiodental, {{400, 1200, 2600, 4000}}},
        {Place::Dental, {{400, 1400, 2800, 4000}}},
        {Place::Alveolar, {{400, 1800, 4500, 6500}}},
        {Place::Postalveolar, {{400, 1600, 3000, 4500}}},
        {Place::Retroflex, {{300, 1200, 2200, 3200}}},
        {Place::Palatal, {{300, 2200, 3500, 5000}}},
        {Place::Velar, {{350, 1400, 2600, 4000}}},
        {Place::Uvular, {{350, 1100, 2000, 3200}}},
        {Place::Pharyngeal, {{500, 900, 1600, 2400}}},
        {Place::Glottal, {{500, 1500, 2500, 3500}}},
        {Place::LabialVelar, {{300, 800, 2200, 3200}}},
        {Place::LabialPalatal, {{300, 1900, 2800, 3500}}},
        {Place::Alveolopalatal, {{400, 2000, 3200, 4500}}},
        {Place::Epiglottal, {{500, 800, 1500, 2200}}},
    };
    static const std::unordered_map<Manner, std::array<double, 4>> profiles = {
        {Manner::Plosive, {0.30, 0.50, 0.70, 0.50}},
        {Manner::Nasal, {1.00, 0.30, 0.10, 0.05}},
        {Manner::Fricative, {0.15, 0.50, 1.00, 0.70}},
        {Manner::LateralFricative, {0.40, 0.70, 0.90, 0.50}},
        {Manner::Approximant, {1.00, 0.60, 0.30, 0.10}},
        {Manner::LateralApproximant, {1.00, 0.60, 0.30, 0.10}},
        {Manner::Trill, {0.80, 0.60, 0.40, 0.20}},
        {Manner::Tap, {0.70, 0.60, 0.50, 0.30}},
        {Manner::Click, {0.50, 0.80, 1.00, 0.70}},
        {Manner::Implosive, {0.35, 0.55, 0.70, 0.45}},
        {Manner::Ejective, {0.40, 0.60, 0.85, 0.60}},
    };

    const PlaceBands &place = places.at(sound.place);
    const std::array<double, 4> &levels = profiles.count(sound.manner)
                                              ? profiles.at(sound.manner)
                                              : profiles.at(Manner::Plosive);
    std::array<FormantBand, 4> bands{};
    for (size_t i = 0; i < bands.size(); ++i) {
        bands[i] = {place.frequencies[i], levels[i], 100.0 + 60.0 * i};
    }
    return bands;
}

std::array<FormantBand, 4> vowelBands(const Sound &sound) {
    static const double f1[] = {300, 450, 550, 600, 700, 800, 850};
    static const double frontF2[] = {2300, 2000, 1900, 1800, 1800, 1700, 1600};
    static const double centralF2[] = {1600, 1400, 1400, 1500,
                                       1300, 1400, 1300};
    static const double backF2[] = {900, 1100, 1000, 1000, 900, 900, 1100};
    const int h = std::clamp(sound.height, 0, 6);
    const double *f2 = sound.back < 0   ? frontF2
                       : sound.back > 0 ? backF2
                                        : centralF2;
    double first = f1[h];
    double second = f2[h];
    double third = sound.back < 0 ? 2800.0 : 2450.0;
    if (sound.rounded) {
        second *= 0.88;
        third *= 0.96;
    }
    if (sound.rhotic) {
        first *= 0.95;
        second *= 0.92;
        third = 1700.0;
    }
    return {FormantBand{first, 1.0, 100.0},
            FormantBand{second, sound.back < 0 ? 0.65 : 0.55, 130.0},
            FormantBand{third, sound.rhotic ? 0.45 : 0.28, 180.0},
            FormantBand{3500.0, 0.08, 250.0}};
}

PhonemeKind kindOf(Manner manner) {
    switch (manner) {
    case Manner::Vowel:
        return PhonemeKind::Voiced;
    case Manner::Nasal:
        return PhonemeKind::Nasal;
    case Manner::Fricative:
    case Manner::LateralFricative:
        return PhonemeKind::Fricative;
    case Manner::Plosive:
    case Manner::Click:
    case Manner::Implosive:
    case Manner::Ejective:
        return PhonemeKind::Plosive;
    case Manner::Approximant:
    case Manner::LateralApproximant:
    case Manner::Trill:
    case Manner::Tap:
        return PhonemeKind::Approximant;
    default:
        return PhonemeKind::Silence;
    }
}

Phoneme makePhoneme(const Sound &sound, const std::string &name,
                    const Prosody &prosody) {
    Phoneme result;
    result.name = name;
    result.kind = kindOf(sound.manner);
    result.bands = sound.manner == Manner::Vowel ? vowelBands(sound)
                                                 : consonantBands(sound);
    result.duration = sound.duration * prosody.duration;
    result.voiced = sound.voiced;
    result.amplitude = prosody.amplitude;
    result.pitchStart = prosody.pitchStart * (prosody.creaky ? 0.85 : 1.0);
    result.pitchEnd = prosody.pitchEnd;
    result.pitchRise = prosody.pitchRise;
    if (prosody.nasalized && !result.bands[0].frequency) {
        return result;
    }
    if (prosody.nasalized) {
        result.bands[1].amplitude *= 0.65;
        result.bands[2].amplitude *= 0.60;
        result.bands[0].bandwidth += 40.0;
    }
    if (prosody.breathy) {
        result.bands[3].amplitude *= 1.20;
    }
    return result;
}

// ---------------------------------------------------------------------------
// The official IPA chart plus widely used extensions and ASCII aliases.
// ---------------------------------------------------------------------------

struct BaseSymbol {
    std::string text;
    std::vector<Sound> sounds;
};

BaseSymbol symbol(std::string_view text, Sound sound) {
    return {std::string(text), {sound}};
}

BaseSymbol compound(std::string_view text, Sound first, Sound second) {
    first.duration *= 0.55;
    return {std::string(text), {first, second}};
}

const std::vector<BaseSymbol> &baseSymbols() {
    using enum Manner;
    using enum Place;
    static const std::vector<BaseSymbol> table = {
        // Pulmonic plosives.
        symbol("p", consonant(Plosive, Bilabial, false)),
        symbol("b", consonant(Plosive, Bilabial, true)),
        symbol("t", consonant(Plosive, Alveolar, false)),
        symbol("d", consonant(Plosive, Alveolar, true)),
        symbol("\u0288", consonant(Plosive, Retroflex, false)), // ʈ
        symbol("\u0256", consonant(Plosive, Retroflex, true)),  // ɖ
        symbol("c", consonant(Plosive, Palatal, false)),
        symbol("\u025F", consonant(Plosive, Palatal, true)), // ɟ
        symbol("k", consonant(Plosive, Velar, false)),
        symbol("\u0261", consonant(Plosive, Velar, true)), // ɡ
        symbol("g", consonant(Plosive, Velar, true)),      // common alias
        symbol("q", consonant(Plosive, Uvular, false)),
        symbol("\u0262", consonant(Plosive, Uvular, true)),      // ɢ
        symbol("\u0294", consonant(Plosive, Glottal, false)),    // ʔ
        symbol("\u02A1", consonant(Plosive, Epiglottal, false)), // ʡ

        // Nasals.
        symbol("m", consonant(Nasal, Bilabial, true)),
        symbol("\u0271", consonant(Nasal, Labiodental, true)), // ɱ
        symbol("n", consonant(Nasal, Alveolar, true)),
        symbol("\u0273", consonant(Nasal, Retroflex, true)), // ɳ
        symbol("\u0272", consonant(Nasal, Palatal, true)),   // ɲ
        symbol("\u014B", consonant(Nasal, Velar, true)),     // ŋ
        symbol("\u0274", consonant(Nasal, Uvular, true)),    // ɴ

        // Trills, taps and flaps.
        symbol("\u0299", consonant(Trill, Bilabial, true)), // ʙ
        symbol("r", consonant(Trill, Alveolar, true)),
        symbol("\u0280", consonant(Trill, Uvular, true)),    // ʀ
        symbol("\u2C71", consonant(Tap, Labiodental, true)), // ⱱ
        symbol("\u027E", consonant(Tap, Alveolar, true)),    // ɾ
        symbol("\u027D", consonant(Tap, Retroflex, true)),   // ɽ
        symbol("\u027A", consonant(Tap, Alveolar, true)),    // ɺ

        // Fricatives (voiceless, voiced).
        symbol("\u0278", consonant(Fricative, Bilabial, false)), // ɸ
        symbol("\u03B2", consonant(Fricative, Bilabial, true)),  // β
        symbol("f", consonant(Fricative, Labiodental, false)),
        symbol("v", consonant(Fricative, Labiodental, true)),
        symbol("\u03B8", consonant(Fricative, Dental, false)), // θ
        symbol("\u00F0", consonant(Fricative, Dental, true)),  // ð
        symbol("s", consonant(Fricative, Alveolar, false)),
        symbol("z", consonant(Fricative, Alveolar, true)),
        symbol("\u0283", consonant(Fricative, Postalveolar, false)), // ʃ
        symbol("\u0292", consonant(Fricative, Postalveolar, true)),  // ʒ
        symbol("\u0282", consonant(Fricative, Retroflex, false)),    // ʂ
        symbol("\u0290", consonant(Fricative, Retroflex, true)),     // ʐ
        symbol("\u00E7", consonant(Fricative, Palatal, false)),      // ç
        symbol("\u029D", consonant(Fricative, Palatal, true)),       // ʝ
        symbol("x", consonant(Fricative, Velar, false)),
        symbol("\u0263", consonant(Fricative, Velar, true)),       // ɣ
        symbol("\u03C7", consonant(Fricative, Uvular, false)),     // χ
        symbol("\u0281", consonant(Fricative, Uvular, true)),      // ʁ
        symbol("\u0127", consonant(Fricative, Pharyngeal, false)), // ħ
        symbol("\u0295", consonant(Fricative, Pharyngeal, true)),  // ʕ
        symbol("h", consonant(Fricative, Glottal, false)),
        symbol("\u0266", consonant(Fricative, Glottal, true)),          // ɦ
        symbol("\u026C", consonant(LateralFricative, Alveolar, false)), // ɬ
        symbol("\u026E", consonant(LateralFricative, Alveolar, true)),  // ɮ
        symbol("\u0255", consonant(Fricative, Alveolopalatal, false)),  // ɕ
        symbol("\u0291", consonant(Fricative, Alveolopalatal, true)),   // ʑ
        symbol("\u028D", consonant(Fricative, LabialVelar, false)),     // ʍ
        symbol("\u0267", consonant(Fricative, LabialPalatal, false)),   // ɧ
        symbol("\u029C", consonant(Fricative, Epiglottal, false)),      // ʜ
        symbol("\u02A2", consonant(Fricative, Epiglottal, true)),       // ʢ

        // Approximants.
        symbol("\u028B", consonant(Approximant, Labiodental, true)), // ʋ
        symbol("\u0279", consonant(Approximant, Alveolar, true)),    // ɹ
        symbol("\u027B", consonant(Approximant, Retroflex, true)),   // ɻ
        symbol("j", consonant(Approximant, Palatal, true)),
        symbol("\u0270", consonant(Approximant, Velar, true)), // ɰ
        symbol("w", consonant(Approximant, LabialVelar, true)),
        symbol("\u0265", consonant(Approximant, LabialPalatal, true)), // ɥ
        symbol("l", consonant(LateralApproximant, Alveolar, true)),
        symbol("\u026B", consonant(LateralApproximant, Velar, true)),     // ɫ
        symbol("\u026D", consonant(LateralApproximant, Retroflex, true)), // ɭ
        symbol("\u028E", consonant(LateralApproximant, Palatal, true)),   // ʎ
        symbol("\u029F", consonant(LateralApproximant, Velar, true)),     // ʟ

        // Clicks.
        symbol("\u0298", consonant(Click, Bilabial, false)),       // ʘ
        symbol("\u01C0", consonant(Click, Dental, false)),         // ǀ
        symbol("\u01C3", consonant(Click, Postalveolar, false)),   // ǃ
        symbol("\u01C2", consonant(Click, Alveolopalatal, false)), // ǂ
        symbol("\u01C1", consonant(Click, Alveolar, false)),       // ǁ
        symbol("\U0001DF0A", consonant(Click, Retroflex, false)),  // 𝼊
        symbol("\u029E", consonant(Click, Velar, false)),          // ʞ
        symbol("\u0297", consonant(Click, Alveolar, false)),       // ʗ

        // Implosives.
        symbol("\u0253", consonant(Implosive, Bilabial, true)),      // ɓ
        symbol("\u0257", consonant(Implosive, Alveolar, true)),      // ɗ
        symbol("\u0284", consonant(Implosive, Palatal, true)),       // ʄ
        symbol("\u0260", consonant(Implosive, Velar, true)),         // ɠ
        symbol("\u029B", consonant(Implosive, Uvular, true)),        // ʛ
        symbol("\U00001D91", consonant(Implosive, Retroflex, true)), // ᶑ
        symbol("\uA7AF", consonant(Plosive, Pharyngeal, false)),     // ꞯ

        // Legacy affricate singletons and common ligatures.
        compound("\u02A6", consonant(Plosive, Alveolar, false),
                 consonant(Fricative, Alveolar, false)), // ʦ
        compound("\u02A7", consonant(Plosive, Alveolar, false),
                 consonant(Fricative, Postalveolar, false)), // ʧ
        compound("\u02A8", consonant(Plosive, Alveolar, false),
                 consonant(Fricative, Alveolopalatal, false)), // ʨ
        compound("\u02A3", consonant(Plosive, Alveolar, true),
                 consonant(Fricative, Alveolar, true)), // ʣ
        compound("\u02A4", consonant(Plosive, Alveolar, true),
                 consonant(Fricative, Postalveolar, true)), // ʤ
        compound("\u02A5", consonant(Plosive, Alveolar, true),
                 consonant(Fricative, Alveolopalatal, true)), // ʥ

        // Vowels: close, near-close, close-mid, mid, open-mid, near-open, open.
        symbol("i", vowel(0, -1)), symbol("y", vowel(0, -1, true)),
        symbol("\u0268", vowel(0, 0)),                                  // ɨ
        symbol("\u0289", vowel(0, 0, true)),                            // ʉ
        symbol("\u026F", vowel(0, 1)),                                  // ɯ
        symbol("u", vowel(0, 1, true)), symbol("\u026A", vowel(1, -1)), // ɪ
        symbol("\u028F", vowel(1, -1, true)),                           // ʏ
        symbol("\u028A", vowel(1, 1, true)),                            // ʊ
        symbol("\u026A\u0339", vowel(1, 0)), // ᵻ-style central near-close
        symbol("\u028A\u031C", vowel(1, 0, true)), symbol("e", vowel(2, -1)),
        symbol("\u00F8", vowel(2, -1, true)),                          // ø
        symbol("\u0258", vowel(2, 0)),                                 // ɘ
        symbol("\u0275", vowel(2, 0, true)),                           // ɵ
        symbol("\u0264", vowel(2, 1)),                                 // ɤ
        symbol("o", vowel(2, 1, true)), symbol("\u0259", vowel(3, 0)), // ə
        symbol("\u1D57", vowel(3, 0)),        // ᵗ central alias
        symbol("\u025B", vowel(4, -1)),       // ɛ
        symbol("\u0153", vowel(4, -1, true)), // œ
        symbol("\u025C", vowel(4, 0)),        // ɜ
        symbol("\u025E", vowel(4, 0, true)),  // ɞ
        symbol("\u028C", vowel(4, 1)),        // ʌ
        symbol("\u0254", vowel(4, 1, true)),  // ɔ
        symbol("\u00E6", vowel(5, -1)),       // æ
        symbol("\u0250", vowel(5, 0)),        // ɐ
        symbol("a", vowel(6, -1)), symbol("\u00E4", vowel(6, 0)), // ä
        symbol("\u0276", vowel(6, -1, true)),                     // ɶ
        symbol("\u0251", vowel(6, 1)),                            // ɑ
        symbol("\u0252", vowel(6, 1, true)),                      // ɒ
        symbol("\u025A", vowel(3, 0)),                            // ɚ
        symbol("\u025D", vowel(4, 0)),                            // ɝ

        // Extension laterals and rare sounds: parsed and rendered from
        // articulatory components rather than needing bespoke synthesis.
        symbol("\U0001DF04", consonant(LateralFricative, Velar, false)),
        symbol("\U0001DF06", consonant(LateralFricative, Palatal, false)),
        symbol("\uA78E", consonant(LateralFricative, Retroflex, false)),
        symbol("\U0001DF05", consonant(LateralFricative, Retroflex, true)),
        symbol("\U0001DF08", consonant(Tap, Retroflex, true)),
        symbol("\u024F", vowel(2, -1)), // ɏ
    };
    return table;
}

const std::unordered_map<std::string_view, std::vector<Sound>> &baseMap() {
    static const std::unordered_map<std::string_view, std::vector<Sound>> map =
        [] {
            std::unordered_map<std::string_view, std::vector<Sound>> result;
            for (const BaseSymbol &entry : baseSymbols()) {
                result.emplace(std::string_view(entry.text), entry.sounds);
            }
            return result;
        }();
    return map;
}

// ---------------------------------------------------------------------------
// Diacritics, stress, length and tone.
// ---------------------------------------------------------------------------

bool isSpace(uint32_t cp) {
    return cp == ' ' || cp == '\t' || cp == '\r' || cp == '\n';
}

bool isToneBar(uint32_t cp) {
    return (cp >= 0x02E5 && cp <= 0x02E9) || (cp >= 0xA712 && cp <= 0xA716);
}

double toneLevel(uint32_t cp) {
    if (cp >= 0x02E5 && cp <= 0x02E9) {
        // ˥ high ... ˩ low
        return 1.30 - 0.145 * static_cast<double>(cp - 0x02E5);
    }
    // Left-stem bars run extra-high (A712) down to extra-low (A716).
    return 1.30 - 0.145 * static_cast<double>(cp - 0xA712);
}

bool isModifier(uint32_t cp) {
    switch (cp) {
    case 0x0300: // grave / low tone
    case 0x0301: // acute / high tone
    case 0x0302: // circumflex / falling
    case 0x0303: // nasal
    case 0x0304: // macron / mid
    case 0x0306: // extra-short
    case 0x0308: // centralized
    case 0x030A: // voiceless above
    case 0x030B: // extra-high
    case 0x030C: // caron / rising
    case 0x030D: // syllabic above
    case 0x030F: // extra-low
    case 0x0311: // non-syllabic above
    case 0x0318: // advanced tongue root
    case 0x0319: // retracted tongue root
    case 0x031A: // no audible release
    case 0x031C: // less rounded
    case 0x031D: // raised
    case 0x031E: // lowered
    case 0x031F: // advanced
    case 0x0320: // retracted
    case 0x0324: // breathy
    case 0x0325: // voiceless
    case 0x0329: // syllabic
    case 0x032A: // dental
    case 0x032C: // voiced
    case 0x032F: // non-syllabic
    case 0x0330: // creaky
    case 0x0334: // velarized / pharyngealized
    case 0x0339: // more rounded
    case 0x033C: // linguolabial
    case 0x033D: // mid-centralized
    case 0x0346: // dental above
    case 0x0357: // more rounded above
    case 0x035C: // bottom tie bar
    case 0x0361: // top tie bar
    case 0x02B0: // aspirated
    case 0x02B2: // palatalized
    case 0x02B7: // labialized
    case 0x02BC: // ejective
    case 0x02DE: // rhotic
    case 0x02E0: // velarized
    case 0x02E1: // lateral release
    case 0x02E3: // velar release
    case 0x02E4: // pharyngealized
    case 0x02D2: // more rounded modifier
    case 0x02D3: // less rounded modifier
    case 0x02D4: // raised modifier
    case 0x02D5: // lowered modifier
    case 0x02D6: // advanced modifier
    case 0x02D7: // retracted modifier
    case 0x1D4A: // mid central vowel release
    case 0x1DBF: // dental fricative release
    case 0x207F: // nasal release
    case 0x1DC4: // high rising
    case 0x1DC5: // low rising
    case 0x1DC6: // low falling
    case 0x1DC7: // high falling
    case 0x1DC8: // rising-falling
    case 0x1DC9: // falling-rising
        return true;
    default:
        return isToneBar(cp);
    }
}

void setContour(Prosody &prosody, double start, double end) {
    prosody.pitchStart = start;
    prosody.pitchEnd = end;
}

void applyModifier(Sound &sound, Prosody &prosody, uint32_t cp) {
    switch (cp) {
    case 0x0325: // voiceless below
    case 0x030A: // voiceless above
        sound.voiced = false;
        break;
    case 0x032C: // voiced
        sound.voiced = true;
        break;
    case 0x02B0: // aspirated
        prosody.duration *= 1.15;
        prosody.breathy = true;
        break;
    case 0x0324: // breathy
        prosody.breathy = true;
        break;
    case 0x0330: // creaky
        prosody.creaky = true;
        break;
    case 0x0303: // nasalized
        prosody.nasalized = true;
        break;
    case 0x0308: // centralized
    case 0x02D6:
        sound.back = 0;
        break;
    case 0x033D: // mid-centralized
        sound.back = 0;
        sound.height = 3;
        break;
    case 0x031F: // advanced
        sound.back = -1;
        break;
    case 0x0320: // retracted
    case 0x02D7:
        sound.back = 1;
        break;
    case 0x031D: // raised
    case 0x02D4:
        sound.height = std::max(0, sound.height - 1);
        break;
    case 0x031E: // lowered
    case 0x02D5:
        sound.height = std::min(6, sound.height + 1);
        break;
    case 0x0339: // more rounded
    case 0x0357:
    case 0x02D2:
        sound.rounded = true;
        break;
    case 0x031C: // less rounded
    case 0x02D3:
        sound.rounded = false;
        break;
    case 0x02DE: // rhotic / retroflex
        if (sound.manner == Manner::Vowel) {
            sound.rhotic = true;
        } else {
            sound.place = Place::Retroflex;
        }
        break;
    case 0x0329: // syllabic
    case 0x030D:
        prosody.duration *= 1.20;
        break;
    case 0x032F: // non-syllabic
    case 0x0311:
        prosody.duration *= 0.60;
        break;
    case 0x0306: // extra-short
        prosody.duration *= 0.50;
        break;
    case 0x032A: // dental
    case 0x0346:
    case 0x033C: // linguolabial
    case 0x1DBF:
        sound.place = Place::Dental;
        break;
    case 0x02B2: // palatalized
        sound.place = Place::Palatal;
        break;
    case 0x02E0: // velarized
    case 0x0334:
        sound.place = Place::Velar;
        break;
    case 0x02E4: // pharyngealized
        sound.place = Place::Pharyngeal;
        break;
    case 0x02B7: // labialized
        sound.rounded = true;
        break;
    case 0x031A: // unreleased
        prosody.duration *= 0.70;
        break;
    case 0x207F: // nasal release
    case 0x02E1: // lateral release
    case 0x02E3: // velar release
    case 0x1D4A: // mid-central vowel release
    case 0x0318: // advanced tongue root
    case 0x0319: // retracted tongue root
    case 0x033A: // apical
    case 0x033B: // laminal
    case 0x035C: // tie bar
    case 0x0361: // tie bar
        break;
    case 0x02BC: // ejective
        sound.manner = Manner::Ejective;
        break;
    case 0x0300: // low
        setContour(prosody, 0.85, 0.85);
        break;
    case 0x0301: // high
        setContour(prosody, 1.18, 1.18);
        break;
    case 0x0304: // mid
        setContour(prosody, 1.0, 1.0);
        break;
    case 0x030B: // extra-high
        setContour(prosody, 1.30, 1.30);
        break;
    case 0x030F: // extra-low
        setContour(prosody, 0.72, 0.72);
        break;
    case 0x030C: // rising
        setContour(prosody, 0.85, 1.25);
        break;
    case 0x0302: // falling
        setContour(prosody, 1.25, 0.85);
        break;
    case 0x1DC4: // high rising
        setContour(prosody, 1.0, 1.25);
        break;
    case 0x1DC5: // low rising
        setContour(prosody, 0.80, 1.0);
        break;
    case 0x1DC6: // low falling
        setContour(prosody, 1.0, 0.80);
        break;
    case 0x1DC7: // high falling
        setContour(prosody, 1.25, 1.0);
        break;
    case 0x1DC8: // rising-falling
        setContour(prosody, 1.20, 0.90);
        break;
    case 0x1DC9: // falling-rising
        setContour(prosody, 0.90, 1.20);
        break;
    default:
        if (isToneBar(cp)) {
            setContour(prosody, toneLevel(cp), toneLevel(cp));
        }
        break;
    }
}

bool isSuprasegmental(uint32_t cp) {
    switch (cp) {
    case 0x02C8: // primary stress
    case 0x02CC: // secondary stress
    case 0x02D0: // long
    case 0x02D1: // half-long
    case '|':
    case 0x2016: // major group
    case '.':    // syllable break
    case 0x203F: // linking
    case 0x2197: // global rise
    case 0x2198: // global fall
    case 0xA71B: // upstep
    case 0xA71C: // downstep
        return true;
    default:
        return false;
    }
}

// ---------------------------------------------------------------------------
// IPA parser.
// ---------------------------------------------------------------------------

std::vector<Phoneme> parseIpaImpl(std::string_view text) {
    const std::vector<uint32_t> points = codePoints(text);
    std::vector<Phoneme> result;
    Prosody pending;
    bool havePending = false;
    std::vector<uint32_t> pendingModifiers;
    size_t i = 0;

    // Strip transcription delimiters when the whole string is wrapped.
    size_t first = 0;
    size_t last = points.size();
    while (first < last && isSpace(points[first])) {
        ++first;
    }
    while (last > first && isSpace(points[last - 1])) {
        --last;
    }
    if (last - first >= 2 &&
        ((points[first] == '/' && points[last - 1] == '/') ||
         (points[first] == '[' && points[last - 1] == ']'))) {
        ++first;
        --last;
    }

    for (i = first; i < last;) {
        const uint32_t cp = points[i];
        if (isSpace(cp)) {
            ++i;
            continue;
        }

        if (cp == 0x02C8 || cp == 0x02CC) { // stress applies to the next sound
            pending = Prosody{};
            pending.amplitude = cp == 0x02C8 ? 1.25 : 1.10;
            pending.duration = cp == 0x02C8 ? 1.15 : 1.05;
            pending.pitchStart = cp == 0x02C8 ? 1.06 : 1.02;
            pending.pitchEnd = cp == 0x02C8 ? 1.06 : 1.02;
            havePending = true;
            ++i;
            continue;
        }

        if (isSuprasegmental(cp)) {
            if (cp == 0x02D0 || cp == 0x02D1) { // length follows its segment
                if (!result.empty()) {
                    result.back().duration *= cp == 0x02D0 ? 1.80 : 1.40;
                }
            } else if (cp == '|') {
                Phoneme pause;
                pause.name = "|";
                pause.kind = PhonemeKind::Silence;
                pause.duration = 0.12;
                result.push_back(pause);
            } else if (cp == 0x2016) {
                Phoneme pause;
                pause.name = "‖";
                pause.kind = PhonemeKind::Silence;
                pause.duration = 0.28;
                result.push_back(pause);
            } else if (cp == '.') {
                Phoneme pause;
                pause.name = ".";
                pause.kind = PhonemeKind::Silence;
                pause.duration = 0.06;
                result.push_back(pause);
            } else if (cp == 0x2197) {
                for (Phoneme &phoneme : result) {
                    phoneme.pitchRise = true;
                }
            } else if (cp == 0x2198) {
                if (!result.empty()) {
                    result.back().pitchEnd *= 0.80;
                }
            } else if (cp == 0xA71B || cp == 0xA71C) {
                const double shift = cp == 0xA71B ? 1.12 : 0.88;
                if (!result.empty()) {
                    result.back().pitchStart *= shift;
                    result.back().pitchEnd *= shift;
                }
            }
            // 0x203F (linking) deliberately adds no pause.
            ++i;
            continue;
        }

        // Modifiers without a preceding base attach to the next segment.
        // X-SAMPA allows prefix forms such as 'a (palatalized a).
        if (isModifier(cp)) {
            while (i < last && isModifier(points[i])) {
                pendingModifiers.push_back(points[i]);
                ++i;
            }
            continue;
        }

        std::string symbol = encodeUtf8(cp);
        auto found = baseMap().find(std::string_view(symbol));
        if (found == baseMap().end()) {
            throw std::invalid_argument("unknown IPA symbol: " + symbol);
        }
        ++i;

        Prosody prosody = havePending ? pending : Prosody{};
        havePending = false;
        std::string name = symbol;
        bool tieNext = false;
        std::vector<Sound> sounds = found->second;

        for (const uint32_t mod : pendingModifiers) {
            if (mod == 0x0361 || mod == 0x035C) {
                tieNext = true;
            } else if (isToneBar(mod)) {
                setContour(prosody, toneLevel(mod), toneLevel(mod));
            } else {
                applyModifier(sounds.back(), prosody, mod);
            }
            name = encodeUtf8(mod) + name;
        }
        pendingModifiers.clear();

        while (i < last && isModifier(points[i])) {
            const uint32_t mod = points[i];
            if (mod == 0x0361 || mod == 0x035C) {
                tieNext = true;
                ++i;
                continue;
            }
            if (isToneBar(mod)) {
                const size_t runStart = i;
                while (i < last && isToneBar(points[i])) {
                    name += encodeUtf8(points[i]);
                    ++i;
                }
                setContour(prosody, toneLevel(points[runStart]),
                           toneLevel(points[i - 1]));
                continue;
            }
            applyModifier(sounds.back(), prosody, mod);
            name += encodeUtf8(mod);
            ++i;
        }

        for (size_t index = 0; index < sounds.size(); ++index) {
            Sound sound = sounds[index];
            if (tieNext && index + 1 == sounds.size()) {
                sound.duration *= 0.55;
            }
            result.push_back(makePhoneme(sound, name, prosody));
        }
    }
    if (!pendingModifiers.empty() && !result.empty()) {
        Phoneme &last = result.back();
        Prosody trailing;
        trailing.pitchStart = last.pitchStart;
        trailing.pitchEnd = last.pitchEnd;
        trailing.pitchRise = last.pitchRise;
        Sound ignored;
        for (const uint32_t mod : pendingModifiers) {
            applyModifier(ignored, trailing, mod);
        }
        last.pitchStart = trailing.pitchStart;
        last.pitchEnd = trailing.pitchEnd;
        last.pitchRise = trailing.pitchRise;
    }
    return result;
}

// ---------------------------------------------------------------------------
// X-SAMPA translation. Longest match first handles \ and _ modifiers.
// ---------------------------------------------------------------------------

using Translation = std::pair<std::string_view, std::string_view>;

const std::vector<Translation> &xsampaTable() {
    static const std::vector<Translation> table = {
        // Segments.
        {"a", "a"},
        {"b", "b"},
        {"b_<", "\u0253"},
        {"c", "c"},
        {"d", "d"},
        {"d`", "\u0256"},
        {"d_<", "\u0257"},
        {"e", "e"},
        {"f", "f"},
        {"g", "\u0261"},
        {"g_<", "\u0260"},
        {"h", "h"},
        {"h\\", "\u0266"},
        {"i", "i"},
        {"j", "j"},
        {"j\\", "\u029D"},
        {"k", "k"},
        {"l", "l"},
        {"l`", "\u026D"},
        {"l\\", "\u027A"},
        {"m", "m"},
        {"n", "n"},
        {"n`", "\u0273"},
        {"o", "o"},
        {"p", "p"},
        {"p\\", "\u0278"},
        {"q", "q"},
        {"r", "r"},
        {"r`", "\u027D"},
        {"r\\", "\u0279"},
        {"r\\`", "\u027B"},
        {"s", "s"},
        {"s`", "\u0282"},
        {"s\\", "\u0255"},
        {"t", "t"},
        {"t`", "\u0288"},
        {"u", "u"},
        {"v", "v"},
        {"v\\", "\u028B"},
        {"w", "w"},
        {"x", "x"},
        {"x\\", "\u0267"},
        {"y", "y"},
        {"z", "z"},
        {"z`", "\u0290"},
        {"z\\", "\u0291"},
        {"A", "\u0251"},
        {"B", "\u03B2"},
        {"B\\", "\u0299"},
        {"C", "\u00E7"},
        {"D", "\u00F0"},
        {"E", "\u025B"},
        {"F", "\u0271"},
        {"G", "\u0263"},
        {"G\\", "\u0262"},
        {"G\\_<", "\u029B"},
        {"H", "\u0265"},
        {"H\\", "\u029C"},
        {"I", "\u026A"},
        {"J", "\u0272"},
        {"J\\", "\u025F"},
        {"J\\_<", "\u0284"},
        {"K", "\u026C"},
        {"K\\", "\u026E"},
        {"L", "\u028E"},
        {"L\\", "\u029F"},
        {"M", "\u026F"},
        {"M\\", "\u0270"},
        {"N", "\u014B"},
        {"N\\", "\u0274"},
        {"O", "\u0254"},
        {"O\\", "\u0298"},
        {"P", "\u028B"},
        {"Q", "\u0252"},
        {"R", "\u0281"},
        {"R\\", "\u0280"},
        {"S", "\u0283"},
        {"T", "\u03B8"},
        {"U", "\u028A"},
        {"V", "\u028C"},
        {"W", "\u028D"},
        {"X", "\u03C7"},
        {"X\\", "\u0127"},
        {"Y", "\u028F"},
        {"Z", "\u0292"},
        {"@", "\u0259"},
        {"@\\", "\u0258"},
        {"@`", "\u025A"},
        {"{", "\u00E6"},
        {"}", "\u0289"},
        {"1", "\u0268"},
        {"2", "\u00F8"},
        {"3", "\u025C"},
        {"3\\", "\u025E"},
        {"4", "\u027E"},
        {"5", "\u026B"},
        {"6", "\u0250"},
        {"7", "\u0264"},
        {"8", "\u0275"},
        {"9", "\u0153"},
        {"&", "\u0276"},
        {"?", "\u0294"},
        {"?\\", "\u0295"},
        {"<\\", "\u02A2"},
        {">\\", "\u02A1"},
        {"!\\", "\u01C3"},
        {"|\\|\\", "\u01C1"},
        {"|\\", "\u01C0"},
        {"=\\", "\u01C2"},
        // Affricates and double articulations.
        {"ts", "t\u0361s"},
        {"dz", "d\u0361z"},
        {"tS", "t\u0361\u0283"},
        {"dZ", "d\u0361\u0292"},
        {"ts\\", "t\u0361\u0255"},
        {"dz\\", "d\u0361\u0291"},
        {"tK", "t\u0361\u026C"},
        {"dK\\", "d\u0361\u026E"},
        {"kp", "k\u0361p"},
        {"gb", "\u0261\u0361b"},
        {"Nm", "\u014B\u0361m"},
        // Stress, length, tone and grouping.
        {"\"", "\u02C8"},
        {"%", "\u02CC"},
        {":", "\u02D0"},
        {":\\", "\u02D1"},
        {".", "."},
        {"|", "|"},
        {"||", "\u2016"},
        {"-", ""},
        {"/", ""},
        {"<", ""},
        {">", ""},
        {"-\\", "\u203F"},
        {"^", "\uA71B"},
        {"!", "\uA71C"},
        {"<^>", "\uA71B"},
        {"<!>", "\uA71C"},
        {"</>", "\u2197"},
        {"<\\>", "\u2198"},
        {"<R>", "\u2197"},
        {"<F>", "\u2198"},
        {"<T>", "\u02E5"},
        {"<H>", "\u02E6"},
        {"<M>", "\u02E7"},
        {"<L>", "\u02E8"},
        {"<B>", "\u02E9"},
        {"<LH>", "\u02E9\u02E5"},
        {"<BT>", "\u02E9\u02E5"},
        {"<HL>", "\u02E5\u02E9"},
        {"<TB>", "\u02E5\u02E9"},
        {"<HT>", "\u02E7\u02E5"},
        {"<BL>", "\u02E9\u02E7"},
        {"<HTH>", "\u02E7\u02E5\u02E7"},
        {"<MHM>", "\u02E7\u02E5\u02E7"},
        // Diacritics.
        {"_B_L", "\u1DC5"},
        {"_H_T", "\u1DC4"},
        {"_R_F", "\u1DC8"},
        {"_F_R", "\u1DC9"},
        {"_L_B", "\u1DC5"},
        {"_T_H", "\u1DC4"},
        {"_T_B", "\u1DC7"},
        {"_H_L", "\u1DC7"},
        {"_B_H", "\u1DC5"},
        {"_?\\", "\u02E4"},
        {"_j", "\u02B2"},
        {"'", "\u02B2"},
        {"`", "\u02DE"},
        {"~", "\u0303"},
        {"=", "\u0329"},
        {"_0", "\u0325"},
        {"_>", "\u02BC"},
        {"_<", ""},
        {"_=", "\u0329"},
        {"_\"", "\u0308"},
        {"_+", "\u031F"},
        {"_-", "\u0320"},
        {"_/", "\u030C"},
        {"_R", "\u030C"},
        {"_F", "\u0302"},
        {"_\\", "\u0302"},
        {"_^", "\u032F"},
        {"_}", "\u031A"},
        {"_~", "\u0303"},
        {"_A", "\u0318"},
        {"_a", "\u033A"},
        {"_B", "\u030F"},
        {"_c", "\u031C"},
        {"_d", "\u032A"},
        {"_e", "\u0334"},
        {"_f", "\u2198"},
        {"_G", "\u02E0"},
        {"_H", "\u0301"},
        {"_h", "\u02B0"},
        {"_k", "\u0330"},
        {"_L", "\u0300"},
        {"_l", "\u02E1"},
        {"_M", "\u0304"},
        {"_m", "\u033B"},
        {"_N", "\u033C"},
        {"_n", "\u207F"},
        {"_O", "\u0339"},
        {"_o", "\u031E"},
        {"_q", "\u0319"},
        {"_r", "\u2197"},
        {"_T", "\u030B"},
        {"_t", "\u0324"},
        {"_v", "\u032C"},
        {"_w", "\u02B7"},
        {"_X", "\u0306"},
        {"_x", "\u033D"},
    };
    return table;
}

std::string translateXsampa(std::string_view text) {
    static const std::vector<Translation> sorted = [] {
        std::vector<Translation> result = xsampaTable();
        std::stable_sort(result.begin(), result.end(),
                         [](const Translation &left, const Translation &right) {
                             return left.first.size() > right.first.size();
                         });
        return result;
    }();

    std::string result;
    size_t pos = 0;
    while (pos < text.size()) {
        if (text[pos] == ' ' || text[pos] == '\t' || text[pos] == '\r' ||
            text[pos] == '\n') {
            result.push_back(text[pos++]);
            continue;
        }
        bool matched = false;
        for (const Translation &entry : sorted) {
            if (text.substr(pos, entry.first.size()) == entry.first) {
                result.append(entry.second);
                pos += entry.first.size();
                matched = true;
                break;
            }
        }
        if (!matched) {
            throw std::invalid_argument("unknown X-SAMPA symbol: " +
                                        std::string(1, text[pos]));
        }
    }
    return result;
}

} // namespace

std::vector<Phoneme> parseIpa(std::string_view transcription) {
    return parseIpaImpl(transcription);
}

std::vector<Phoneme> parseXsampa(std::string_view transcription) {
    return parseIpaImpl(translateXsampa(transcription));
}

std::vector<std::string> ipaSymbols() {
    std::vector<std::string> result;
    for (const BaseSymbol &entry : baseSymbols()) {
        result.push_back(entry.text);
    }
    return result;
}

std::vector<float> synthesizeIpa(std::string_view transcription,
                                 const Voice &voice, int sampleRate) {
    return Synthesizer(voice).render(parseIpa(transcription), sampleRate);
}

std::vector<float> synthesizeXsampa(std::string_view transcription,
                                    const Voice &voice, int sampleRate) {
    return Synthesizer(voice).render(parseXsampa(transcription), sampleRate);
}

} // namespace formant
