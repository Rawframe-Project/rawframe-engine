#pragma once

#include "rawframe/base/bits128.h"
#include "rawframe/result/error.h"

#include <cstdint>

namespace rawframe::render_scene {

/// The domain of every Error this module creates.
inline constexpr result::ErrorDomain kRenderSceneDomain{
    base::parseBits128Hex("3bd5a98e4d716db49772a00074bd14af").value};

/// Codes within kRenderSceneDomain.
enum class RenderSceneError : std::uint32_t {
    /// The game declares no component of `rawframe.model.Model`'s type.
    NoModels = 1,
    /// One of its `rawframe.model` components is not laid out as that
    /// module lays it out, or it declares two of a kind there is one of.
    BadComponents = 2,
    /// A material's cooked content could not be read (D303).
    MaterialUnreadable = 3,
};

[[nodiscard]] constexpr result::ErrorCode code(RenderSceneError error) noexcept {
    return result::ErrorCode{static_cast<std::uint32_t>(error)};
}

} // namespace rawframe::render_scene
