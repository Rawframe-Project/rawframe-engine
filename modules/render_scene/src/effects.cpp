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

SceneScreenReflections reflectionsOf(const std::optional<ScreenSpaceReflections>& asked) noexcept {
    if (!asked.has_value() || !std::isfinite(asked->distance) || asked->distance <= 0) {
        return {};
    }
    return SceneScreenReflections{.enabled = true, .distance = std::clamp(asked->distance, 0.1F, 100.0F)};
}

SceneMotionBlur motionBlurOf(const std::optional<MotionBlur>& asked) noexcept {
    if (!asked.has_value() || !std::isfinite(asked->shutter) || asked->shutter <= 0) {
        return {};
    }
    return SceneMotionBlur{.enabled = true, .shutter = std::min(asked->shutter, 1.0F)};
}

SceneDepthOfField depthOfFieldOf(const std::optional<DepthOfField>& asked) noexcept {
    if (!asked.has_value() || !std::isfinite(asked->focus) || asked->focus <= 0 || !std::isfinite(asked->aperture) ||
        asked->aperture <= 0) {
        return {};
    }
    return SceneDepthOfField{.enabled = true,
                             .focus = std::clamp(asked->focus, 0.1F, 10'000.0F),
                             .aperture = std::clamp(asked->aperture, 0.5F, 64.0F)};
}

SceneContactShadows contactShadowsOf(const std::optional<ContactShadows>& asked) noexcept {
    if (!asked.has_value() || !std::isfinite(asked->length) || asked->length <= 0) {
        return {};
    }
    return SceneContactShadows{.enabled = true, .length = std::clamp(asked->length, 0.01F, 10.0F)};
}

} // namespace rawframe::render_scene
