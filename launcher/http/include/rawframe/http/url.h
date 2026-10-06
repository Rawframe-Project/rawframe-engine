#pragma once

#include "rawframe/result/result.h"

#include <cstdint>
#include <string>
#include <string_view>

namespace rawframe::http {

/// An http or https URL, as a request needs it.
struct Url {
    bool secure = true;
    /// A name or an IPv4 address, or an IPv6 address without its brackets.
    std::string host;
    std::uint16_t port = 443;
    /// The path and query, `/` at least.
    std::string target;

    /// The Host header's value: the host, bracketed if IPv6, and the port
    /// unless it is the scheme's own.
    [[nodiscard]] std::string authority() const;
    /// The URL again.
    [[nodiscard]] std::string text() const;
};

/// Reads `http://` or `https://`, a host, an optional port, and a path.
/// Refuses (`BadUrl`) user information, a fragment, a port out of range,
/// characters a URL may not hold, and anything longer than 8 KiB.
[[nodiscard]] result::Result<Url> parseUrl(std::string_view text);

/// `reference`, a redirect's Location, against `base`: an absolute URL, or
/// a path from the root.
[[nodiscard]] result::Result<Url> resolveUrl(const Url& base, std::string_view reference);

} // namespace rawframe::http
