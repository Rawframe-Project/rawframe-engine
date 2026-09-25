#pragma once

#include "rawframe/base/bits128.h"
#include "rawframe/result/error.h"

#include <cstdint>

namespace rawframe::animation {

/// The domain of every Error this module creates.
inline constexpr result::ErrorDomain kAnimationDomain{base::parseBits128Hex("9bdeec4efa9a3ad9961ce9628252c309").value};

/// Codes within kAnimationDomain.
enum class AnimationError : std::uint32_t {
    /// A skeleton document that is not one, or not in its one form: no
    /// single root, a parent after its child, a target twice, a bind pose
    /// that is not finite or a rotation that is not unit.
    SkeletonInvalid = 1,
    /// A clip document that is not one, or not in its one form: keys out of
    /// order or outside the duration, a binding twice, an interpolation its
    /// channel does not allow.
    ClipInvalid = 2,
    /// A clip that does not fit the skeleton it is bound to: another
    /// skeleton, or a bone the skeleton lacks.
    BindingInvalid = 3,
    /// More bones, tracks, keys, or events than the limits allow.
    OverLimit = 4,
};

[[nodiscard]] constexpr result::ErrorCode code(AnimationError error) noexcept {
    return result::ErrorCode{static_cast<std::uint32_t>(error)};
}

} // namespace rawframe::animation
