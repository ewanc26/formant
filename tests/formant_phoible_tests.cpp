// PHOIBLE conformance test (issue #98).
//
// Reads the committed segment table and coverage summary, then checks every
// PHOIBLE segment against the real Formant IPA front end. A segment is covered
// when its transcription parses and renders at least one non-silent phoneme.
// Rows marked "direct" must work with the PHOIBLE symbol itself, so removing
// support for a previously covered symbol fails this test. Rows marked
// "approx" use the documented substitution policy (see data/phoible/
// PROVENANCE.md). PHOIBLE is never downloaded or read at run time.

#include "formant/formant.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#ifndef FORMANT_PHOIBLE_TABLE
#define FORMANT_PHOIBLE_TABLE "data/phoible/segments.tsv"
#endif
#ifndef FORMANT_PHOIBLE_COVERAGE
#define FORMANT_PHOIBLE_COVERAGE "data/phoible/coverage.txt"
#endif

namespace {

int failures = 0;

#define CHECK(condition)                                                       \
    do {                                                                       \
        if (!(condition)) {                                                    \
            std::cerr << "FAIL " << __FILE__ << ":" << __LINE__ << ": "        \
                      << #condition << "\n";                                   \
            ++failures;                                                        \
        }                                                                      \
    } while (false)

struct Segment {
    std::string glyph;
    std::string category;
    std::string ipa;
    std::string source;
};

struct Coverage {
    std::string category;
    int total = 0;
    int direct = 0;
    int approx = 0;
    double directPercent = 0.0;
};

std::vector<std::string> splitTabs(const std::string &line) {
    std::vector<std::string> fields;
    std::stringstream stream(line);
    std::string field;
    while (std::getline(stream, field, '\t')) {
        fields.push_back(field);
    }
    return fields;
}

std::string readWholeFile(const std::string &path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        std::cerr << "cannot open " << path << "\n";
        ++failures;
        return {};
    }
    std::ostringstream contents;
    contents << file.rdbuf();
    return contents.str();
}

std::vector<Segment> loadSegments(const std::string &path) {
    std::vector<Segment> segments;
    const std::string text = readWholeFile(path);
    std::istringstream stream(text);
    std::string line;
    if (!std::getline(stream, line) || line != "glyph\tcategory\tipa\tsource") {
        std::cerr << "bad PHOIBLE table header in " << path << "\n";
        ++failures;
        return segments;
    }
    while (std::getline(stream, line)) {
        if (line.empty()) {
            continue;
        }
        const std::vector<std::string> fields = splitTabs(line);
        if (fields.size() != 4) {
            std::cerr << "bad PHOIBLE table row: " << line << "\n";
            ++failures;
            continue;
        }
        segments.push_back({fields[0], fields[1], fields[2], fields[3]});
    }
    return segments;
}

std::vector<Coverage> loadCoverage(const std::string &path) {
    std::vector<Coverage> rows;
    const std::string text = readWholeFile(path);
    std::istringstream stream(text);
    std::string line;
    while (std::getline(stream, line)) {
        if (line.empty() || line[0] == '#') {
            continue;
        }
        const std::vector<std::string> fields = splitTabs(line);
        if (fields.size() != 5) {
            std::cerr << "bad coverage row: " << line << "\n";
            ++failures;
            continue;
        }
        rows.push_back({fields[0], std::stoi(fields[1]), std::stoi(fields[2]),
                        std::stoi(fields[3]), std::stod(fields[4])});
    }
    return rows;
}

bool rendersNonSilence(const std::string &ipa, const formant::Voice &voice) {
    const std::vector<formant::Phoneme> phonemes = formant::parseIpa(ipa);
    if (phonemes.empty()) {
        return false;
    }
    const bool hasSegment = std::any_of(
        phonemes.begin(), phonemes.end(), [](const formant::Phoneme &phoneme) {
            return phoneme.kind != formant::PhonemeKind::Silence;
        });
    if (!hasSegment) {
        return false;
    }
    // Render at the lowest supported rate to keep the full 2,162-segment
    // inventory fast while still exercising the synthesis path.
    const std::vector<float> samples = formant::synthesizeIpa(ipa, voice, 8000);
    return std::any_of(samples.begin(), samples.end(),
                       [](float sample) { return sample != 0.0F; });
}

} // namespace

int main() {
    const std::vector<Segment> segments = loadSegments(FORMANT_PHOIBLE_TABLE);
    const std::vector<Coverage> coverage =
        loadCoverage(FORMANT_PHOIBLE_COVERAGE);

    CHECK(!segments.empty());
    CHECK(coverage.size() == 5);

    // Full inventory: every listed segment must parse and render non-silence.
    // Direct rows additionally prove the PHOIBLE symbol is supported natively.
    std::map<std::string, std::pair<int, int>> counts;
    const formant::Voice voice = formant::voicePreset("neutral");
    for (const Segment &segment : segments) {
        bool ok = false;
        try {
            ok = rendersNonSilence(segment.ipa, voice);
        } catch (const std::exception &error) {
            std::cerr << "FAIL parse " << segment.glyph << " -> " << segment.ipa
                      << ": " << error.what() << "\n";
            ++failures;
        }
        if (!ok) {
            std::cerr << "FAIL silent " << segment.glyph << " -> "
                      << segment.ipa << "\n";
            ++failures;
        }
        if (segment.source == "direct") {
            bool native = false;
            try {
                native = rendersNonSilence(segment.glyph, voice);
            } catch (const std::exception &error) {
                std::cerr << "FAIL direct " << segment.glyph << ": "
                          << error.what() << "\n";
                ++failures;
            }
            if (!native) {
                std::cerr << "FAIL direct no longer supported: "
                          << segment.glyph << "\n";
                ++failures;
            }
        } else if (segment.source != "approx") {
            std::cerr << "FAIL unknown source: " << segment.source << "\n";
            ++failures;
        }
        if (segment.category != "vowel" && segment.category != "pulmonic" &&
            segment.category != "non-pulmonic" &&
            segment.category != "diacritic") {
            std::cerr << "FAIL unknown category: " << segment.category << "\n";
            ++failures;
        }
        std::pair<int, int> &count = counts[segment.category];
        ++count.first;
        if (segment.source == "direct") {
            ++count.second;
        }
    }

    // Compare the measured table with the committed coverage summary. This is
    // the no-regression number tracked in CI: a smaller table or fewer native
    // symbols than recorded fails the test.
    for (const Coverage &row : coverage) {
        int total = 0;
        int direct = 0;
        if (row.category == "ALL") {
            total = static_cast<int>(segments.size());
            for (const Segment &segment : segments) {
                if (segment.source == "direct") {
                    ++direct;
                }
            }
        } else {
            const auto found = counts.find(row.category);
            if (found == counts.end()) {
                std::cerr << "FAIL missing category: " << row.category << "\n";
                ++failures;
                continue;
            }
            total = found->second.first;
            direct = found->second.second;
        }
        CHECK(total == row.total);
        CHECK(direct == row.direct);
        CHECK(total - direct == row.approx);
        const double percent =
            total > 0 ? 100.0 * static_cast<double>(direct) / total : 0.0;
        CHECK(std::fabs(percent - row.directPercent) < 0.05);
        std::cout << row.category << "\t" << total << "\t" << direct << "\t"
                  << (total - direct) << "\t" << std::fixed
                  << std::setprecision(1) << percent << "%\n";
    }

    if (failures != 0) {
        std::cerr << failures << " PHOIBLE conformance failure(s)\n";
        return 1;
    }
    std::cout << "PHOIBLE conformance: all " << segments.size()
              << " segments parse and render non-silence\n";
    return 0;
}
