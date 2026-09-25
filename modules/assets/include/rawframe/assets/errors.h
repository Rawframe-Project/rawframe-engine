#pragma once

#include "rawframe/base/bits128.h"
#include "rawframe/result/error.h"

#include <cstdint>

namespace rawframe::assets {

/// The domain of every Error this module creates.
inline constexpr result::ErrorDomain kAssetsDomain{base::parseBits128Hex("0708a347c742361fdb4c4cb45bb19125").value};

/// SPEC-0027's closed failure conditions.
enum class AssetError : std::uint32_t {
    DecodeFailed = 1,
    DependencyMissing = 2,
    DependencyUnknown = 3,
    DependencyFailed = 4,
    FormUnavailable = 5,
    LimitExceeded = 6,
    DeadlineExceeded = 7,
    RevisionRetired = 8,
    ScopeClosed = 9,
    Evicted = 10,
    WrongScope = 11,
};

[[nodiscard]] constexpr result::ErrorCode code(AssetError error) noexcept {
    return result::ErrorCode{static_cast<std::uint32_t>(error)};
}

} // namespace rawframe::assets
