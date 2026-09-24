#pragma once

#include <compare>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <span>
#include <string_view>

namespace rawframe::base {

/// Exactly 128 bits, ordered, comparable, and canonically representable as
/// hexadecimal. It identifies nothing. Identity domains wrap it.
struct Bits128 {
    std::uint64_t high = 0;
    std::uint64_t low = 0;

    friend constexpr bool operator==(const Bits128&, const Bits128&) noexcept = default;
    friend constexpr std::strong_ordering operator<=>(const Bits128&, const Bits128&) noexcept = default;
};

/// The canonical text form is exactly this many lowercase hexadecimal digits,
/// with no prefix, no separators, and no surrounding whitespace.
inline constexpr std::size_t kBits128HexDigits = 32;

/// What parsing produced. Deliberately not a Result: base is below
/// rawframe.result and inverting that is the dependency ADR-0075 forbids.
struct Bits128Parse {
    Bits128 value;
    bool parsed = false;
};

/// Total. Never asserts, never allocates, never terminates. Accepts exactly the
/// canonical form and rejects everything else, including uppercase digits,
/// a 0x prefix, separators, padding, and any length but kBits128HexDigits.
[[nodiscard]] constexpr Bits128Parse parseBits128Hex(std::string_view text) noexcept {
    if (text.size() != kBits128HexDigits) {
        return Bits128Parse{Bits128{}, false};
    }

    std::uint64_t high = 0;
    std::uint64_t low = 0;
    // Indexing is spelled as pointer arithmetic throughout this header. The
    // bounds-safe alternative the analyzer asks for is `at`, which throws, and
    // ADR-0008 disables exceptions; the repository's own analysis policy admits
    // pointer arithmetic for exactly this reason. Every offset below is bounded
    // by the size check above or by the span's static extent.
    for (std::size_t index = 0; index < kBits128HexDigits; ++index) {
        const char kDigit = *(text.data() + index);
        std::uint64_t nibble = 0;
        if (kDigit >= '0' && kDigit <= '9') {
            nibble = static_cast<std::uint64_t>(kDigit - '0');
        } else if (kDigit >= 'a' && kDigit <= 'f') {
            nibble = static_cast<std::uint64_t>(kDigit - 'a') + 10;
        } else {
            return Bits128Parse{Bits128{}, false};
        }

        if (index < kBits128HexDigits / 2) {
            high = (high << 4) | nibble;
        } else {
            low = (low << 4) | nibble;
        }
    }

    return Bits128Parse{Bits128{high, low}, true};
}

/// Writes exactly kBits128HexDigits characters and no terminator.
constexpr void formatBits128Hex(const Bits128& value, std::span<char, kBits128HexDigits> out) noexcept {
    constexpr std::string_view kDigits = "0123456789abcdef";
    constexpr std::size_t kHalf = kBits128HexDigits / 2;

    for (std::size_t index = 0; index < kHalf; ++index) {
        const unsigned kShift = static_cast<unsigned>(60 - (4 * index));
        *(out.data() + index) = *(kDigits.data() + static_cast<std::size_t>((value.high >> kShift) & 0xFU));
        *(out.data() + kHalf + index) = *(kDigits.data() + static_cast<std::size_t>((value.low >> kShift) & 0xFU));
    }
}

} // namespace rawframe::base

/// Makes Bits128 usable as a key in standard associative containers, and nothing
/// else. The value is unspecified and unstable across builds, runs, and
/// platforms, and it MUST NOT be persisted, transmitted, logged as identity, or
/// used to derive any durable value: ADR-0011 forbids deriving stable identity
/// from layout or implementation, and a hash that reached a durable artifact
/// would be exactly that.
template <> struct std::hash<rawframe::base::Bits128> {
    [[nodiscard]] std::size_t operator()(const rawframe::base::Bits128& value) const noexcept {
        const std::size_t kHigh = std::hash<std::uint64_t>{}(value.high);
        const std::size_t kLow = std::hash<std::uint64_t>{}(value.low);
        return kHigh ^ (kLow + static_cast<std::size_t>(0x9E3779B97F4A7C15ULL) + (kHigh << 6) + (kHigh >> 2));
    }
};
