#include "formant/mrt.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <stdexcept>

#include "formant/synthesizer.h"
#include "formant/wav.h"

namespace formant {
namespace {

struct Ensemble {
    const char *id;
    const char *focus; // initial or final
    const char *words[6];
};

// Canonical Modified Rhyme Test (House, Williams, Hecker & Kryter, 1965)
// 50 ensembles x 6 CVC words. Source: NIST MRT public-domain software
// (usnistgov/MRT, mrt/MRT.py) which reproduces the published list.
constexpr Ensemble kMrtEnsembles[] = {
    {"01", "initial", {"went", "sent", "bent", "dent", "tent", "rent"}},
    {"02", "initial", {"hold", "cold", "told", "fold", "sold", "gold"}},
    {"03", "final", {"pat", "pad", "pan", "path", "pack", "pass"}},
    {"04", "final", {"lane", "lay", "late", "lake", "lace", "lame"}},
    {"05", "initial", {"kit", "bit", "fit", "hit", "wit", "sit"}},
    {"06", "initial", {"must", "bust", "gust", "rust", "dust", "just"}},
    {"07", "final", {"teak", "team", "teal", "teach", "tear", "tease"}},
    {"08", "final", {"din", "dill", "dim", "dig", "dip", "did"}},
    {"09", "initial", {"bed", "led", "fed", "red", "wed", "shed"}},
    {"10", "initial", {"pin", "sin", "tin", "fin", "din", "win"}},
    {"11", "final", {"dug", "dung", "duck", "dud", "dub", "dun"}},
    {"12", "final", {"sum", "sun", "sung", "sup", "sub", "sud"}},
    {"13", "final", {"seep", "seen", "seethe", "seek", "seem", "seed"}},
    {"14", "initial", {"not", "tot", "got", "pot", "hot", "lot"}},
    {"15", "initial", {"vest", "test", "rest", "best", "west", "nest"}},
    {"16", "final", {"pig", "pill", "pin", "pip", "pit", "pick"}},
    {"17", "final", {"back", "bath", "bad", "bass", "bat", "ban"}},
    {"18", "initial", {"way", "may", "say", "pay", "day", "gay"}},
    {"19", "initial", {"pig", "big", "dig", "wig", "rig", "fig"}},
    {"20", "final", {"pale", "pace", "page", "pane", "pay", "pave"}},
    {"21", "final", {"cane", "case", "cape", "cake", "came", "cave"}},
    {"22", "initial", {"shop", "mop", "cop", "top", "hop", "pop"}},
    {"23", "initial", {"coil", "oil", "soil", "toil", "boil", "foil"}},
    {"24", "final", {"tan", "tang", "tap", "tack", "tam", "tab"}},
    {"25", "final", {"fit", "fib", "fizz", "fill", "fig", "fin"}},
    {"26", "initial", {"same", "name", "game", "tame", "came", "fame"}},
    {"27", "initial", {"peel", "reel", "feel", "eel", "keel", "heel"}},
    {"28", "initial", {"hark", "dark", "mark", "bark", "park", "lark"}},
    {"29", "final", {"heave", "hear", "heat", "heal", "heap", "heath"}},
    {"30", "final", {"cup", "cut", "cud", "cuff", "cuss", "cub"}},
    {"31", "initial", {"thaw", "law", "raw", "paw", "jaw", "saw"}},
    {"32", "initial", {"pen", "hen", "men", "then", "den", "ten"}},
    {"33", "final", {"puff", "puck", "pub", "pus", "pup", "pun"}},
    {"34", "final", {"bean", "beach", "beat", "beak", "bead", "beam"}},
    {"35", "initial", {"heat", "neat", "feat", "seat", "meat", "beat"}},
    {"36", "initial", {"dip", "sip", "hip", "tip", "lip", "rip"}},
    {"37", "final", {"kill", "kin", "kit", "kick", "king", "kid"}},
    {"38", "initial", {"hang", "sang", "bang", "rang", "fang", "gang"}},
    {"39", "initial", {"took", "cook", "look", "hook", "shook", "book"}},
    {"40", "final", {"mass", "math", "map", "mat", "man", "mad"}},
    {"41", "final", {"ray", "raze", "rate", "rave", "rake", "race"}},
    {"42", "final", {"save", "same", "sale", "sane", "sake", "safe"}},
    {"43", "initial", {"fill", "kill", "will", "hill", "till", "bill"}},
    {"44", "final", {"sill", "sick", "sip", "sing", "sit", "sin"}},
    {"45", "initial", {"bale", "gale", "sale", "tale", "pale", "male"}},
    {"46", "initial", {"wick", "sick", "kick", "lick", "pick", "tick"}},
    {"47", "final", {"peace", "peas", "peak", "peach", "peat", "peal"}},
    {"48", "final", {"bun", "bus", "but", "bug", "buck", "buff"}},
    {"49", "final", {"sag", "sat", "sass", "sack", "sad", "sap"}},
    {"50", "initial", {"fun", "sun", "bun", "gun", "run", "nun"}},
};

std::string sanitizeName(std::string_view value) {
    std::string out;
    out.reserve(value.size());
    for (char c : value) {
        if (std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '-' ||
            c == '_') {
            out.push_back(c);
        } else {
            out.push_back('_');
        }
    }
    return out;
}

void writeText(const std::filesystem::path &path, std::string_view content) {
    std::ofstream file(path, std::ios::binary);
    if (!file) {
        throw std::runtime_error("cannot write file: " + path.string());
    }
    file << content;
    if (!file) {
        throw std::runtime_error("cannot write file: " + path.string());
    }
}

} // namespace

std::vector<MrtEnsemble> mrtEnsembles() {
    std::vector<MrtEnsemble> out;
    out.reserve(std::size(kMrtEnsembles));
    for (const Ensemble &e : kMrtEnsembles) {
        MrtEnsemble item;
        item.id = e.id;
        item.focus = e.focus;
        for (int i = 0; i < 6; ++i) {
            item.words[i] = e.words[i];
        }
        out.push_back(std::move(item));
    }
    return out;
}

MrtBatteryResult writeMrtBattery(const MrtBatteryOptions &options) {
    if (options.outDir.empty()) {
        throw std::invalid_argument("MRT output directory must not be empty");
    }
    if (options.sampleRate < 8000) {
        throw std::invalid_argument("MRT sample rate must be at least 8000");
    }

    std::filesystem::path root(options.outDir);
    std::filesystem::create_directories(root);

    Synthesizer synth(options.voice);
    MrtBatteryResult result;
    result.outDir = root.string();

    std::ostringstream response;
    response << "trial,wav,ensemble,focus,target,response,correct,voice,rate,"
                "pitch\n";
    std::ostringstream key;
    key << "trial,wav,ensemble,focus,target,alternatives\n";

    int trial = 1;
    for (const Ensemble &ensemble : kMrtEnsembles) {
        for (int i = 0; i < 6; ++i) {
            const std::string word = ensemble.words[i];
            const std::vector<Phoneme> phonemes =
                phonemize(word, options.language);
            const std::vector<float> samples =
                synth.render(phonemes, options.sampleRate);

            std::ostringstream name;
            name << "mrt_" << std::setw(3) << std::setfill('0') << trial << "_e"
                 << ensemble.id << "_" << ensemble.focus << "_"
                 << sanitizeName(word) << ".wav";
            const std::filesystem::path wavPath = root / name.str();
            writeWav(wavPath.string(), samples, options.sampleRate);

            std::ostringstream alternatives;
            for (int j = 0; j < 6; ++j) {
                if (j != 0) {
                    alternatives << '|';
                }
                alternatives << ensemble.words[j];
            }

            response << trial << ',' << name.str() << ',' << ensemble.id << ','
                     << ensemble.focus << ',' << word << ",,,"
                     << options.voice.name << ',' << options.voice.rate << ','
                     << options.voice.pitch << '\n';
            key << trial << ',' << name.str() << ',' << ensemble.id << ','
                << ensemble.focus << ',' << word << ',' << alternatives.str()
                << '\n';
            ++trial;
            ++result.filesWritten;
        }
    }

    writeText(root / "response_sheet.csv", response.str());
    writeText(root / "answer_key.csv", key.str());
    ++result.filesWritten;
    ++result.filesWritten;

    std::ostringstream readme;
    readme << "# Modified Rhyme Test battery\n\n"
           << "Generated for Formant voice '" << options.voice.name
           << "' (rate=" << options.voice.rate
           << ", pitch=" << options.voice.pitch << ").\n\n"
           << "Files:\n"
           << "- `mrt_###_eNN_<focus>_<word>.wav`: one stimulus per trial\n"
           << "- `response_sheet.csv`: listener response template\n"
           << "- `answer_key.csv`: scoring key with target and alternatives\n\n"
           << "Administration: play each WAV once in random or listed order, "
              "ask the\n"
           << "listener to choose from the six alternatives for that ensemble, "
              "and\n"
           << "record the response in `response_sheet.csv`. Score percent "
              "correct\n"
           << "overall and by initial/final focus. Correct for guessing as\n"
           << "`(correct - (total-correct)/5) / total * 100` if desired.\n\n"
           << "Source: House, Williams, Hecker & Kryter (1965), Modified Rhyme "
              "Test.\n"
           << "Word list reproduced from the public-domain NIST MRT software\n"
           << "(usnistgov/MRT).\n";
    writeText(root / "README.txt", readme.str());
    ++result.filesWritten;

    return result;
}

} // namespace formant
