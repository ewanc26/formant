#include "formant/gibberish.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <iterator>
#include <numeric>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

#include "formant/intonation.h"
#include "formant/ipa.h"
#include "gibberish_ngram.h"

namespace formant {
namespace {

// Well beyond anything a word pattern needs; guards against runaway nesting
// and against stack exhaustion from a hostile pattern string.
constexpr int kMaxDepth = 32;
// A filter that can never be satisfied would otherwise spin forever.
constexpr int kMaxFilterRetries = 1000;
// Phrase-final nuclei get a gentle, fixed lengthening.
constexpr double kFinalLengthening = 1.20;
constexpr double kInterWordPause = 0.10;

bool isUpperLetter(char c) { return c >= 'A' && c <= 'Z'; }
bool isDigit(char c) { return c >= '0' && c <= '9'; }

bool isSpace(char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; }

std::string trim(std::string_view text) {
    size_t begin = 0;
    size_t end = text.size();
    while (begin < end && isSpace(text[begin])) {
        ++begin;
    }
    while (end > begin && isSpace(text[end - 1])) {
        --end;
    }
    return std::string(text.substr(begin, end - begin));
}

struct Node {
    enum class Kind { Literal, Category, Sequence, Choice, Optional };

    Kind kind = Kind::Literal;
    // Literal text or the single category letter.
    std::string text;
    // Weight of this node when it is an alternative in a Choice (1..128).
    uint32_t weight = 1;
    std::vector<Node> children;     // Sequence, or the body of an Optional.
    std::vector<Node> alternatives; // Choice.
    std::vector<std::string> filters;
};

// Recursive-descent parser for the Awkwords grammar. Whitespace outside quotes
// is ignored. `""` escapes any special characters, `[]` groups, `()` marks an
// optional group, `/` separates weighted alternatives and `^` attaches fixed
// exclusion strings to the fragment immediately before it.
class PatternParser {
  public:
    explicit PatternParser(std::string_view text) : text_(text) {}

    Node parse() {
        Node result = parseExpression('\0', 0);
        if (pos_ != text_.size()) {
            fail("unexpected character in pattern");
        }
        return result;
    }

  private:
    [[noreturn]] void fail(const std::string &message) const {
        throw std::invalid_argument("pattern at offset " +
                                    std::to_string(pos_) + ": " + message);
    }

    char peek() const { return pos_ < text_.size() ? text_[pos_] : '\0'; }

    void skipSpace() {
        while (pos_ < text_.size() && isSpace(text_[pos_])) {
            ++pos_;
        }
    }

    // Reads a *n weight. Returns 1 when there is no weight at this position.
    uint32_t parseWeight() {
        if (peek() != '*') {
            return 1;
        }
        ++pos_;
        const size_t begin = pos_;
        while (pos_ < text_.size() && isDigit(text_[pos_])) {
            ++pos_;
        }
        if (begin == pos_) {
            fail("expected an integer after '*'");
        }
        long long value = 0;
        for (size_t i = begin; i < pos_; ++i) {
            value = value * 10 + (text_[i] - '0');
            if (value > 128) {
                fail("weight must be between 1 and 128");
            }
        }
        if (value < 1) {
            fail("weight must be between 1 and 128");
        }
        return static_cast<uint32_t>(value);
    }

    // Exclusion filters are fixed strings, not patterns. Syntax characters end
    // a filter unless they are inside quotes, exactly as in Awkwords.
    std::vector<std::string> parseFilters() {
        std::vector<std::string> filters;
        while (peek() == '^') {
            ++pos_;
            std::string filter;
            while (pos_ < text_.size()) {
                const char c = peek();
                if (c == '"') {
                    ++pos_;
                    while (pos_ < text_.size() && peek() != '"') {
                        filter.push_back(text_[pos_++]);
                    }
                    if (pos_ == text_.size()) {
                        fail("unterminated quote in filter");
                    }
                    ++pos_;
                    continue;
                }
                if (c == '^' || c == '/' || c == '[' || c == ']' || c == '(' ||
                    c == ')' || c == '*' || isSpace(c) || isUpperLetter(c)) {
                    break;
                }
                filter.push_back(c);
                ++pos_;
            }
            if (filter.empty()) {
                fail("empty exclusion filter");
            }
            filters.push_back(std::move(filter));
        }
        return filters;
    }

    Node parseElement(int depth) {
        const char c = peek();
        if (c == '"') {
            ++pos_;
            Node literal;
            literal.kind = Node::Kind::Literal;
            while (pos_ < text_.size() && peek() != '"') {
                literal.text.push_back(text_[pos_++]);
            }
            if (pos_ == text_.size()) {
                fail("unterminated quote");
            }
            ++pos_;
            literal.filters = parseFilters();
            return literal;
        }
        if (c == '[' || c == '(') {
            const char close = c == '[' ? ']' : ')';
            ++pos_;
            Node body = parseExpression(close, depth + 1);
            if (pos_ >= text_.size() || peek() != close) {
                fail(std::string("expected '") + close + "'");
            }
            ++pos_;
            std::vector<std::string> filters = parseFilters();
            if (c == '(') {
                Node optional;
                optional.kind = Node::Kind::Optional;
                optional.children.push_back(std::move(body));
                optional.filters = std::move(filters);
                return optional;
            }
            body.filters.insert(body.filters.end(),
                                std::make_move_iterator(filters.begin()),
                                std::make_move_iterator(filters.end()));
            return body;
        }
        if (isUpperLetter(c)) {
            Node category;
            category.kind = Node::Kind::Category;
            category.text.assign(1, c);
            ++pos_;
            category.filters = parseFilters();
            return category;
        }

        Node literal;
        literal.kind = Node::Kind::Literal;
        while (pos_ < text_.size()) {
            const char current = peek();
            if (current == '/' || current == '[' || current == ']' ||
                current == '(' || current == ')' || current == '*' ||
                current == '^' || current == '"' || isSpace(current) ||
                isUpperLetter(current)) {
                break;
            }
            literal.text.push_back(current);
            ++pos_;
        }
        if (literal.text.empty()) {
            fail("expected a literal, category or group");
        }
        literal.filters = parseFilters();
        return literal;
    }

    Node parseSequence(char terminator, int depth) {
        Node sequence;
        sequence.kind = Node::Kind::Sequence;
        skipSpace();
        while (pos_ < text_.size()) {
            const char c = peek();
            if (c == terminator) {
                break;
            }
            if (c == '/' || c == '*') {
                break;
            }
            if (c == ']' || c == ')') {
                fail(std::string("unexpected '") + c + "'");
            }
            sequence.children.push_back(parseElement(depth));
            skipSpace();
        }
        return sequence;
    }

    Node parseExpression(char terminator, int depth) {
        if (depth > kMaxDepth) {
            fail("pattern nesting is too deep");
        }
        Node first = parseSequence(terminator, depth);
        uint32_t firstWeight = parseWeight();
        if (peek() != '/') {
            if (firstWeight != 1) {
                fail("'*' needs a '/'-separated alternative");
            }
            return first;
        }

        Node choice;
        choice.kind = Node::Kind::Choice;
        first.weight = firstWeight;
        choice.alternatives.push_back(std::move(first));
        while (peek() == '/') {
            ++pos_;
            skipSpace();
            Node alternative = parseSequence(terminator, depth);
            alternative.weight = parseWeight();
            choice.alternatives.push_back(std::move(alternative));
        }
        return choice;
    }

    std::string_view text_;
    size_t pos_ = 0;
};

struct Expansion {
    std::string raw;    // concatenated atoms, used for exclusion matching
    std::string spaced; // IPA transcript, one atom per token
};

void appendExpansion(Expansion &target, const Expansion &part) {
    if (part.spaced.empty()) {
        return;
    }
    if (!target.spaced.empty()) {
        target.spaced.push_back(' ');
    }
    target.spaced += part.spaced;
    target.raw += part.raw;
}

struct FilterCheck {
    std::string text;
    std::vector<std::string> filters;
};

using Definitions = std::vector<std::pair<char, Node>>;

const Node *findDefinition(const Definitions &definitions, char letter) {
    for (const auto &entry : definitions) {
        if (entry.first == letter) {
            return &entry.second;
        }
    }
    return nullptr;
}

// Expand one tree. A category letter may be backed by an inline subpattern
// definition or by the phoneme inventory. The active stack makes recursive
// definitions an error instead of an infinite loop.
Expansion expand(const Node &node, SeededRng &rng,
                 const PatternInventory &inventory,
                 const Definitions &definitions, std::vector<char> &stack,
                 std::vector<FilterCheck> &checks) {
    Expansion result;
    switch (node.kind) {
    case Node::Kind::Literal:
        result.raw = node.text;
        result.spaced = node.text;
        break;
    case Node::Kind::Category: {
        if (const Node *definition =
                findDefinition(definitions, node.text[0])) {
            if (std::find(stack.begin(), stack.end(), node.text[0]) !=
                stack.end()) {
                throw std::invalid_argument(
                    "recursive subpattern definition for " + node.text);
            }
            if (stack.size() >= static_cast<size_t>(kMaxDepth)) {
                throw std::invalid_argument("subpattern nesting is too deep");
            }
            stack.push_back(node.text[0]);
            result =
                expand(*definition, rng, inventory, definitions, stack, checks);
            stack.pop_back();
            break;
        }
        const std::vector<std::string> &atoms = inventory.get(node.text[0]);
        if (atoms.empty()) {
            throw std::invalid_argument("no inventory for category " +
                                        node.text);
        }
        const std::string &atom =
            atoms[rng.below(static_cast<uint32_t>(atoms.size()))];
        result.raw = atom;
        result.spaced = atom;
        break;
    }
    case Node::Kind::Sequence:
        for (const Node &child : node.children) {
            appendExpansion(result, expand(child, rng, inventory, definitions,
                                           stack, checks));
        }
        break;
    case Node::Kind::Choice: {
        const uint32_t total =
            std::accumulate(node.alternatives.begin(), node.alternatives.end(),
                            uint32_t{0}, [](uint32_t sum, const Node &option) {
                                return sum + option.weight;
                            });
        uint32_t draw = rng.below(total);
        size_t selected = 0;
        for (; selected + 1 < node.alternatives.size(); ++selected) {
            if (draw < node.alternatives[selected].weight) {
                break;
            }
            draw -= node.alternatives[selected].weight;
        }
        result = expand(node.alternatives[selected], rng, inventory,
                        definitions, stack, checks);
        break;
    }
    case Node::Kind::Optional:
        if (rng.chance(0.5) && !node.children.empty()) {
            result = expand(node.children.front(), rng, inventory, definitions,
                            stack, checks);
        }
        break;
    }
    checks.push_back({result.raw, node.filters});
    return result;
}

// Validate every referenced category before generating, so an undefined or
// recursive subpattern fails immediately even when the main pattern never
// reaches it.
void validateCategories(const Node &node, const PatternInventory &inventory,
                        const Definitions &definitions,
                        std::vector<char> &stack) {
    for (const Node &child : node.children) {
        validateCategories(child, inventory, definitions, stack);
    }
    for (const Node &child : node.alternatives) {
        validateCategories(child, inventory, definitions, stack);
    }
    if (node.kind != Node::Kind::Category) {
        return;
    }
    if (const Node *definition = findDefinition(definitions, node.text[0])) {
        if (std::find(stack.begin(), stack.end(), node.text[0]) !=
            stack.end()) {
            throw std::invalid_argument("recursive subpattern definition for " +
                                        node.text);
        }
        stack.push_back(node.text[0]);
        validateCategories(*definition, inventory, definitions, stack);
        stack.pop_back();
        return;
    }
    if (inventory.empty(node.text[0])) {
        throw std::invalid_argument("no inventory for category " + node.text);
    }
}

bool passesFilters(const std::vector<FilterCheck> &checks) {
    for (const FilterCheck &check : checks) {
        for (const std::string &filter : check.filters) {
            if (check.text.find(filter) != std::string::npos) {
                return false;
            }
        }
    }
    return true;
}

void applySeededProsody(std::vector<Phoneme> &phonemes, Language language,
                        SeededRng &rng, int index, int count) {
    static const char *const kTunes[] = {"statement", "yesno", "list"};
    const char *tune = kTunes[rng.below(3)];
    IntonationFile file;
    file.tune = tune;
    applyIntonation(phonemes, language, file, languageToneInventory(language));

    for (auto it = phonemes.rbegin(); it != phonemes.rend(); ++it) {
        if (it->kind == PhonemeKind::Voiced) {
            it->duration *= kFinalLengthening;
            break;
        }
    }

    const char *boundary = ".";
    if (std::string(tune) == "yesno") {
        boundary = "?";
    } else if (std::string(tune) == "list" && index + 1 < count) {
        boundary = ",";
    }
    const Phoneme *pause = findPhoneme(boundary);
    if (pause != nullptr) {
        phonemes.push_back(*pause);
    }
}

std::string joinWords(const std::vector<GeneratedWord> &words) {
    std::string result;
    for (const GeneratedWord &word : words) {
        if (!result.empty()) {
            result.push_back('\n');
        }
        result += word.ipa;
    }
    return result;
}

} // namespace

SeededRng::SeededRng(uint32_t seed) : state_(seed ^ 0x6d2b79f5u) {
    // xorshift32 cannot leave an all-zero state; keep every seed distinct and
    // usable, including zero.
    if (state_ == 0u) {
        state_ = 0x9e3779b9u;
    }
}

uint32_t SeededRng::next() {
    state_ ^= state_ << 13;
    state_ ^= state_ >> 17;
    state_ ^= state_ << 5;
    return state_;
}

uint32_t SeededRng::below(uint32_t bound) {
    if (bound == 0u) {
        throw std::invalid_argument("random bound must be positive");
    }
    // 32-bit rejection sampling keeps the distribution uniform even when the
    // bound does not divide 2^32, which matters for the weight tests.
    const uint32_t limit = (UINT32_MAX - bound + 1u) % bound;
    for (;;) {
        const uint32_t draw = next();
        if (draw >= limit) {
            return draw % bound;
        }
    }
}

bool SeededRng::chance(double probability) {
    if (!(probability >= 0.0 && probability <= 1.0)) {
        throw std::invalid_argument("probability must be between 0 and 1");
    }
    if (probability <= 0.0) {
        return false;
    }
    if (probability >= 1.0) {
        return true;
    }
    const double scaled = static_cast<double>(next() >> 8) / 16777216.0;
    return scaled < probability;
}

namespace {

std::string lowerAscii(std::string text) {
    for (char &c : text) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return text;
}

std::vector<std::string> splitWhitespace(const std::string &line) {
    std::istringstream stream(line);
    std::vector<std::string> items;
    std::string item;
    while (stream >> item) {
        items.push_back(item);
    }
    return items;
}

uint32_t parseUint32(const std::string &text, const std::string &context) {
    if (text.empty()) {
        throw std::invalid_argument("empty integer in " + context);
    }
    uint64_t value = 0;
    for (char c : text) {
        if (c < '0' || c > '9') {
            throw std::invalid_argument("invalid integer in " + context + ": " +
                                        text);
        }
        value = value * 10 + static_cast<uint64_t>(c - '0');
        if (value > 0xFFFFFFFFu) {
            throw std::invalid_argument("integer out of range in " + context);
        }
    }
    return static_cast<uint32_t>(value);
}

std::pair<std::string, uint32_t> parseWeightedAtom(const std::string &text,
                                                   const std::string &context) {
    const size_t colon = text.rfind(':');
    if (colon == std::string::npos) {
        throw std::invalid_argument("expected name:count in " + context);
    }
    const std::string name = text.substr(0, colon);
    const uint32_t count = parseUint32(text.substr(colon + 1), context);
    if (name.empty() || count == 0) {
        throw std::invalid_argument("invalid weighted atom in " + context);
    }
    return {name, count};
}

bool isKnownPhonemeName(const std::string &name) {
    return findPhoneme(name) != nullptr;
}

void validateNgramTable(const PhonotacticNgramTable &table) {
    if (table.languageTag.empty()) {
        throw std::invalid_argument("ngram table is missing lang");
    }
    if (table.empty()) {
        throw std::invalid_argument("ngram table is missing required sections");
    }
    auto validateAtoms =
        [](const std::vector<std::pair<std::string, uint32_t>> &atoms,
           const std::string &context) {
            uint64_t total = 0;
            for (const auto &atom : atoms) {
                if (atom.second == 0 || !isKnownPhonemeName(atom.first)) {
                    throw std::invalid_argument("invalid phoneme in " +
                                                context + ": " + atom.first);
                }
                total += atom.second;
            }
            if (total == 0) {
                throw std::invalid_argument("empty section: " + context);
            }
        };
    validateAtoms(table.starts, "starts");
    validateAtoms(table.unigrams, "unigrams");
    for (const auto &entry : table.lengths) {
        const uint32_t length = parseUint32(entry.first, "lengths");
        if (length < 1 || length > 32 || entry.second == 0) {
            throw std::invalid_argument("invalid length entry");
        }
    }
    for (const auto &entry : table.bigrams) {
        if (!isKnownPhonemeName(entry.first)) {
            throw std::invalid_argument("invalid bigram key: " + entry.first);
        }
        validateAtoms(entry.second, "bigrams " + entry.first);
    }
    for (const auto &entry : table.trigrams) {
        const size_t space = entry.first.find(' ');
        if (space == std::string::npos) {
            throw std::invalid_argument("invalid trigram key: " + entry.first);
        }
        const std::string p1 = entry.first.substr(0, space);
        const std::string p2 = entry.first.substr(space + 1);
        if (!isKnownPhonemeName(p1) || !isKnownPhonemeName(p2)) {
            throw std::invalid_argument("invalid trigram key: " + entry.first);
        }
        validateAtoms(entry.second, "trigrams " + entry.first);
    }
    for (const auto &entry : table.ends) {
        if (entry.second == 0 || !isKnownPhonemeName(entry.first)) {
            throw std::invalid_argument("invalid end phoneme: " + entry.first);
        }
    }
}

} // namespace

PhonotacticNgramTable parsePhonotacticNgramTable(std::string_view source) {
    PhonotacticNgramTable table;
    std::string section;
    size_t begin = 0;
    while (begin <= source.size()) {
        size_t end = source.find('\n', begin);
        if (end == std::string_view::npos) {
            end = source.size();
        }
        std::string line = trim(source.substr(begin, end - begin));
        begin = end + 1;
        if (line.empty() || line[0] == '#') {
            continue;
        }
        if (line.rfind("begin ", 0) == 0) {
            section = trim(line.substr(6));
            continue;
        }
        if (line.rfind("end ", 0) == 0) {
            const std::string closed = trim(line.substr(4));
            if (closed != section) {
                throw std::invalid_argument("ngram section mismatch: " + line);
            }
            section.clear();
            continue;
        }
        if (section.empty()) {
            const auto parts = splitWhitespace(line);
            if (parts.size() >= 2 && parts[0] == "lang") {
                table.languageTag = lowerAscii(parts[1]);
            } else if (parts.size() >= 2 && parts[0] == "source") {
                table.source = trim(line.substr(line.find(' ') + 1));
            } else if (parts.size() >= 2 && parts[0] == "license") {
                table.license = trim(line.substr(line.find(' ') + 1));
            }
            continue;
        }
        if (section == "lengths" || section == "starts" || section == "ends" ||
            section == "unigrams") {
            const auto parts = splitWhitespace(line);
            if (parts.size() != 2) {
                throw std::invalid_argument("invalid " + section + " row");
            }
            const uint32_t count = parseUint32(parts[1], section);
            auto &target = section == "lengths"  ? table.lengths
                           : section == "starts" ? table.starts
                           : section == "ends"   ? table.ends
                                                 : table.unigrams;
            target.emplace_back(parts[0], count);
            continue;
        }
        if (section == "bigrams") {
            const size_t tab = line.find('\t');
            if (tab == std::string::npos) {
                throw std::invalid_argument("invalid bigram row");
            }
            const std::string prev = trim(line.substr(0, tab));
            const auto rhs = splitWhitespace(line.substr(tab + 1));
            auto &row = table.bigrams[prev];
            for (const std::string &item : rhs) {
                row.push_back(parseWeightedAtom(item, "bigrams"));
            }
            continue;
        }
        if (section == "trigrams") {
            const size_t tab = line.find('\t');
            if (tab == std::string::npos) {
                throw std::invalid_argument("invalid trigram row");
            }
            const auto key = splitWhitespace(line.substr(0, tab));
            if (key.size() != 2) {
                throw std::invalid_argument("invalid trigram key");
            }
            const auto rhs = splitWhitespace(line.substr(tab + 1));
            auto &row = table.trigrams[key[0] + " " + key[1]];
            for (const std::string &item : rhs) {
                row.push_back(parseWeightedAtom(item, "trigrams"));
            }
            continue;
        }
        throw std::invalid_argument("unknown ngram section: " + section);
    }
    if (!section.empty()) {
        throw std::invalid_argument("unterminated ngram section: " + section);
    }
    validateNgramTable(table);
    return table;
}

PhonotacticNgramTable loadPhonotacticNgramTable(const std::string &path) {
    std::ifstream file(path);
    if (!file) {
        throw std::runtime_error("cannot open ngram table: " + path);
    }
    std::ostringstream source;
    source << file.rdbuf();
    if (file.bad()) {
        throw std::runtime_error("cannot read ngram table: " + path);
    }
    try {
        return parsePhonotacticNgramTable(source.str());
    } catch (const std::invalid_argument &error) {
        throw std::invalid_argument(path + ": " + error.what());
    }
}

PhonotacticNgramTable builtinPhonotacticNgramTable(std::string_view tag) {
    const std::string normalized = lowerAscii(std::string(tag));
    if (normalized == "en-us" || normalized == "en" ||
        normalized == "english" || normalized == "statistical") {
        return parsePhonotacticNgramTable(detail::builtinEnUsNgramTable());
    }
    throw std::invalid_argument("unknown gibberish style: " + std::string(tag));
}

std::vector<std::string> availableGibberishStyles() { return {"en-us"}; }

PatternGrammar parsePatternGrammar(std::string_view source) {
    PatternGrammar grammar;
    size_t begin = 0;
    while (begin <= source.size()) {
        size_t end = source.find('\n', begin);
        if (end == std::string_view::npos) {
            end = source.size();
        }
        std::string line = trim(source.substr(begin, end - begin));
        begin = end + 1;

        if (line.empty() || line[0] == '#') {
            continue;
        }
        const size_t equals = line.find('=');
        if (equals == 1 && isUpperLetter(line[0])) {
            const std::string body =
                trim(std::string_view(line).substr(equals + 1));
            PatternParser(body).parse();
            grammar.definitions.emplace_back(line[0], body);
            continue;
        }
        if (!grammar.main.empty()) {
            grammar.main.push_back(' ');
        }
        grammar.main += line;
    }
    if (grammar.main.empty()) {
        throw std::invalid_argument("pattern grammar needs a main pattern");
    }
    PatternParser(grammar.main).parse();

    // Duplicate definitions would make inventory resolution ambiguous.
    for (size_t i = 0; i < grammar.definitions.size(); ++i) {
        for (size_t j = i + 1; j < grammar.definitions.size(); ++j) {
            if (grammar.definitions[i].first == grammar.definitions[j].first) {
                throw std::invalid_argument(
                    std::string("duplicate pattern definition for ") +
                    grammar.definitions[i].first);
            }
        }
    }
    return grammar;
}

void PatternInventory::set(char letter, std::vector<std::string> atoms) {
    if (!isUpperLetter(letter)) {
        throw std::invalid_argument("category must be an uppercase letter");
    }
    categories_[static_cast<size_t>(letter - 'A')] = std::move(atoms);
}

const std::vector<std::string> &PatternInventory::get(char letter) const {
    static const std::vector<std::string> kEmpty;
    if (!isUpperLetter(letter)) {
        return kEmpty;
    }
    return categories_[static_cast<size_t>(letter - 'A')];
}

PatternInventory defaultPatternInventory() {
    PatternInventory inventory;
    inventory.set('V', {"a", "e", "i", "o", "u", "\u0251", "\u025B", "\u026A",
                        "\u028A", "\u0259", "\u0254", "\u00E6", "\u028C"});
    inventory.set('C', {"p",      "t",       "k",      "b", "d",      "\u0261",
                        "f",      "v",       "s",      "z", "\u0283", "\u0292",
                        "\u03B8", "\u00F0",  "h",      "l", "\u0279", "w",
                        "j",      "t\u0283", "d\u0292"});
    inventory.set('N', {"m", "n", "\u014B"});
    return inventory;
}

PatternInventory patternInventory(const LanguageDefinition &language) {
    PatternInventory inventory = defaultPatternInventory();
    for (const auto &entry : language.classes) {
        if (entry.first.size() != 1 || !isUpperLetter(entry.first[0])) {
            continue;
        }
        inventory.set(entry.first[0], entry.second);
    }

    // A conlang that does not declare classes still has a phoneme inventory.
    // Classify its symbols by the PhonemeKind produced by the shared IPA
    // parser, which is the same information the renderer consumes.
    std::vector<std::string> vowels;
    std::vector<std::string> nasals;
    std::vector<std::string> consonants;
    for (const auto &entry : language.phonemes) {
        try {
            const std::vector<Phoneme> parsed = parseIpa(entry.first);
            if (parsed.size() != 1) {
                continue;
            }
            switch (parsed.front().kind) {
            case PhonemeKind::Voiced:
                vowels.push_back(entry.first);
                break;
            case PhonemeKind::Nasal:
                nasals.push_back(entry.first);
                break;
            case PhonemeKind::Fricative:
            case PhonemeKind::Plosive:
            case PhonemeKind::Approximant:
                consonants.push_back(entry.first);
                break;
            case PhonemeKind::Silence:
                break;
            }
        } catch (const std::invalid_argument &) {
            // An override key that is not a base IPA symbol cannot be spoken
            // by the shared renderer, so it is not a usable generation atom.
        }
    }
    auto sortUnique = [](std::vector<std::string> &items) {
        std::sort(items.begin(), items.end());
        items.erase(std::unique(items.begin(), items.end()), items.end());
    };
    sortUnique(vowels);
    sortUnique(nasals);
    sortUnique(consonants);
    if (inventory.empty('V')) {
        inventory.set('V', std::move(vowels));
    }
    if (inventory.empty('N')) {
        inventory.set('N', std::move(nasals));
    }
    if (inventory.empty('C')) {
        inventory.set('C', std::move(consonants));
    }
    return inventory;
}

std::string patternGrammarHelp() {
    return R"(Awkwords-compatible pattern grammar

The main pattern describes one word. Lower-case letters (and IPA symbols) are
literal text; capital letters A-Z refer to subpatterns. C (consonants), V
(vowels) and N (nasals) have built-in IPA inventories, or come from a conlang
file's `class` / `phoneme` directives. Other letters must be defined.

  a/b/c        choose one alternative at random
  *n           weight an alternative (integer 1..128, default 1)
  [a/b]        group, so the following text sees one unit
  (a/b)        optional group: appears or not, with equal probability
  "text"       literal text; quotes escape all pattern syntax
  X=a/b        define subpattern X (one definition per line)
  ]^abc        exclude the fixed string abc from the preceding fragment;
               several filters may be chained: [VV]^aa^ii^uu

Whitespace outside quotes is ignored. Brackets nest up to 32 levels. A word
that violates any exclusion is discarded and regenerated from the same seeded
stream, so a given seed always produces the same transcripts.

Styles:
  pattern      explicit grammar (default)
  statistical  weighted phoneme n-grams from a data table
  combined     grammar structure with weighted statistical categories

Statistical tables are plain data files with documented provenance. Use
--gibberish-style en-us, or --gibberish-style-file for a custom table.

Examples:
  CVC
  CV(V)C
  [[V][V]]^aa^ii^uu   (no doubled vowels)
  C="s"/"z"VC

Inline definitions can be placed before the main pattern:
  C=p/t/k
  V=a/i/u
  CVC
)";
}

namespace {

using Weighted = std::vector<std::pair<std::string, uint32_t>>;

std::unordered_set<std::string>
inventoryAtomSet(const PatternInventory &inventory) {
    std::unordered_set<std::string> atoms;
    for (char letter = 'A'; letter <= 'Z'; ++letter) {
        for (const std::string &atom : inventory.get(letter)) {
            if (isKnownPhonemeName(atom)) {
                atoms.insert(atom);
            }
        }
    }
    return atoms;
}

Weighted filterWeighted(const Weighted &items,
                        const std::unordered_set<std::string> *allowed) {
    Weighted filtered;
    filtered.reserve(items.size());
    for (const auto &item : items) {
        if (!isKnownPhonemeName(item.first) || item.second == 0) {
            continue;
        }
        if (allowed != nullptr && allowed->find(item.first) == allowed->end()) {
            continue;
        }
        filtered.push_back(item);
    }
    return filtered;
}

uint32_t totalWeight(const Weighted &items) {
    uint64_t total = 0;
    for (const auto &item : items) {
        total += item.second;
    }
    if (total > 0xFFFFFFFFu) {
        return 0xFFFFFFFFu;
    }
    return static_cast<uint32_t>(total);
}

const std::string &pickWeighted(const Weighted &items, SeededRng &rng) {
    const uint32_t total = totalWeight(items);
    if (total == 0) {
        throw std::runtime_error("no weighted candidates available");
    }
    uint32_t draw = rng.below(total);
    for (const auto &item : items) {
        if (draw < item.second) {
            return item.first;
        }
        draw -= item.second;
    }
    return items.back().first;
}

PhonotacticNgramTable constrainNgramTable(const PhonotacticNgramTable &table,
                                          const PatternInventory &inventory) {
    const std::unordered_set<std::string> allowed = inventoryAtomSet(inventory);
    if (allowed.empty()) {
        throw std::invalid_argument(
            "statistical gibberish needs a non-empty phoneme inventory");
    }
    PhonotacticNgramTable constrained = table;
    constrained.starts = filterWeighted(table.starts, &allowed);
    constrained.ends = filterWeighted(table.ends, &allowed);
    constrained.unigrams = filterWeighted(table.unigrams, &allowed);
    constrained.lengths.clear();
    for (const auto &entry : table.lengths) {
        const uint32_t length = parseUint32(entry.first, "lengths");
        if (length >= 1 && length <= 32 && entry.second > 0) {
            constrained.lengths.push_back(entry);
        }
    }
    constrained.bigrams.clear();
    for (const auto &entry : table.bigrams) {
        if (allowed.find(entry.first) == allowed.end()) {
            continue;
        }
        Weighted next = filterWeighted(entry.second, &allowed);
        if (!next.empty()) {
            constrained.bigrams[entry.first] = std::move(next);
        }
    }
    constrained.trigrams.clear();
    for (const auto &entry : table.trigrams) {
        const size_t space = entry.first.find(' ');
        if (space == std::string::npos) {
            continue;
        }
        const std::string p1 = entry.first.substr(0, space);
        const std::string p2 = entry.first.substr(space + 1);
        if (allowed.find(p1) == allowed.end() ||
            allowed.find(p2) == allowed.end()) {
            continue;
        }
        Weighted next = filterWeighted(entry.second, &allowed);
        if (!next.empty()) {
            constrained.trigrams[entry.first] = std::move(next);
        }
    }
    if (constrained.starts.empty() || constrained.unigrams.empty() ||
        constrained.lengths.empty()) {
        throw std::invalid_argument(
            "gibberish style table has no usable phonemes for this inventory");
    }
    return constrained;
}

bool hasLegalTransitions(const std::vector<std::string> &phones,
                         const PhonotacticNgramTable &table) {
    if (phones.empty()) {
        return false;
    }
    for (size_t i = 0; i + 1 < phones.size(); ++i) {
        auto bigram = table.bigrams.find(phones[i]);
        if (bigram == table.bigrams.end()) {
            return false;
        }
        bool allowed = false;
        for (const auto &next : bigram->second) {
            if (next.first == phones[i + 1]) {
                allowed = true;
                break;
            }
        }
        if (!allowed) {
            return false;
        }
        if (i + 2 < phones.size()) {
            auto trigram = table.trigrams.find(phones[i] + " " + phones[i + 1]);
            if (trigram != table.trigrams.end()) {
                bool trigramAllowed = false;
                for (const auto &next : trigram->second) {
                    if (next.first == phones[i + 2]) {
                        trigramAllowed = true;
                        break;
                    }
                }
                if (!trigramAllowed) {
                    return false;
                }
            }
        }
    }
    return true;
}

Weighted categoryWeights(char letter, const PatternInventory &inventory,
                         const PhonotacticNgramTable &table) {
    const std::vector<std::string> &atoms = inventory.get(letter);
    Weighted weighted;
    weighted.reserve(atoms.size());
    for (const std::string &atom : atoms) {
        uint32_t weight = 0;
        for (const auto &entry : table.unigrams) {
            if (entry.first == atom) {
                weight = entry.second;
                break;
            }
        }
        if (weight == 0) {
            weight = 1;
        }
        weighted.emplace_back(atom, weight);
    }
    return weighted;
}

GeneratedWord wordFromPhones(std::vector<std::string> phones) {
    GeneratedWord word;
    for (size_t i = 0; i < phones.size(); ++i) {
        if (i != 0) {
            word.ipa.push_back(' ');
        }
        word.ipa += phones[i];
    }
    word.phonemes = parseIpa(word.ipa);
    return word;
}

GeneratedWord generateStatisticalWord(const PhonotacticNgramTable &table,
                                      SeededRng &rng) {
    for (int attempt = 0; attempt < kMaxFilterRetries; ++attempt) {
        const uint32_t targetLength =
            parseUint32(pickWeighted(table.lengths, rng), "lengths");
        std::vector<std::string> phones;
        phones.reserve(targetLength);
        phones.push_back(pickWeighted(table.starts, rng));
        for (uint32_t i = 1; i < targetLength; ++i) {
            Weighted candidates;
            if (i >= 2) {
                auto trigram =
                    table.trigrams.find(phones[i - 2] + " " + phones[i - 1]);
                if (trigram != table.trigrams.end()) {
                    candidates = trigram->second;
                }
            }
            if (candidates.empty()) {
                auto bigram = table.bigrams.find(phones.back());
                if (bigram != table.bigrams.end()) {
                    candidates = bigram->second;
                }
            }
            if (candidates.empty()) {
                candidates = table.unigrams;
            }
            if (i + 1 == targetLength && !table.ends.empty()) {
                Weighted endCandidates;
                for (const auto &candidate : candidates) {
                    for (const auto &end : table.ends) {
                        if (candidate.first == end.first) {
                            endCandidates.emplace_back(
                                candidate.first,
                                std::max<uint32_t>(1, candidate.second / 8 +
                                                          end.second / 8));
                            break;
                        }
                    }
                }
                if (!endCandidates.empty()) {
                    candidates = std::move(endCandidates);
                }
            }
            phones.push_back(pickWeighted(candidates, rng));
        }
        if (hasLegalTransitions(phones, table)) {
            return wordFromPhones(std::move(phones));
        }
    }
    throw std::runtime_error(
        "gibberish style could not satisfy phonotactic constraints");
}

Expansion expandCombined(const Node &node, SeededRng &rng,
                         const PatternInventory &inventory,
                         const Definitions &definitions,
                         const PhonotacticNgramTable &table,
                         std::vector<char> &stack,
                         std::vector<FilterCheck> &checks) {
    Expansion result;
    switch (node.kind) {
    case Node::Kind::Literal:
        result.raw = node.text;
        result.spaced = node.text;
        break;
    case Node::Kind::Category: {
        if (const Node *definition =
                findDefinition(definitions, node.text[0])) {
            if (std::find(stack.begin(), stack.end(), node.text[0]) !=
                stack.end()) {
                throw std::invalid_argument(
                    "recursive subpattern definition for " + node.text);
            }
            stack.push_back(node.text[0]);
            result = expandCombined(*definition, rng, inventory, definitions,
                                    table, stack, checks);
            stack.pop_back();
            break;
        }
        const Weighted weighted =
            categoryWeights(node.text[0], inventory, table);
        if (weighted.empty()) {
            throw std::invalid_argument("no inventory for category " +
                                        node.text);
        }
        const std::string &atom = pickWeighted(weighted, rng);
        result.raw = atom;
        result.spaced = atom;
        break;
    }
    case Node::Kind::Sequence:
        for (const Node &child : node.children) {
            appendExpansion(result,
                            expandCombined(child, rng, inventory, definitions,
                                           table, stack, checks));
        }
        break;
    case Node::Kind::Choice: {
        const uint32_t total =
            std::accumulate(node.alternatives.begin(), node.alternatives.end(),
                            uint32_t{0}, [](uint32_t sum, const Node &option) {
                                return sum + option.weight;
                            });
        uint32_t draw = rng.below(total);
        size_t selected = 0;
        for (; selected + 1 < node.alternatives.size(); ++selected) {
            if (draw < node.alternatives[selected].weight) {
                break;
            }
            draw -= node.alternatives[selected].weight;
        }
        result = expandCombined(node.alternatives[selected], rng, inventory,
                                definitions, table, stack, checks);
        break;
    }
    case Node::Kind::Optional:
        if (rng.chance(0.5) && !node.children.empty()) {
            result = expandCombined(node.children.front(), rng, inventory,
                                    definitions, table, stack, checks);
        }
        break;
    }
    checks.push_back({result.raw, node.filters});
    return result;
}

void finalizeWord(GibberishResult &result, GeneratedWord word, int index,
                  int count, bool prosody, Language language,
                  SeededRng &prosodyRng) {
    std::vector<Phoneme> utterance = word.phonemes;
    if (prosody) {
        applySeededProsody(utterance, language, prosodyRng, index, count);
    }
    if (!result.phonemes.empty()) {
        Phoneme pause = *findPhoneme("_");
        pause.duration = kInterWordPause;
        result.phonemes.push_back(pause);
    }
    result.phonemes.insert(result.phonemes.end(), utterance.begin(),
                           utterance.end());
    result.words.push_back(std::move(word));
}

} // namespace

GibberishResult generateGibberish(const PatternGrammar &grammar,
                                  const PatternInventory &inventory,
                                  const GibberishSettings &settings,
                                  Language language) {
    if (settings.count < 1) {
        throw std::invalid_argument("gibberish count must be positive");
    }

    const bool isStatistical = settings.style == GibberishStyle::Statistical;
    const bool isCombined = settings.style == GibberishStyle::Combined;
    PhonotacticNgramTable styleTable;
    if (isStatistical || isCombined) {
        styleTable = settings.styleTablePath.empty()
                         ? builtinPhonotacticNgramTable(settings.styleTag)
                         : loadPhonotacticNgramTable(settings.styleTablePath);
        styleTable = constrainNgramTable(styleTable, inventory);
    }

    Definitions definitions;
    Node root;
    if (!isStatistical) {
        for (const auto &definition : grammar.definitions) {
            definitions.emplace_back(definition.first,
                                     PatternParser(definition.second).parse());
        }
        root = PatternParser(grammar.main).parse();
        std::vector<char> stack;
        validateCategories(root, inventory, definitions, stack);
        for (const auto &definition : definitions) {
            stack.clear();
            stack.push_back(definition.first);
            validateCategories(definition.second, inventory, definitions,
                               stack);
        }
    }

    SeededRng rng(settings.seed);
    SeededRng prosodyRng(settings.seed ^ 0x85ebca6bu);
    GibberishResult result;
    result.words.reserve(static_cast<size_t>(settings.count));

    for (int index = 0; index < settings.count; ++index) {
        GeneratedWord word;
        bool generated = false;
        for (int attempt = 0; attempt < kMaxFilterRetries && !generated;
             ++attempt) {
            if (isStatistical) {
                word = generateStatisticalWord(styleTable, rng);
                generated = true;
                break;
            }
            std::vector<FilterCheck> checks;
            std::vector<char> stack;
            const Expansion expansion =
                isCombined
                    ? expandCombined(root, rng, inventory, definitions,
                                     styleTable, stack, checks)
                    : expand(root, rng, inventory, definitions, stack, checks);
            if (expansion.spaced.empty() || !passesFilters(checks)) {
                continue;
            }
            std::vector<std::string> atoms;
            {
                std::istringstream spaced(expansion.spaced);
                std::string atom;
                while (spaced >> atom) {
                    atoms.push_back(atom);
                }
            }
            if (isCombined && !hasLegalTransitions(atoms, styleTable)) {
                continue;
            }
            try {
                word = wordFromPhones(std::move(atoms));
            } catch (const std::invalid_argument &error) {
                throw std::invalid_argument(
                    std::string("generated transcript is not valid IPA: ") +
                    error.what());
            }
            if (word.phonemes.empty()) {
                continue;
            }
            generated = true;
        }
        if (!generated) {
            throw std::runtime_error("gibberish pattern could not satisfy its "
                                     "exclusions/constraints");
        }
        finalizeWord(result, std::move(word), index, settings.count,
                     settings.prosody, language, prosodyRng);
    }
    result.transcript = joinWords(result.words);
    return result;
}

} // namespace formant
