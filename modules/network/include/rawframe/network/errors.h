#pragma once

#include "rawframe/base/bits128.h"
#include "rawframe/result/error.h"

#include <cstdint>

namespace rawframe::network {

/// The domain of every Error this module creates.
inline constexpr result::ErrorDomain kNetworkDomain{base::parseBits128Hex("58a3e358f82b5907969667a658f9a13f").value};

/// Codes within kNetworkDomain (SPEC-0010 failure distinctions).
enum class NetworkError : std::uint32_t {
    /// The input ends inside a value. On a stream, more bytes may complete it.
    Truncated = 1,
    /// A varint longer than its value needs: generation 1 takes one spelling.
    NonCanonical = 2,
    /// A value above what its field or the profile allows.
    Oversized = 3,
    /// Bytes after a record that must consume its input exactly.
    TrailingBytes = 4,
    /// An even frame type or a stream or lane kind this generation does not
    /// know.
    UnknownCritical = 5,
    /// A stream format version other than generation 1's.
    WrongVersion = 6,
    /// No room left in the output.
    BufferFull = 7,
};

[[nodiscard]] constexpr result::ErrorCode code(NetworkError error) noexcept {
    return result::ErrorCode{static_cast<std::uint32_t>(error)};
}

} // namespace rawframe::network
