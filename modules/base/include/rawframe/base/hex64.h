#pragma once

#include <cstdint>
#include <optional>
#include <string_view>

namespace rawframe::base {

/// A 64-bit identity's canonical text, as documents and game descriptions
/// write actions, contexts, labels, components, and style classes (D431):
/// exactly 16 lowercase hexadecimal digits, no prefix, no separators.
/// Anything else is no identity.
[[nodiscard]] constexpr std::optional<std::uint64_t> parseHex64(std::string_view text) noexcept {
    if (text.size() != 16) {
        return std::nullopt;
    }
    std::uint64_t value = 0;
    for (const char kDigit : text) {
        value <<= 4U;
        if (kDigit >= '0' && kDigit <= '9') {
            value |= static_cast<std::uint64_t>(kDigit - '0');
        } else if (kDigit >= 'a' && kDigit <= 'f') {
            value |= static_cast<std::uint64_t>(kDigit - 'a') + 10;
        } else {
            return std::nullopt;
        }
    }
    return value;
}

} // namespace rawframe::base
