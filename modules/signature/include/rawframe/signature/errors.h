#pragma once

#include "rawframe/base/bits128.h"
#include "rawframe/result/error.h"

#include <cstdint>

namespace rawframe::signature {

/// The domain of every Error this module creates.
inline constexpr result::ErrorDomain kSignatureDomain{base::parseBits128Hex("8a17ed52f851e8296290b69d29ec3a27").value};

enum class SignatureError : std::uint32_t {
    /// An envelope or key set that is not its canonical record.
    Malformed = 1,
    /// A key identity the key set does not list.
    UnknownKey = 2,
    /// Signed by a key the key set says is revoked (`publisher_key_revoked`).
    KeyRevoked = 3,
    /// A signature that does not verify.
    BadSignature = 4,
};

[[nodiscard]] constexpr result::ErrorCode code(SignatureError error) noexcept {
    return result::ErrorCode{static_cast<std::uint32_t>(error)};
}

} // namespace rawframe::signature
