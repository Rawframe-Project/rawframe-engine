#pragma once

#include "rawframe/base/bits128.h"

#include <compare>
#include <cstddef>
#include <string_view>

namespace rawframe::schema {

/// The canonical text of a stable ID: lowercase UUID form, 36 characters with
/// hyphens after the 8th, 12th, 16th, and 20th digits (SPEC-0006).
inline constexpr std::size_t kStableIdTextLength = 36;

/// Parses canonical UUID text, or returns a zero value with `parsed` false.
/// Total and usable at compile time, so stable IDs can be constants.
[[nodiscard]] constexpr base::Bits128Parse parseStableIdText(std::string_view text) noexcept {
    if (text.size() != kStableIdTextLength) {
        return {};
    }
    char digits[base::kBits128HexDigits]{};
    std::size_t count = 0;
    for (std::size_t index = 0; index < text.size(); ++index) {
        const char kCharacter = *(text.data() + index);
        const bool kHyphenPosition = index == 8 || index == 13 || index == 18 || index == 23;
        if (kHyphenPosition) {
            if (kCharacter != '-') {
                return {};
            }
            continue;
        }
        digits[count++] = kCharacter;
    }
    return base::parseBits128Hex(std::string_view{digits, base::kBits128HexDigits});
}

/// The stable 128-bit identity of a component type. Chosen once, committed,
/// and never derived from a name, path, or layout. Zero is invalid.
struct ComponentTypeId {
    base::Bits128 value;

    /// From canonical UUID text; a malformed constant does not compile.
    [[nodiscard]] static consteval ComponentTypeId fromText(std::string_view text) {
        const base::Bits128Parse kParsed = parseStableIdText(text);
        if (!kParsed.parsed || kParsed.value == base::Bits128{}) {
            invalidStableIdText();
        }
        return ComponentTypeId{kParsed.value};
    }

    [[nodiscard]] constexpr bool valid() const noexcept {
        return value != base::Bits128{};
    }

    friend constexpr bool operator==(const ComponentTypeId&, const ComponentTypeId&) noexcept = default;
    friend constexpr std::strong_ordering operator<=>(const ComponentTypeId&,
                                                      const ComponentTypeId&) noexcept = default;

private:
    // Not constexpr: reaching it during constant evaluation is the error.
    static void invalidStableIdText() noexcept;
};

} // namespace rawframe::schema
