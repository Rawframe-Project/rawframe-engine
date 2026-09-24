#pragma once

#include "rawframe/base/bits128.h"
#include "rawframe/result/error.h"

#include <cstdint>

namespace rawframe::network_quic {

/// The domain of the Errors this module creates itself. What the provider
/// contract refuses is refused in `network::kNetworkDomain`, the same as for
/// every provider.
inline constexpr result::ErrorDomain kQuicDomain{base::parseBits128Hex("0455038e966f8d9cd80b0c86bbf8ca8b").value};

enum class QuicError : std::uint32_t {
    /// MsQuic could not be opened, registered, or configured.
    Unavailable = 1,
    /// A certificate or key that cannot be read or made.
    BadCertificate = 2,
    /// An endpoint that is not `host:port`, or a listen address that is not
    /// an IP literal.
    BadEndpoint = 3,
    /// Listening needs a certificate and connecting needs a pin.
    MissingIdentity = 4,
    /// A fingerprint that is not 64 hexadecimal digits.
    BadFingerprint = 5,
};

[[nodiscard]] constexpr result::ErrorCode code(QuicError error) noexcept {
    return result::ErrorCode{static_cast<std::uint32_t>(error)};
}

} // namespace rawframe::network_quic
