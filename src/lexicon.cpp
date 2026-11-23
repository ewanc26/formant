#include "formant/lexicon.h"

#include <cctype>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "formant/ipa.h"

namespace formant {
namespace {

std::string asciiLower(std::string_view text) {
    std::string result;
    result.reserve(text.size());
    for (const unsigned char c : text) {
        result.push_back(
            static_cast<char>(c >= 'A' && c <= 'Z' ? c + ('a' - 'A') : c));
    }
    return result;
}

std::string_view trimView(std::string_view text) {
    size_t begin = 0;
    while (begin < text.size() &&
           std::isspace(static_cast<unsigned char>(text[begin]))) {
        ++begin;
    }
    size_t end = text.size();
    while (end > begin &&
           std::isspace(static_cast<unsigned char>(text[end - 1]))) {
        --end;
    }
    return text.substr(begin, end - begin);
}

std::vector<std::string> splitFields(std::string_view line) {
    std::vector<std::string> fields;
    size_t pos = 0;
    while (pos < line.size()) {
        while (pos < line.size() &&
               std::isspace(static_cast<unsigned char>(line[pos]))) {
            ++pos;
        }
        if (pos == line.size()) {
            break;
        }
        size_t stop = pos;
        while (stop < line.size() &&
               !std::isspace(static_cast<unsigned char>(line[stop]))) {
            ++stop;
        }
        fields.emplace_back(line.substr(pos, stop - pos));
        pos = stop;
    }
    return fields;
}

std::string joinFields(const std::vector<std::string> &fields, size_t begin) {
    std::string result;
    for (size_t i = begin; i < fields.size(); ++i) {
        if (!result.empty()) {
            result.push_back(' ');
        }
        result += fields[i];
    }
    return result;
}

[[noreturn]] void lexiconError(size_t line, const std::string &message) {
    throw std::invalid_argument("lexicon line " + std::to_string(line) + ": " +
                                message);
}

void validateIpa(std::string_view ipa, size_t line) {
    try {
        (void)parseIpa(ipa);
    } catch (const std::invalid_argument &error) {
        lexiconError(line, "invalid IPA: " + std::string(error.what()));
    }
}

} // namespace

PronunciationLexicon parsePronunciationLexicon(std::string_view text) {
    PronunciationLexicon lexicon;
    size_t lineNumber = 0;
    size_t pos = 0;

    while (pos <= text.size()) {
        size_t end = text.find('\n', pos);
        if (end == std::string_view::npos) {
            end = text.size();
        }
        std::string_view line = text.substr(pos, end - pos);
        ++lineNumber;
        if (const size_t comment = line.find('#');
            comment != std::string_view::npos) {
            line = line.substr(0, comment);
        }
        line = trimView(line);

        if (!line.empty()) {
            const std::vector<std::string> fields = splitFields(line);
            const std::string &directive = fields.front();
            if (directive == "word") {
                if (fields.size() < 3) {
                    lexiconError(lineNumber, "word needs a spelling and IPA");
                }
                PronunciationEntry entry;
                entry.spelling = asciiLower(fields[1]);
                entry.ipa = joinFields(fields, 2);
                if (entry.spelling.empty() || entry.ipa.empty()) {
                    lexiconError(lineNumber, "word needs a spelling and IPA");
                }
                validateIpa(entry.ipa, lineNumber);
                lexicon.words.push_back(std::move(entry));
            } else if (directive == "homograph") {
                if (fields.size() < 3) {
                    lexiconError(lineNumber,
                                 "homograph needs a spelling and IPA");
                }
                HomographRule rule;
                rule.spelling = asciiLower(fields[1]);
                bool sawOption = false;
                for (size_t i = 2; i < fields.size(); ++i) {
                    const std::string &field = fields[i];
                    if (field.rfind("before=", 0) == 0 ||
                        field.rfind("after=", 0) == 0) {
                        const bool isBefore = field.rfind("before=", 0) == 0;
                        const std::string value =
                            field.substr(isBefore ? 7 : 6);
                        if (value.empty()) {
                            lexiconError(lineNumber,
                                         "empty " +
                                             std::string(isBefore ? "before"
                                                                  : "after") +
                                             " context");
                        }
                        std::string &target = isBefore ? rule.before
                                                       : rule.after;
                        if (!target.empty()) {
                            lexiconError(lineNumber,
                                         "duplicate " +
                                             std::string(isBefore ? "before"
                                                                  : "after") +
                                             " context");
                        }
                        target = asciiLower(value);
                        sawOption = true;
                    } else if (field.find('=') != std::string::npos) {
                        lexiconError(lineNumber,
                                     "unknown homograph option: " + field);
                    } else {
                        if (sawOption) {
                            lexiconError(
                                lineNumber,
                                "homograph IPA must come before options");
                        }
                        if (!rule.ipa.empty()) {
                            rule.ipa.push_back(' ');
                        }
                        rule.ipa += field;
                    }
                }
                if (rule.ipa.empty()) {
                    lexiconError(lineNumber,
                                 "homograph needs a spelling and IPA");
                }
                validateIpa(rule.ipa, lineNumber);
                lexicon.homographs.push_back(std::move(rule));
            } else {
                lexiconError(lineNumber,
                             "unknown directive: " + directive);
            }
        }

        if (end == text.size()) {
            break;
        }
        pos = end + 1;
    }
    return lexicon;
}

} // namespace formant
