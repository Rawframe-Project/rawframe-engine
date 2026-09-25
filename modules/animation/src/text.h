#pragma once

// What the skeleton and clip documents share: their members, identities,
// names, and arrays of numbers as the authored-document profile holds them.

#include "rawframe/base/bits128.h"
#include "rawframe/document/json.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace rawframe::animation {

/// An object with exactly these members.
[[nodiscard]] bool hasMembers(const document::Value& value, std::initializer_list<std::string_view> names);

/// 32 lowercase hex digits.
[[nodiscard]] std::string hexOf(base::Bits128 value);
[[nodiscard]] std::optional<base::Bits128> bits128Of(const document::Value* value);

/// 16 lowercase hex digits.
[[nodiscard]] std::string hexOf(std::uint64_t value);
[[nodiscard]] std::optional<std::uint64_t> bits64Of(const document::Value* value);

/// `^[a-z][a-z0-9_]*$`, at most 64 characters.
[[nodiscard]] bool machineName(std::string_view name) noexcept;

/// A finite number.
[[nodiscard]] std::optional<double> numberOf(const document::Value* value);

/// The first `width` numbers, as an array.
[[nodiscard]] document::Value arrayOf(const std::array<double, 4>& numbers, std::size_t width);
/// An array of exactly `width` finite numbers.
[[nodiscard]] std::optional<std::array<double, 4>> numbersOf(const document::Value* value, std::size_t width);

/// Every one finite.
[[nodiscard]] bool finite(std::span<const double> numbers) noexcept;

/// Of length one, to a millionth.
[[nodiscard]] bool unit(const std::array<double, 4>& rotation) noexcept;

} // namespace rawframe::animation
