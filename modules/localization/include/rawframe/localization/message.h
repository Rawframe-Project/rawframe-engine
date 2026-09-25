#pragma once

// Messages (ADR-0050, SPEC-0033): a closed subset of Unicode MessageFormat
// 2.0, read once and checked whole, then formatted as often as asked.
//
//   Hello, {$name}!
//   .input {$count :integer}
//   .match $count
//   0   {{No files}}
//   one {{One file}}
//   *   {{{$count} files}}
//
// A simple message is text with `{$argument}` placeholders; it may not
// begin with `.` or with whitespace (D143: MF2 leaves leading whitespace to
// be trimmed or kept, so the subset keeps neither). Text escapes are `\{`,
// `\}`, and `\\`. A complex message is `.input {$a ...}` and
// `.local $b = {... }` declarations, then either a quoted pattern
// `{{...}}` or `.match $x ...` and its variants, each a key for each
// selector (a CLDR plural category, an integer, a quoted `|literal|`, or
// `*`) and a quoted pattern; exactly one variant is all `*`.
//
// The functions are `:string` (no options), `:integer` (`select` of
// `cardinal`, `ordinal`, or `exact`, and `useGrouping` of `auto` or
// `never`), and `:number` (those, and `minimumFractionDigits` and
// `maximumFractionDigits`, 0 to 15, by default 0 and 3 or the minimum if
// that is more, as ECMA-402 does (D143); only a written maximum below the
// minimum is refused). A function on a
// value a declaration already annotated keeps that annotation's options
// unless it sets them again. Operands are variables or quoted literals;
// markup, attributes, and every other function are refused.
//
// Variables are named `[a-z][a-zA-Z0-9_]*`. A declaration may not name a
// variable any earlier declaration named or read, nor read itself; a
// selector is a declared variable a function annotates.

#include "rawframe/localization/locale.h"
#include "rawframe/localization/plural.h"
#include "rawframe/result/result.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace rawframe::localization {

enum class MessageFunction : std::uint8_t {
    String,
    Number,
    Integer,
};

enum class Selection : std::uint8_t {
    Cardinal,
    Ordinal,
    Exact,
};

/// A function and the options written on it; an option not written is
/// inherited, or its default.
struct Annotation {
    MessageFunction function = MessageFunction::String;
    std::optional<Selection> select;
    std::optional<bool> grouping;
    std::optional<std::uint32_t> minimumFraction;
    std::optional<std::uint32_t> maximumFraction;

    friend bool operator==(const Annotation&, const Annotation&) = default;
};

/// A variable by name, or a quoted literal's text.
struct Operand {
    bool literal = false;
    std::string text;

    friend bool operator==(const Operand&, const Operand&) = default;
};

struct Expression {
    Operand operand;
    std::optional<Annotation> annotation;

    friend bool operator==(const Expression&, const Expression&) = default;
};

struct Declaration {
    /// `.input`, or else `.local`.
    bool input = false;
    std::string name;
    Expression expression;

    friend bool operator==(const Declaration&, const Declaration&) = default;
};

/// Text, or a placeholder.
struct PatternPart {
    std::string text;
    std::optional<Expression> placeholder;

    friend bool operator==(const PatternPart&, const PatternPart&) = default;
};

using Pattern = std::vector<PatternPart>;

enum class KeyKind : std::uint8_t {
    /// `*`.
    Any,
    /// Unquoted: a plural category or an integer.
    Name,
    /// `|quoted|`.
    Quoted,
};

struct VariantKey {
    KeyKind kind = KeyKind::Any;
    std::string text;

    friend bool operator==(const VariantKey&, const VariantKey&) = default;
};

struct Variant {
    std::vector<VariantKey> keys;
    Pattern pattern;

    friend bool operator==(const Variant&, const Variant&) = default;
};

struct Message {
    std::vector<Declaration> declarations;
    /// A matcher's selectors and variants; none for a pattern alone.
    std::vector<std::string> selectors;
    std::vector<Variant> variants;
    /// The message's pattern when it has no matcher.
    Pattern pattern;

    friend bool operator==(const Message&, const Message&) = default;
};

/// SPEC-0033's named limits for messages.
struct MessageLimits {
    std::size_t maximumBytes = 4096;
    std::size_t maximumDeclarations = 16;
    std::size_t maximumSelectors = 4;
    std::size_t maximumVariants = 64;
    std::size_t maximumPlaceholders = 64;
    std::size_t maximumArguments = 32;
};

/// Reads and checks a message. Refuses (`MessageInvalid`) one out of the
/// subset and (`OverLimit`) one past a limit.
[[nodiscard]] result::Result<Message> parseMessage(std::string_view text, const MessageLimits& limits = {});

/// The arguments a message reads from its caller, each once, in name order.
[[nodiscard]] std::vector<std::string> argumentsOf(const Message& message);

/// SPEC-0033's closed argument types.
using ArgumentValue = std::variant<std::string, std::int64_t, double, bool>;

struct Argument {
    std::string_view name;
    ArgumentValue value;
};

/// A message formatted in `locale`, the locale of the text it came from:
/// its selection made on each selector's formatted value, and its
/// placeholders written as their functions say. Refuses a call without an
/// argument the message reads (`ArgumentMissing`), with an argument of a
/// type its function does not take or a decimal not finite
/// (`ArgumentMistyped`), or with more arguments than the limit
/// (`OverLimit`). Given what it asks for, it cannot fail otherwise.
[[nodiscard]] result::Result<std::string> format(const Message& message,
                                                 const Locale& locale,
                                                 std::span<const Argument> arguments,
                                                 const MessageLimits& limits = {});

} // namespace rawframe::localization
