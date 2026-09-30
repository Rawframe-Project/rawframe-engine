#include "rawframe/render_scene/scene.h"

#include <algorithm>
#include <cmath>

namespace rawframe::render_scene {

SceneOcclusion occlusionOf(const std::optional<AmbientOcclusion>& asked) noexcept {
    if (!asked.has_value() || !std::isfinite(asked->radius) || !std::isfinite(asked->intensity) || asked->radius <= 0 ||
        asked->intensity <= 0) {
        return {};
    }
    return SceneOcclusion{.enabled = true,
                          .radius = std::clamp(asked->radius, 0.01F, 10.0F),
                          .intensity = std::min(asked->intensity, 4.0F)};
}

SceneBloom bloomOf(const std::optional<Bloom>& asked) noexcept {
    if (!asked.has_value() || !std::isfinite(asked->intensity) || asked->intensity <= 0) {
        return {};
    }
    return SceneBloom{.enabled = true, .intensity = std::min(asked->intensity, 1.0F)};
}

} // namespace rawframe::render_scene
