#pragma once

#include "rawframe/base/bits128.h"
#include "rawframe/result/error.h"

#include <cstdint>

namespace rawframe::world_runtime {

/// The domain of every Error this module creates.
inline constexpr result::ErrorDomain kWorldRuntimeDomain{
    base::parseBits128Hex("d0f8bac6a2b476b004d3366f545f9937").value};

/// Codes within kWorldRuntimeDomain.
enum class WorldRuntimeError : std::uint32_t {
    SettingOutOfRange = 1,
    AlreadyStarted = 2,
    /// Something that needs the running World was asked for before start.
    NotStarted = 3,
    /// A checkpoint could not be read, written, or published.
    CheckpointFailed = 4,
};

[[nodiscard]] constexpr result::ErrorCode code(WorldRuntimeError error) noexcept {
    return result::ErrorCode{static_cast<std::uint32_t>(error)};
}

} // namespace rawframe::world_runtime
