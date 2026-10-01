#pragma once

#include "rawframe/base/bits128.h"
#include "rawframe/result/error.h"

#include <cstdint>

namespace rawframe::world_ui {

/// The domain of every Error this module creates.
inline constexpr result::ErrorDomain kWorldUiDomain{base::parseBits128Hex("07c3a3bec3b01c164f0d6255d6863e47").value};

/// Codes within kWorldUiDomain.
enum class WorldUiError : std::uint32_t {
    /// The game's UI node components are not as `rawframe.ui` lays them
    /// out, or a `ui` line names a component that is not one.
    BadNodes = 1,
};

[[nodiscard]] constexpr result::ErrorCode code(WorldUiError error) noexcept {
    return result::ErrorCode{static_cast<std::uint32_t>(error)};
}

} // namespace rawframe::world_ui
