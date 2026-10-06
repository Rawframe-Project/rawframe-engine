#include "rawframe/http/url.h"

#include "rawframe/http/errors.h"

#include <algorithm>
#include <cstddef>

namespace rawframe::http {

namespace {

constexpr std::size_t kMaximumUrlBytes = 8 * 1024;

std::unexpected<result::Error> badUrl(std::string_view why, std::string_view text) {
    return std::unexpected<result::Error>{
        result::fail(result::ErrorClass::InvalidArgument, kHttpDomain, code(HttpError::BadUrl), why)
            .error()
            .withContext("url", std::string{text.substr(0, 256)})};
}

bool alphanumeric(char letter) noexcept {
    return (letter >= 'a' && letter <= 'z') || (letter >= 'A' && letter <= 'Z') || (letter >= '0' && letter <= '9');
}

bool hexadecimal(char letter) noexcept {
    return (letter >= '0' && letter <= '9') || (letter >= 'a' && letter <= 'f') || (letter >= 'A' && letter <= 'F');
}

/// A path and query's characters (RFC 3986: unreserved, reserved but for
/// `#`, `[` and `]`, and percent escapes whole).
bool targetOk(std::string_view target) noexcept {
    constexpr std::string_view kAllowed = "-._~!$&'()*+,;=:@/?";
    for (std::size_t at = 0; at < target.size(); ++at) {
        const char kLetter = target[at];
        if (kLetter == '%') {
            if (at + 2 >= target.size() || !hexadecimal(target[at + 1]) || !hexadecimal(target[at + 2])) {
                return false;
            }
            at += 2;
        } else if (!alphanumeric(kLetter) && kAllowed.find(kLetter) == std::string_view::npos) {
            return false;
        }
    }
    return true;
}

bool nameOk(std::string_view host) noexcept {
    return !host.empty() && host.size() <= 253 && std::ranges::all_of(host, [](char letter) {
        return alphanumeric(letter) || letter == '-' || letter == '.';
    });
}

bool addressSixOk(std::string_view host) noexcept {
    return host.size() >= 2 && std::ranges::all_of(host, [](char letter) {
               return hexadecimal(letter) || letter == ':' || letter == '.';
           });
}

} // namespace

std::string Url::authority() const {
    std::string text = host.find(':') == std::string::npos ? host : "[" + host + "]";
    if (port != (secure ? 443 : 80)) {
        text += ":" + std::to_string(port);
    }
    return text;
}

std::string Url::text() const {
    return (secure ? "https://" : "http://") + authority() + target;
}

result::Result<Url> parseUrl(std::string_view text) {
    if (text.size() > kMaximumUrlBytes) {
        return badUrl("a URL is at most 8 KiB", text);
    }
    Url url;
    std::string_view rest;
    if (text.starts_with("https://")) {
        rest = text.substr(8);
    } else if (text.starts_with("http://")) {
        url.secure = false;
        url.port = 80;
        rest = text.substr(7);
    } else {
        return badUrl("a URL is http:// or https://", text);
    }
    if (rest.find('#') != std::string_view::npos) {
        return badUrl("a URL to fetch has no fragment", text);
    }
    const std::size_t kPath = rest.find_first_of("/?");
    const std::string_view kAuthority = rest.substr(0, kPath);
    url.target = kPath == std::string_view::npos ? "/" : std::string{rest.substr(kPath)};
    if (url.target.front() == '?') {
        url.target.insert(0, "/");
    }
    if (kAuthority.find('@') != std::string_view::npos) {
        return badUrl("a URL to fetch carries no user information", text);
    }
    std::string_view host = kAuthority;
    std::string_view port;
    bool ported = false;
    if (kAuthority.starts_with('[')) {
        const std::size_t kClose = kAuthority.find(']');
        if (kClose == std::string_view::npos) {
            return badUrl("an IPv6 host is closed by ]", text);
        }
        host = kAuthority.substr(1, kClose - 1);
        const std::string_view kAfter = kAuthority.substr(kClose + 1);
        if (!kAfter.empty() && !kAfter.starts_with(':')) {
            return badUrl("only a port follows an IPv6 host", text);
        }
        ported = !kAfter.empty();
        port = ported ? kAfter.substr(1) : kAfter;
        if (!addressSixOk(host)) {
            return badUrl("an IPv6 host is hexadecimal digits, colons, and dots", text);
        }
    } else {
        const std::size_t kColon = kAuthority.find(':');
        if (kColon != std::string_view::npos) {
            ported = true;
            host = kAuthority.substr(0, kColon);
            port = kAuthority.substr(kColon + 1);
        }
        if (!nameOk(host)) {
            return badUrl("a host is letters, digits, hyphens, and dots", text);
        }
    }
    if (ported) {
        std::uint32_t number = 0;
        if (port.empty() || port.size() > 5 || !std::ranges::all_of(port, [](char letter) {
                return letter >= '0' && letter <= '9';
            })) {
            return badUrl("a port is a number", text);
        }
        for (const char kDigit : port) {
            number = (number * 10) + static_cast<std::uint32_t>(kDigit - '0');
        }
        if (number < 1 || number > 65535) {
            return badUrl("a port is 1 to 65535", text);
        }
        url.port = static_cast<std::uint16_t>(number);
    }
    if (!targetOk(url.target)) {
        return badUrl("a path holds only the characters a URL may", text);
    }
    // Names are compared without case: kept lower.
    url.host = std::string{host};
    for (char& letter : url.host) {
        if (letter >= 'A' && letter <= 'Z') {
            letter = static_cast<char>(letter - 'A' + 'a');
        }
    }
    return url;
}

result::Result<Url> resolveUrl(const Url& base, std::string_view reference) {
    if (reference.starts_with("http://") || reference.starts_with("https://")) {
        return parseUrl(reference);
    }
    if (!reference.starts_with('/') || reference.starts_with("//")) {
        return badUrl("a redirect is to a URL or a path from the root", reference);
    }
    Url url = base;
    url.target = std::string{reference};
    if (url.target.find('#') != std::string::npos || url.target.size() > kMaximumUrlBytes || !targetOk(url.target)) {
        return badUrl("a path holds only the characters a URL may", reference);
    }
    return url;
}

} // namespace rawframe::http
