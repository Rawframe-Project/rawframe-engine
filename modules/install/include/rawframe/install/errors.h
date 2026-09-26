#pragma once

#include "rawframe/base/bits128.h"
#include "rawframe/result/error.h"

#include <cstdint>

namespace rawframe::install {

/// The domain of every Error this module creates. What a Build, a manifest,
/// a signature, or a record refuses is refused in its own module's domain.
inline constexpr result::ErrorDomain kInstallDomain{base::parseBits128Hex("316f1a9ad380d6ce1fbc3f705ba566de").value};

enum class InstallError : std::uint32_t {
    /// The origin could not serve what was asked, or served more than its
    /// ceiling.
    FetchFailed = 1,
    /// The origin served bytes that are not what the manifest says.
    FetchedWrong = 2,
    /// The library could not be written.
    WriteFailed = 3,
    /// An update would fetch more than its limit.
    OverLimit = 4,
    /// Nothing is retained to roll back to, or what is retained is not whole.
    NothingToRollBack = 5,
    /// The library's own records (the installed pointer, a kept
    /// Composition) are not what this module writes.
    LibraryInvalid = 6,
};

[[nodiscard]] constexpr result::ErrorCode code(InstallError error) noexcept {
    return result::ErrorCode{static_cast<std::uint32_t>(error)};
}

} // namespace rawframe::install
