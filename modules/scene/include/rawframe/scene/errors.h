#pragma once

#include "rawframe/base/bits128.h"
#include "rawframe/result/error.h"

#include <cstdint>

namespace rawframe::scene {

/// The domain of every Error this module creates.
inline constexpr result::ErrorDomain kSceneDomain{base::parseBits128Hex("8fa78e742e995cd353464bd28abd5065").value};

/// Codes within kSceneDomain.
enum class SceneError : std::uint32_t {
    /// A scene document that is not one, or not in its one form.
    SceneInvalid = 1,
    /// An instance that does not fit its source: an entity not mapped or
    /// mapped that the source lacks, an override of a component the entity
    /// has not (or adds one it has), a cycle, or two layouts of a component.
    InstanceInvalid = 2,
};

[[nodiscard]] constexpr result::ErrorCode code(SceneError error) noexcept {
    return result::ErrorCode{static_cast<std::uint32_t>(error)};
}

} // namespace rawframe::scene
