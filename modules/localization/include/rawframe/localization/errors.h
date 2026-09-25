#pragma once

#include "rawframe/base/bits128.h"
#include "rawframe/result/error.h"

#include <cstdint>

namespace rawframe::localization {

/// The domain of every Error localization creates.
inline constexpr result::ErrorDomain kLocalizationDomain{
    base::parseBits128Hex("86259ef53831eb69fb220856483ffd3c").value};

/// Codes within kLocalizationDomain.
enum class LocalizationError : std::uint32_t {
    /// A locale tag out of SPEC-0033's grammar or canonical case.
    LocaleInvalid = 1,
    /// A tag of well-formed subtags CLDR does not know, even after its
    /// aliases.
    LocaleUnknown = 2,
    /// More than a named limit allows.
    OverLimit = 3,
};

[[nodiscard]] constexpr result::ErrorCode code(LocalizationError error) noexcept {
    return result::ErrorCode{static_cast<std::uint32_t>(error)};
}

} // namespace rawframe::localization
