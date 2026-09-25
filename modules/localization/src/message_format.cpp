// Formatting messages of SPEC-0033's MessageFormat 2.0 subset: selection,
// then the chosen pattern with its placeholders written.

#include "message_parts.h"
#include "rawframe/localization/errors.h"
#include "rawframe/localization/message.h"
#include "rawframe/localization/number.h"
#include "rawframe/localization/plural.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <map>

namespace rawframe::localization {

namespace {

std::unexpected<result::Error> missing(std::string_view why) {
    return result::fail(
        result::ErrorClass::InvalidArgument, kLocalizationDomain, code(LocalizationError::ArgumentMissing), why);
}

std::unexpected<result::Error> mistyped(std::string_view why) {
    return result::fail(
        result::ErrorClass::InvalidArgument, kLocalizationDomain, code(LocalizationError::ArgumentMistyped), why);
}

/// A value and the function over it.
struct Resolved {
    ArgumentValue value;
    Annotation annotation;
};

/// What a value is written as with no function over it.
Annotation defaultOf(const ArgumentValue& value) {
    if (std::holds_alternative<std::int64_t>(value)) {
        return Annotation{.function = MessageFunction::Integer};
    }
    if (std::holds_alternative<double>(value)) {
        return Annotation{.function = MessageFunction::Number};
    }
    return Annotation{.function = MessageFunction::String};
}

/// A number literal as the value its function reads; the parser has
/// checked that it is one.
ArgumentValue literalValue(const std::string& text, MessageFunction function) {
    if (function == MessageFunction::String) {
        return text;
    }
    if (integerLiteral(text)) {
        std::int64_t made = 0;
        std::from_chars(text.data(), text.data() + text.size(), made);
        return made;
    }
    double made = 0.0;
    std::from_chars(text.data(), text.data() + text.size(), made);
    return made;
}

struct Formatter {
    const Locale& locale;
    std::span<const Argument> arguments;
    std::map<std::string, Resolved, std::less<>> declared;

    result::Result<Resolved> resolve(const Expression& expression) const {
        std::optional<ArgumentValue> value;
        std::optional<Annotation> base;
        if (expression.operand.literal) {
            value = literalValue(expression.operand.text,
                                 expression.annotation.has_value() ? expression.annotation->function
                                                                   : MessageFunction::String);
        } else if (const auto kFound = declared.find(expression.operand.text); kFound != declared.end()) {
            value = kFound->second.value;
            base = kFound->second.annotation;
        } else {
            // The first argument of the name is the one read.
            for (const Argument& argument : arguments) {
                if (argument.name == expression.operand.text) {
                    value = argument.value;
                    break;
                }
            }
            if (!value.has_value()) {
                return missing("a format call gives every argument its message reads");
            }
        }
        const std::optional<Annotation> kApplied = applied(base, expression.annotation);
        const Annotation kAnnotation = kApplied.has_value() ? *kApplied : defaultOf(*value);
        const bool kInteger = std::holds_alternative<std::int64_t>(*value);
        const bool kDecimal = std::holds_alternative<double>(*value);
        const bool kFits = kAnnotation.function == MessageFunction::String    ? !kInteger && !kDecimal
                           : kAnnotation.function == MessageFunction::Integer ? kInteger
                                                                              : kInteger || kDecimal;
        if (!kFits) {
            return mistyped("an integer is read by :integer or :number, a decimal by :number, and text or a truth "
                            "by :string");
        }
        if (kDecimal && !std::isfinite(std::get<double>(*value))) {
            return mistyped("a decimal argument is finite");
        }
        return Resolved{.value = std::move(*value), .annotation = kAnnotation};
    }
};

/// A value written plainly: its text, or its number in ASCII, which is
/// what selection reads.
std::string plainOf(const Resolved& resolved) {
    const Annotation& annotation = resolved.annotation;
    if (const auto* text = std::get_if<std::string>(&resolved.value)) {
        return *text;
    }
    if (const auto* truth = std::get_if<bool>(&resolved.value)) {
        return *truth ? "true" : "false";
    }
    const std::uint32_t kMinimum = annotation.minimumFraction.value_or(0);
    if (const auto* integer = std::get_if<std::int64_t>(&resolved.value)) {
        std::string made = plainInteger(*integer);
        if (kMinimum > 0) {
            made += '.';
            made.append(kMinimum, '0');
        }
        return made;
    }
    // Checked finite, with digits in order and under the ceiling: always
    // written.
    return plainDecimal(std::get<double>(resolved.value),
                        kMinimum,
                        annotation.maximumFraction.value_or(std::max(kMinimum, kDefaultMaximumFraction)))
        .value_or("");
}

std::string written(const Locale& locale, const Resolved& resolved) {
    const std::string kPlain = plainOf(resolved);
    if (resolved.annotation.function == MessageFunction::String) {
        return kPlain;
    }
    return localized(locale, kPlain, resolved.annotation.grouping.value_or(true));
}

/// How well a key matches a selector's value: 0 exactly, 1 by its plural
/// category, 2 as `*`; none when it does not.
std::optional<int>
rankOf(const Locale& locale, const Resolved& resolved, const std::string& plain, const VariantKey& key) {
    if (key.kind == KeyKind::Any) {
        return 2;
    }
    if (key.text == plain) {
        return 0;
    }
    const Selection kSelection = resolved.annotation.select.value_or(Selection::Cardinal);
    const std::optional<PluralCategory> kCategory = pluralCategory(key.text);
    if (resolved.annotation.function == MessageFunction::String || key.kind != KeyKind::Name ||
        kSelection == Selection::Exact || !kCategory.has_value()) {
        return std::nullopt;
    }
    // A number past CLDR's operands (more than eighteen digits a side) is
    // `other`.
    const std::optional<PluralOperands> kOperands = operandsOf(plain);
    const PluralCategory kMade = !kOperands.has_value()             ? PluralCategory::Other
                                 : kSelection == Selection::Ordinal ? ordinal(locale, *kOperands)
                                                                    : cardinal(locale, *kOperands);
    return kMade == *kCategory ? std::optional{1} : std::nullopt;
}

} // namespace

result::Result<std::string>
format(const Message& message, const Locale& locale, std::span<const Argument> arguments, const MessageLimits& limits) {
    if (arguments.size() > limits.maximumArguments) {
        return result::fail(result::ErrorClass::InvalidArgument,
                            kLocalizationDomain,
                            code(LocalizationError::OverLimit),
                            "a format call has more arguments than its limit");
    }
    Formatter formatter{.locale = locale, .arguments = arguments, .declared = {}};
    for (const Declaration& declaration : message.declarations) {
        RAWFRAME_TRY_ASSIGN(Resolved resolved, formatter.resolve(declaration.expression));
        formatter.declared.insert_or_assign(declaration.name, std::move(resolved));
    }
    const Pattern* chosen = &message.pattern;
    if (!message.selectors.empty()) {
        std::vector<const Resolved*> selecting;
        std::vector<std::string> plains;
        for (const std::string& selector : message.selectors) {
            // The parser has checked each is declared.
            const Resolved& resolved = formatter.declared.find(selector)->second;
            selecting.push_back(&resolved);
            plains.push_back(plainOf(resolved));
        }
        // The best variant: exact before category before `*`, the first
        // selector the most significant. The all-`*` variant always matches.
        std::optional<std::vector<int>> best;
        for (const Variant& variant : message.variants) {
            std::vector<int> ranks;
            for (std::size_t at = 0; at < variant.keys.size(); ++at) {
                const std::optional<int> kRank = rankOf(locale, *selecting[at], plains[at], variant.keys[at]);
                if (!kRank.has_value()) {
                    break;
                }
                ranks.push_back(*kRank);
            }
            if (ranks.size() == variant.keys.size() && (!best.has_value() || ranks < *best)) {
                best = std::move(ranks);
                chosen = &variant.pattern;
            }
        }
    }
    std::string made;
    for (const PatternPart& part : *chosen) {
        if (!part.placeholder.has_value()) {
            made += part.text;
            continue;
        }
        RAWFRAME_TRY_ASSIGN(const Resolved kResolved, formatter.resolve(*part.placeholder));
        made += written(locale, kResolved);
    }
    return made;
}

} // namespace rawframe::localization
