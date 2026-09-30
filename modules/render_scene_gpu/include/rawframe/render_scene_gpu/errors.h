#pragma once

#include "rawframe/base/bits128.h"
#include "rawframe/result/error.h"

#include <cstdint>

namespace rawframe::render_scene_gpu {

/// The domain of every Error this module creates (SPEC-0024's scene).
inline constexpr result::ErrorDomain kSceneGpuDomain{base::parseBits128Hex("4189bab432dfa0c7b29b738323e217ee").value};

enum class SceneGpuError : std::uint32_t {
    /// The device refused or failed work: a shader, a pipeline, a buffer, a
    /// frame.
    Device = 1,
    /// A named limit was passed: a target's sides.
    OverLimit = 2,
    /// Asked of a device that is not ready, or was lost.
    State = 3,
};

[[nodiscard]] constexpr result::ErrorCode code(SceneGpuError error) noexcept {
    return result::ErrorCode{static_cast<std::uint32_t>(error)};
}

} // namespace rawframe::render_scene_gpu
