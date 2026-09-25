#pragma once

// Plural categories (ADR-0050, SPEC-0033): CLDR's cardinal and ordinal
// rules, compiled ahead of time into code by tools/generate_cldr.py, so no
// rule text is read or evaluated at run time. A category is chosen from a
// number as it is written, not as it is worth: `1` and `1.0` are different
// operands, and English says "1 day" but "1.0 days".

#include "rawframe/localization/locale.h"

#include <cstdint>
#include <optional>
#include <string_view>

namespace rawframe::localization {

enum class PluralCategory : std::uint8_t {
    Zero,
    One,
    Two,
    Few,
    Many,
    Other,
};

/// The category's CLDR keyword: `zero`, `one`, `two`, `few`, `many`, or
/// `other`.
[[nodiscard]] std::string_view nameOf(PluralCategory category) noexcept;
/// The category a keyword names, if it names one.
[[nodiscard]] std::optional<PluralCategory> pluralCategory(std::string_view name) noexcept;

/// CLDR's plural operands of a written number (UTS #35): `i` its integer
/// digits, `v` how many fraction digits are written and `f` them as an
/// integer, `w` and `t` the same without trailing zeros, and `e` the
/// compact exponent, which is always nought here. `n` is `i` and `f`
/// together.
struct PluralOperands {
    std::uint64_t i = 0;
    std::uint32_t v = 0;
    std::uint64_t f = 0;
    std::uint32_t w = 0;
    std::uint64_t t = 0;
    std::uint32_t e = 0;

    friend bool operator==(const PluralOperands&, const PluralOperands&) = default;
};

/// The operands of a number written in ASCII: an optional `-`, digits, and
/// optionally `.` and more digits (`-12.340`), at most eighteen of each;
/// none for any other text.
[[nodiscard]] std::optional<PluralOperands> operandsOf(std::string_view number) noexcept;

/// The cardinal (`3 files`) or ordinal (`3rd file`) category of a number in
/// a locale: CLDR's rules for its tag, else for its language, else
/// `other`.
[[nodiscard]] PluralCategory cardinal(const Locale& locale, const PluralOperands& operands) noexcept;
[[nodiscard]] PluralCategory ordinal(const Locale& locale, const PluralOperands& operands) noexcept;

} // namespace rawframe::localization
