// Reading and checking messages of SPEC-0033's MessageFormat 2.0 subset.

#include "message_parts.h"
#include "rawframe/localization/errors.h"
#include "rawframe/localization/message.h"

#include <algorithm>
#include <map>
#include <set>

namespace rawframe::localization {

namespace {

std::unexpected<result::Error> invalid(std::string_view why) {
    return result::fail(
        result::ErrorClass::InvalidArgument, kLocalizationDomain, code(LocalizationError::MessageInvalid), why);
}

std::unexpected<result::Error> overLimit(std::string_view why) {
    return result::fail(
        result::ErrorClass::InvalidArgument, kLocalizationDomain, code(LocalizationError::OverLimit), why);
}

bool whitespace(char each) {
    return each == ' ' || each == '\t' || each == '\n' || each == '\r';
}

bool nameCharacter(char each) {
    return (each >= 'a' && each <= 'z') || (each >= 'A' && each <= 'Z') || (each >= '0' && each <= '9') ||
           each == '_' || each == '-' || each == '.' || each == '+';
}

/// A cursor over the message's text.
struct Reader {
    std::string_view text;
    std::size_t at = 0;

    [[nodiscard]] bool done() const {
        return at >= text.size();
    }
    [[nodiscard]] char peek() const {
        return done() ? '\0' : text[at];
    }
    [[nodiscard]] bool ahead(std::string_view word) const {
        return text.substr(at).starts_with(word);
    }
    /// Skips whitespace, saying whether there was any.
    bool skip() {
        const std::size_t kFrom = at;
        while (!done() && whitespace(text[at])) {
            ++at;
        }
        return at != kFrom;
    }
    /// A run of name characters.
    std::string_view token() {
        const std::size_t kFrom = at;
        while (!done() && nameCharacter(text[at])) {
            ++at;
        }
        return text.substr(kFrom, at - kFrom);
    }
};

result::Result<std::string> quoted(Reader& reader) {
    // `|...|`, with `\|` and `\\` its only escapes.
    ++reader.at;
    std::string made;
    while (!reader.done() && reader.peek() != '|') {
        if (reader.peek() == '\\') {
            ++reader.at;
            if (reader.peek() != '|' && reader.peek() != '\\') {
                return invalid("a quoted literal escapes only | and \\");
            }
        }
        made += reader.peek();
        ++reader.at;
    }
    if (reader.done()) {
        return invalid("a quoted literal ends with |");
    }
    ++reader.at;
    return made;
}

result::Result<std::string> variable(Reader& reader) {
    if (reader.peek() != '$') {
        return invalid("a variable begins with $");
    }
    ++reader.at;
    const std::string_view kName = reader.token();
    if (!variableName(kName)) {
        return invalid("a variable is named [a-z][a-zA-Z0-9_]*");
    }
    return std::string{kName};
}

result::Status option(Reader& reader, Annotation& annotation) {
    const std::string_view kName = reader.token();
    reader.skip();
    if (reader.peek() != '=') {
        return invalid("an option is name=value");
    }
    ++reader.at;
    reader.skip();
    std::string value;
    if (reader.peek() == '|') {
        RAWFRAME_TRY_ASSIGN(value, quoted(reader));
    } else if (reader.peek() == '$') {
        return invalid("an option's value is a literal");
    } else {
        value = reader.token();
    }
    const bool kNumeric = annotation.function != MessageFunction::String;
    const auto kDigits = [&value]() -> std::optional<std::uint32_t> {
        if (value.empty() || value.size() > 2 || !std::ranges::all_of(value, [](char each) {
                return each >= '0' && each <= '9';
            })) {
            return std::nullopt;
        }
        std::uint32_t made = 0;
        for (const char kEach : value) {
            made = made * 10 + static_cast<std::uint32_t>(kEach - '0');
        }
        return made <= kMostFractionDigitsOption ? std::optional{made} : std::nullopt;
    };
    if (kNumeric && kName == "select" && !annotation.select.has_value()) {
        if (value == "cardinal" || value == "ordinal" || value == "exact") {
            annotation.select = value == "cardinal"  ? Selection::Cardinal
                                : value == "ordinal" ? Selection::Ordinal
                                                     : Selection::Exact;
            return {};
        }
    } else if (kNumeric && kName == "useGrouping" && !annotation.grouping.has_value()) {
        if (value == "auto" || value == "never") {
            annotation.grouping = value == "auto";
            return {};
        }
    } else if (annotation.function == MessageFunction::Number && kName == "minimumFractionDigits" &&
               !annotation.minimumFraction.has_value()) {
        annotation.minimumFraction = kDigits();
        if (annotation.minimumFraction.has_value()) {
            return {};
        }
    } else if (annotation.function == MessageFunction::Number && kName == "maximumFractionDigits" &&
               !annotation.maximumFraction.has_value()) {
        annotation.maximumFraction = kDigits();
        if (annotation.maximumFraction.has_value()) {
            return {};
        }
    }
    return invalid("an option is one its function takes, once, of a value it takes");
}

result::Result<Expression> expression(Reader& reader) {
    if (reader.peek() != '{') {
        return invalid("an expression is in braces");
    }
    ++reader.at;
    reader.skip();
    Expression made;
    if (reader.peek() == '$') {
        RAWFRAME_TRY_ASSIGN(made.operand.text, variable(reader));
    } else if (reader.peek() == '|') {
        made.operand.literal = true;
        RAWFRAME_TRY_ASSIGN(made.operand.text, quoted(reader));
    } else {
        return invalid("an expression reads a variable or a quoted literal; markup and bare literals are not taken");
    }
    while (true) {
        const bool kSpaced = reader.skip();
        if (reader.peek() == '}') {
            ++reader.at;
            return made;
        }
        if (!kSpaced) {
            return invalid("an expression's parts are apart");
        }
        if (reader.peek() == ':' && !made.annotation.has_value()) {
            ++reader.at;
            const std::string_view kFunction = reader.token();
            if (kFunction != "string" && kFunction != "number" && kFunction != "integer") {
                return invalid("a function is :string, :number, or :integer");
            }
            made.annotation = Annotation{.function = kFunction == "string"   ? MessageFunction::String
                                                     : kFunction == "number" ? MessageFunction::Number
                                                                             : MessageFunction::Integer};
            continue;
        }
        if (made.annotation.has_value() && reader.peek() != '@') {
            RAWFRAME_TRY(option(reader, *made.annotation));
            continue;
        }
        return invalid("an expression is an operand, a function, and its options; no attributes");
    }
}

/// A pattern up to the text's end, or, `quoted`, up to its `}}`.
result::Result<Pattern> pattern(Reader& reader, bool quotedPattern, std::size_t& placeholders) {
    Pattern made;
    const auto kText = [&made]() -> std::string& {
        if (made.empty() || made.back().placeholder.has_value()) {
            made.emplace_back();
        }
        return made.back().text;
    };
    while (true) {
        if (reader.done()) {
            if (quotedPattern) {
                return invalid("a quoted pattern ends with }}");
            }
            return made;
        }
        const char kEach = reader.peek();
        if (kEach == '\\') {
            ++reader.at;
            if (reader.peek() != '\\' && reader.peek() != '{' && reader.peek() != '}') {
                return invalid("text escapes only \\, {, and }");
            }
            kText() += reader.peek();
            ++reader.at;
        } else if (kEach == '{') {
            RAWFRAME_TRY_ASSIGN(Expression placeholder, expression(reader));
            if (placeholder.operand.literal) {
                return invalid("a placeholder reads a variable");
            }
            made.push_back(PatternPart{.text = {}, .placeholder = std::move(placeholder)});
            ++placeholders;
        } else if (kEach == '}') {
            if (quotedPattern && reader.ahead("}}")) {
                reader.at += 2;
                return made;
            }
            return invalid("a } in text is escaped");
        } else {
            kText() += kEach;
            ++reader.at;
        }
    }
}

result::Result<VariantKey> key(Reader& reader) {
    if (reader.peek() == '*') {
        ++reader.at;
        return VariantKey{.kind = KeyKind::Any, .text = {}};
    }
    if (reader.peek() == '|') {
        RAWFRAME_TRY_ASSIGN(std::string text, quoted(reader));
        return VariantKey{.kind = KeyKind::Quoted, .text = std::move(text)};
    }
    const std::string_view kName = reader.token();
    if (kName.empty()) {
        return invalid("a variant's key is *, a name, or a quoted literal");
    }
    return VariantKey{.kind = KeyKind::Name, .text = std::string{kName}};
}

/// The variables an expression reads.
void readsOf(const Expression& expression, std::vector<std::string>& reads) {
    if (!expression.operand.literal) {
        reads.push_back(expression.operand.text);
    }
}

/// An expression's annotation over what it reads, as its checks see it.
result::Result<std::optional<Annotation>> checked(const Expression& expression,
                                                  const std::map<std::string, std::optional<Annotation>>& declared) {
    std::optional<Annotation> base;
    if (!expression.operand.literal) {
        const auto kFound = declared.find(expression.operand.text);
        if (kFound != declared.end()) {
            base = kFound->second;
        }
    }
    const std::optional<Annotation> kMade = applied(base, expression.annotation);
    if (kMade.has_value() && kMade->function == MessageFunction::Number && kMade->maximumFraction.has_value() &&
        kMade->minimumFraction.value_or(0) > *kMade->maximumFraction) {
        return invalid("a number's minimumFractionDigits is at most its maximumFractionDigits");
    }
    if (expression.operand.literal && expression.annotation.has_value() &&
        ((expression.annotation->function == MessageFunction::Integer && !integerLiteral(expression.operand.text)) ||
         (expression.annotation->function == MessageFunction::Number && !numberLiteral(expression.operand.text)))) {
        return invalid("a literal a number function reads is a number");
    }
    return kMade;
}

result::Status validate(const Message& message) {
    std::map<std::string, std::optional<Annotation>> declared;
    std::set<std::string> seen;
    for (const Declaration& declaration : message.declarations) {
        std::vector<std::string> reads;
        readsOf(declaration.expression, reads);
        if (seen.contains(declaration.name) || (!declaration.input && std::ranges::contains(reads, declaration.name))) {
            return invalid("a declaration names a variable no earlier declaration named or read, and does not read "
                           "itself");
        }
        RAWFRAME_TRY_ASSIGN(const std::optional<Annotation> kAnnotation, checked(declaration.expression, declared));
        declared[declaration.name] = kAnnotation;
        seen.insert(declaration.name);
        seen.insert(reads.begin(), reads.end());
    }
    const auto kPattern = [&declared](const Pattern& pattern) -> result::Status {
        for (const PatternPart& part : pattern) {
            if (part.placeholder.has_value()) {
                RAWFRAME_TRY(checked(*part.placeholder, declared));
            }
        }
        return {};
    };
    RAWFRAME_TRY(kPattern(message.pattern));
    std::vector<Annotation> selecting;
    for (const std::string& selector : message.selectors) {
        const auto kFound = declared.find(selector);
        if (kFound == declared.end() || !kFound->second.has_value()) {
            return invalid("a selector is a declared variable a function annotates");
        }
        selecting.push_back(*kFound->second);
    }
    std::set<std::vector<std::pair<KeyKind, std::string>>> rows;
    std::size_t fallbacks = 0;
    for (const Variant& variant : message.variants) {
        if (variant.keys.size() != selecting.size()) {
            return invalid("a variant has a key for each selector");
        }
        std::vector<std::pair<KeyKind, std::string>> row;
        for (std::size_t at = 0; at < variant.keys.size(); ++at) {
            const VariantKey& each = variant.keys[at];
            const Annotation& annotation = selecting[at];
            const bool kString = annotation.function == MessageFunction::String;
            const bool kCategory = pluralCategory(each.text).has_value();
            const bool kExact = annotation.select.value_or(Selection::Cardinal) == Selection::Exact;
            const bool kFits =
                each.kind == KeyKind::Any ||
                (kString ? each.kind == KeyKind::Quoted
                         : each.kind == KeyKind::Name && ((kCategory && !kExact) || integerLiteral(each.text)));
            if (!kFits) {
                return invalid("a key is *, a quoted literal for a string, or a plural category (not with exact "
                               "selection) or an integer for a number");
            }
            row.emplace_back(each.kind, each.text);
        }
        if (!rows.insert(row).second) {
            return invalid("no two variants have the same keys");
        }
        fallbacks += std::ranges::all_of(variant.keys,
                                         [](const VariantKey& each) {
                                             return each.kind == KeyKind::Any;
                                         })
                         ? 1
                         : 0;
        RAWFRAME_TRY(kPattern(variant.pattern));
    }
    if (!message.selectors.empty() && fallbacks != 1) {
        return invalid("exactly one variant has * for every key");
    }
    return {};
}

} // namespace

bool integerLiteral(std::string_view text) {
    if (text.starts_with('-')) {
        text.remove_prefix(1);
    }
    return !text.empty() && text.size() <= kMostLiteralDigits && (text == "0" || text.front() != '0') &&
           std::ranges::all_of(text, [](char each) {
               return each >= '0' && each <= '9';
           });
}

bool numberLiteral(std::string_view text) {
    const std::size_t kPoint = text.find('.');
    if (kPoint == std::string_view::npos) {
        return integerLiteral(text);
    }
    const std::string_view kFraction = text.substr(kPoint + 1);
    return integerLiteral(text.substr(0, kPoint)) && !kFraction.empty() && kFraction.size() <= kMostLiteralDigits &&
           std::ranges::all_of(kFraction, [](char each) {
               return each >= '0' && each <= '9';
           });
}

bool variableName(std::string_view name) {
    return !name.empty() && name.front() >= 'a' && name.front() <= 'z' && std::ranges::all_of(name, [](char each) {
        return (each >= 'a' && each <= 'z') || (each >= 'A' && each <= 'Z') || (each >= '0' && each <= '9') ||
               each == '_';
    });
}

std::optional<Annotation> applied(const std::optional<Annotation>& base, const std::optional<Annotation>& own) {
    if (!own.has_value()) {
        return base;
    }
    Annotation made = *own;
    const bool kNumbers =
        base.has_value() && base->function != MessageFunction::String && own->function != MessageFunction::String;
    if (kNumbers) {
        made.select = own->select.has_value() ? own->select : base->select;
        made.grouping = own->grouping.has_value() ? own->grouping : base->grouping;
        made.minimumFraction = own->minimumFraction.has_value() ? own->minimumFraction : base->minimumFraction;
        made.maximumFraction = own->maximumFraction.has_value() ? own->maximumFraction : base->maximumFraction;
    }
    if (made.function == MessageFunction::Integer) {
        made.minimumFraction.reset();
        made.maximumFraction.reset();
    }
    return made;
}

result::Result<Message> parseMessage(std::string_view text, const MessageLimits& limits) {
    if (text.size() > limits.maximumBytes) {
        return overLimit("a message is longer than its limit");
    }
    Reader reader{.text = text, .at = 0};
    Message made;
    std::size_t placeholders = 0;
    reader.skip();
    if (!reader.ahead(".") && !reader.ahead("{{")) {
        if (!text.empty() && whitespace(text.front())) {
            return invalid("a simple message does not begin with whitespace");
        }
        reader.at = 0;
        RAWFRAME_TRY_ASSIGN(made.pattern, pattern(reader, false, placeholders));
    } else {
        while (true) {
            reader.skip();
            if (reader.ahead(".input")) {
                reader.at += 6;
                reader.skip();
                RAWFRAME_TRY_ASSIGN(Expression input, expression(reader));
                if (input.operand.literal) {
                    return invalid(".input declares a variable");
                }
                std::string name = input.operand.text;
                made.declarations.push_back(Declaration{.input = true, .name = std::move(name), .expression = input});
            } else if (reader.ahead(".local")) {
                reader.at += 6;
                if (!reader.skip()) {
                    return invalid(".local is apart from its variable");
                }
                RAWFRAME_TRY_ASSIGN(std::string name, variable(reader));
                reader.skip();
                if (reader.peek() != '=') {
                    return invalid(".local $name = {expression}");
                }
                ++reader.at;
                reader.skip();
                RAWFRAME_TRY_ASSIGN(Expression local, expression(reader));
                made.declarations.push_back(
                    Declaration{.input = false, .name = std::move(name), .expression = std::move(local)});
            } else if (reader.ahead(".match")) {
                reader.at += 6;
                while (true) {
                    const std::size_t kBefore = reader.at;
                    if (!reader.skip() || reader.peek() != '$') {
                        reader.at = kBefore;
                        break;
                    }
                    RAWFRAME_TRY_ASSIGN(std::string selector, variable(reader));
                    made.selectors.push_back(std::move(selector));
                }
                if (made.selectors.empty()) {
                    return invalid(".match has a selector");
                }
                while (true) {
                    reader.skip();
                    if (reader.done()) {
                        break;
                    }
                    Variant variant;
                    while (!reader.ahead("{{")) {
                        RAWFRAME_TRY_ASSIGN(VariantKey each, key(reader));
                        variant.keys.push_back(std::move(each));
                        const bool kSpaced = reader.skip();
                        if (!kSpaced && !reader.ahead("{{")) {
                            return invalid("a variant's keys are apart, then its quoted pattern");
                        }
                    }
                    reader.at += 2;
                    RAWFRAME_TRY_ASSIGN(variant.pattern, pattern(reader, true, placeholders));
                    made.variants.push_back(std::move(variant));
                }
                if (made.variants.empty()) {
                    return invalid(".match has variants");
                }
                break;
            } else if (reader.ahead("{{")) {
                reader.at += 2;
                RAWFRAME_TRY_ASSIGN(made.pattern, pattern(reader, true, placeholders));
                reader.skip();
                if (!reader.done()) {
                    return invalid("nothing follows a message's quoted pattern");
                }
                break;
            } else {
                return invalid("a complex message is .input and .local declarations, then {{a pattern}} or .match");
            }
        }
    }
    if (made.declarations.size() > limits.maximumDeclarations || made.selectors.size() > limits.maximumSelectors ||
        made.variants.size() > limits.maximumVariants || placeholders > limits.maximumPlaceholders) {
        return overLimit("a message has more declarations, selectors, variants, or placeholders than its limits");
    }
    RAWFRAME_TRY(validate(made));
    return made;
}

std::vector<std::string> argumentsOf(const Message& message) {
    std::set<std::string> locals;
    std::set<std::string> made;
    const auto kRead = [&locals, &made](const Expression& expression) {
        if (!expression.operand.literal && !locals.contains(expression.operand.text)) {
            made.insert(expression.operand.text);
        }
    };
    for (const Declaration& declaration : message.declarations) {
        kRead(declaration.expression);
        if (!declaration.input) {
            locals.insert(declaration.name);
        }
    }
    const auto kPattern = [&kRead](const Pattern& pattern) {
        for (const PatternPart& part : pattern) {
            if (part.placeholder.has_value()) {
                kRead(*part.placeholder);
            }
        }
    };
    kPattern(message.pattern);
    for (const Variant& variant : message.variants) {
        kPattern(variant.pattern);
    }
    return {made.begin(), made.end()};
}

} // namespace rawframe::localization
