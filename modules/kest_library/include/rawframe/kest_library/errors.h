#pragma once

#include "rawframe/base/bits128.h"
#include "rawframe/result/error.h"

#include <cstdint>

namespace rawframe::kest_library {

/// The domain of every Error this module creates but a compile's.
inline constexpr result::ErrorDomain kKestLibraryDomain{
    base::parseBits128Hex("d3b972e07ae41a17555f1bba1e607d62").value};

/// Codes within kKestLibraryDomain.
enum class KestLibraryError : std::uint32_t {
    /// A game's sources resource that is not one.
    SourcesInvalid = 1,
};

[[nodiscard]] constexpr result::ErrorCode code(KestLibraryError error) noexcept {
    return result::ErrorCode{static_cast<std::uint32_t>(error)};
}

} // namespace rawframe::kest_library
