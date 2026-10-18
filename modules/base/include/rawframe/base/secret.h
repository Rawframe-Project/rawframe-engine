#pragma once

#include <cstddef>
#include <string_view>

namespace rawframe::base {

/// Whether a secret given is the one expected, in a time that depends on
/// their lengths only, so a token is not guessed a byte at a time (D408,
/// D881).
[[nodiscard]] inline bool sameSecret(std::string_view given, std::string_view expected) noexcept {
    unsigned difference = given.size() == expected.size() ? 0U : 1U;
    for (std::size_t at = 0; at < given.size(); ++at) {
        const char kExpected = expected.empty() ? '\0' : expected[at % expected.size()];
        difference |= static_cast<unsigned char>(given[at]) ^ static_cast<unsigned char>(kExpected);
    }
    return difference == 0;
}

} // namespace rawframe::base
