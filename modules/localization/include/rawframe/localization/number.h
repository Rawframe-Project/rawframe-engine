#pragma once

// Numbers (ADR-0050, SPEC-0033's `:number` and `:integer`): a value first
// written plainly, in ASCII digits with `-` and `.`, which is what plural
// categories are chosen from, then in a locale's symbols, digits, and
// grouping from CLDR, compiled ahead of time. Nothing asks the platform how
// to write a number: the same value and locale give the same bytes
// everywhere.

#include "rawframe/localization/locale.h"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace rawframe::localization {

/// SPEC-0033's named fraction-digit ceiling.
inline constexpr std::uint32_t kMostFractionDigits = 15;

/// An integer written plainly: `-1234`.
[[nodiscard]] std::string plainInteger(std::int64_t value);

/// A decimal written plainly with at least `minimumFraction` and at most
/// `maximumFraction` fraction digits: the nearest such number to the
/// binary value, ties to even, trailing zeros past the minimum dropped, and
/// no sign on a value written as nought (`-0.0004` to three digits is `0`).
/// None for a value not finite, or digits past the ceiling or out of order.
[[nodiscard]] std::optional<std::string>
plainDecimal(double value, std::uint32_t minimumFraction, std::uint32_t maximumFraction);

/// A plain number in a locale: its minus sign, decimal separator, and
/// digits, and with `grouping` its group separator at its grouping sizes
/// once there are enough integer digits (`1234` stays whole in Spanish,
/// `12.345` does not). The locale's own data, else its language's, else
/// CLDR's root: `.`, `,`, `-`, and groups of three.
[[nodiscard]] std::string localized(const Locale& locale, std::string_view plain, bool grouping);

} // namespace rawframe::localization
