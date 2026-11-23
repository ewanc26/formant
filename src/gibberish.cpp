#include "formant/gibberish.h"

#include <algorithm>
#include <cctype>
#include <iterator>
#include <numeric>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "formant/intonation.h"
#include "formant/ipa.h"

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

GibberishResult generateGibberish(const PatternGrammar &grammar,
                                  const PatternInventory &inventory,
                                  const GibberishSettings &settings,
                                  Language language) {
    if (settings.count < 1) {
        throw std::invalid_argument("gibberish count must be positive");
    }

    // Parse the raw pattern bodies afresh: parsePatternGrammar() validated the
    // source, and generateGibberish() is also a public entry point.
    Definitions definitions;
    for (const auto &definition : grammar.definitions) {
        definitions.emplace_back(definition.first,
                                 PatternParser(definition.second).parse());
    }
    const Node root = PatternParser(grammar.main).parse();

    {
        std::vector<char> stack;
        validateCategories(root, inventory, definitions, stack);
        for (const auto &definition : definitions) {
            stack.clear();
            stack.push_back(definition.first);
            validateCategories(definition.second, inventory, definitions,
                               stack);
        }
    }

    // Word generation and prosody use independent streams derived from the
    // seed. Enabling or disabling contours must never change the transcript,
    // so a word must not consume a draw that a later word would have used.
    SeededRng rng(settings.seed);
    SeededRng prosodyRng(settings.seed ^ 0x85ebca6bu);
    GibberishResult result;
    result.words.reserve(static_cast<size_t>(settings.count));

    for (int index = 0; index < settings.count; ++index) {
        GeneratedWord word;
        bool generated = false;
        for (int attempt = 0; attempt < kMaxFilterRetries && !generated;
             ++attempt) {
            std::vector<FilterCheck> checks;
            std::vector<char> stack;
            const Expansion expansion =
                expand(root, rng, inventory, definitions, stack, checks);
            if (expansion.spaced.empty() || !passesFilters(checks)) {
                continue;
            }
            std::vector<Phoneme> phonemes;
            try {
                phonemes = parseIpa(expansion.spaced);
            } catch (const std::invalid_argument &error) {
                throw std::invalid_argument(
                    std::string("generated transcript is not valid IPA: ") +
                    error.what());
            }
            if (phonemes.empty()) {
                continue;
            }
            word.ipa = expansion.spaced;
            word.phonemes = phonemes;
            generated = true;
        }
        if (!generated) {
            throw std::runtime_error(
                "gibberish pattern could not satisfy its exclusions");
        }

        std::vector<Phoneme> utterance = word.phonemes;
        if (settings.prosody) {
            applySeededProsody(utterance, language, prosodyRng, index,
                               settings.count);
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
    result.transcript = joinWords(result.words);
    return result;
}

} // namespace formant
