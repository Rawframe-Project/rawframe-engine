#pragma once

// What reading and formatting messages share.

#include "rawframe/localization/message.h"
#include "rawframe/localization/number.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

namespace rawframe::localization {

/// `:number`'s default greatest fraction digits (SPEC-0033), or its least
/// when that is more (D143).
inline constexpr std::uint32_t kDefaultMaximumFraction = 3;
/// The fraction-digit options' ceiling: SPEC-0033's named limit.
inline constexpr std::uint32_t kMostFractionDigitsOption = kMostFractionDigits;

/// The most digits a number literal's integer or fraction part has, so an
/// integer literal is an int64_t and the operands of either are CLDR's.
inline constexpr std::size_t kMostLiteralDigits = 18;

/// `-?(0|[1-9][0-9]*)`, at most the digits above.
[[nodiscard]] bool integerLiteral(std::string_view text);
/// An integer literal, then optionally `.` and digits.
[[nodiscard]] bool numberLiteral(std::string_view text);

/// A run of a pattern's text in the message's bytes, escapes as written.
struct TextRun {
    std::size_t begin = 0;
    std::size_t end = 0;
};

/// A pattern's bytes (inside `{{` and `}}` when quoted) and its text runs.
struct PatternSpan {
    std::size_t begin = 0;
    std::size_t end = 0;
    std::vector<TextRun> runs;
};

/// `parseMessage`, also saying where each pattern is, in the order they
/// are written.
[[nodiscard]] result::Result<Message>
parseMessage(std::string_view text, const MessageLimits& limits, std::vector<PatternSpan>* spans);

/// `[a-z][a-zA-Z0-9_]*`.
[[nodiscard]] bool variableName(std::string_view name);

/// A function over a value another annotated: numbers keep the options
/// the new one does not set; `:integer` has no fraction digits.
[[nodiscard]] std::optional<Annotation> applied(const std::optional<Annotation>& base,
                                                const std::optional<Annotation>& own);

} // namespace rawframe::localization
