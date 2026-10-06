#pragma once

#include "rawframe/base/bits128.h"
#include "rawframe/result/error.h"

#include <cstdint>

namespace rawframe::http {

/// The domain of every Error this unit creates.
inline constexpr result::ErrorDomain kHttpDomain{base::parseBits128Hex("2c28a1bc31854ee85d67e7afd6d0bb29").value};

enum class HttpError : std::uint32_t {
    /// A URL out of form, or of a scheme other than http and https.
    BadUrl = 1,
    /// The host's name did not resolve, or no address of it answered.
    Unreachable = 2,
    /// The server's certificate is not one the trusted authorities vouch
    /// for, for that host; or TLS could not be agreed.
    Untrusted = 3,
    /// The server answered out of HTTP/1.1's form.
    Malformed = 4,
    /// The server's answer is not 200, or is a redirect that may not be
    /// followed.
    Status = 5,
    /// The body is longer than the caller's ceiling.
    TooLarge = 6,
    /// The connection ended or went silent before the answer was whole.
    Interrupted = 7,
    /// No trusted authorities could be read.
    NoAuthorities = 8,
};

[[nodiscard]] constexpr result::ErrorCode code(HttpError error) noexcept {
    return result::ErrorCode{static_cast<std::uint32_t>(error)};
}

} // namespace rawframe::http
