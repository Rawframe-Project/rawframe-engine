#pragma once

#include "rawframe/base/bits128.h"
#include "rawframe/result/error.h"

#include <cstdint>

namespace rawframe::ui {

/// The domain of every Error this module creates.
inline constexpr result::ErrorDomain kUiDomain{base::parseBits128Hex("eb85a21cf277664027a97e09121ad351").value};

/// Codes within kUiDomain.
enum class UiError : std::uint32_t {
    /// A value or a node the tree cannot take: out of range, or a parent
    /// for a node that has one.
    Invalid = 1,
    /// Past a tree's limit.
    Capacity = 2,
    /// A node no longer in the tree.
    Stale = 3,
};

[[nodiscard]] constexpr result::ErrorCode code(UiError error) noexcept {
    return result::ErrorCode{static_cast<std::uint32_t>(error)};
}

} // namespace rawframe::ui
