#pragma once

#include "rawframe/base/bits128.h"
#include "rawframe/result/error.h"

#include <cstdint>

namespace rawframe::world_tooling {

/// The domain of every Error this module creates.
inline constexpr result::ErrorDomain kToolingDomain{base::parseBits128Hex("bd734a90390c7b861b1f659cd8ea9a99").value};

/// Codes within kToolingDomain, as the protocol's error records name them.
enum class ToolingError : std::uint32_t {
    /// A record out of the protocol's form.
    Malformed = 1,
    /// No hello yet, or a hello whose token is not the server's.
    Unauthenticated = 2,
    /// A verb the client's grants do not reach.
    NotGranted = 3,
    /// A protocol version or verb this server does not speak.
    Unsupported = 4,
    /// A record past the protocol's limit.
    LimitExceeded = 5,
    /// A configuration the endpoint cannot run with.
    Configuration = 6,
    /// An entity not alive in the World, or no World running.
    NotFound = 7,
    /// The game is stopped at a breakpoint, or not, as the verb needs
    /// (D460).
    Stopped = 8,
};

[[nodiscard]] constexpr result::ErrorCode code(ToolingError error) noexcept {
    return result::ErrorCode{static_cast<std::uint32_t>(error)};
}

} // namespace rawframe::world_tooling
