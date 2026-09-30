#pragma once

#include "rawframe/base/bits128.h"
#include "rawframe/result/error.h"

#include <cstdint>

namespace rawframe::material {

/// The domain of every Error this module creates.
inline constexpr result::ErrorDomain kMaterialDomain{base::parseBits128Hex("52c229ebb235826639c4a8631b18475c").value};

/// Codes within kMaterialDomain.
enum class MaterialError : std::uint32_t {
    /// Not a surface material, or not in its one form: not one surface
    /// node, an input the Surface contract lacks or of the wrong type, a
    /// value out of its range, a state out of its set, a default written.
    Invalid = 1,
    /// A material this engine cannot compile yet: a node of a type it does
    /// not know (kept whole), or an input connected, before the standard
    /// node library exists.
    Unsupported = 2,
    /// More nodes, inputs, or bytes than the limits allow.
    OverLimit = 3,
};

[[nodiscard]] constexpr result::ErrorCode code(MaterialError error) noexcept {
    return result::ErrorCode{static_cast<std::uint32_t>(error)};
}

} // namespace rawframe::material
