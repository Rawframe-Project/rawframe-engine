#include "blocks.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <utility>

namespace rawframe::render_scene_gpu {

namespace {

/// The light an EV100 exposes to one (ADR-0047): a sensor's saturation at
/// 1.2 times two to the EV100.
float exposureOf(float ev100) noexcept {
    return 1.0F / (1.2F * std::exp2(ev100));
}

} // namespace

FrameBlock blockOf(const render_scene::SceneFrame& frame, std::uint32_t width, std::uint32_t height) noexcept {
    FrameBlock block;
    // The projection times the view, column-major; jittered, a point ahead
    // moves right by twice the jitter over the width in clip space, and
    // down likewise, as the distance ahead divides it. A frame not
    // antialiased over time is never jittered.
    render_scene::Matrix jittered = frame.projection;
    if (frame.temporal.enabled) {
        jittered[8] -= 2 * frame.temporal.jitter[0] / static_cast<float>(std::max<std::uint32_t>(width, 1));
        jittered[9] += 2 * frame.temporal.jitter[1] / static_cast<float>(std::max<std::uint32_t>(height, 1));
    }
    for (const auto& [kProjection, kInto] :
         {std::pair{static_cast<const render_scene::Matrix*>(&jittered), &block.viewProjection},
          std::pair{&frame.projection, &block.unjittered}}) {
        for (std::size_t column = 0; column < 4; ++column) {
            for (std::size_t row = 0; row < 4; ++row) {
                float sum = 0;
                for (std::size_t k = 0; k < 4; ++k) {
                    sum += (*kProjection)[(k * 4) + row] * frame.view[(column * 4) + k];
                }
                (*kInto)[(column * 4) + row] = sum;
            }
        }
    }
    block.previous = frame.temporal.enabled ? frame.temporal.previousViewProjection : block.unjittered;
    const render_scene::SceneLights& kLights = frame.lights;
    block.toSun = {kLights.toSun[0], kLights.toSun[1], kLights.toSun[2], 0};
    block.sun = {kLights.sun[0], kLights.sun[1], kLights.sun[2], 0};
    block.sky = {kLights.sky[0], kLights.sky[1], kLights.sky[2], 0};
    block.exposure = {exposureOf(frame.exposure), 0, 0, 0};
    block.forward = {frame.forward[0], frame.forward[1], frame.forward[2], 0};
    const render_scene::SceneShadows& kShadows = frame.shadows;
    for (std::size_t at = 0; at < kShadows.count; ++at) {
        block.cascadeFar[at] = kShadows.cascades[at].far;
        block.cascadeTexel[at] = kShadows.cascades[at].texel;
        block.cascades[at] = kShadows.cascades[at].viewProjection;
    }
    block.shadow = {static_cast<float>(kShadows.count),
                    kShadows.distance,
                    static_cast<float>(std::max<std::uint32_t>(kShadows.side, 1)),
                    0};
    const render_scene::SceneClusters& kClusters = frame.clusters;
    const bool kClustered =
        !frame.lights3d.empty() && kClusters.near > 0 && kClusters.far > kClusters.near &&
        kClusters.ranges.size() == std::size_t{kClusters.tilesX} * kClusters.tilesY * kClusters.slices * 2;
    block.clusterGrid = {static_cast<float>(kClusters.tilesX),
                         static_cast<float>(kClusters.tilesY),
                         static_cast<float>(kClusters.slices),
                         kClustered ? static_cast<float>(frame.lights3d.size()) : 0};
    block.clusterDepth = {kClusters.near,
                          kClustered ? static_cast<float>(kClusters.slices) / std::log(kClusters.far / kClusters.near)
                                     : 0,
                          0,
                          0};
    return block;
}

std::vector<LightBlock> lightsOf(const render_scene::SceneFrame& frame) {
    std::vector<LightBlock> made;
    for (const render_scene::SceneLight& light : frame.lights3d) {
        made.push_back(
            {.placeRange = {light.position[0], light.position[1], light.position[2], light.range},
             .intensity = {light.intensity[0], light.intensity[1], light.intensity[2], light.spot ? 1.0F : 0.0F},
             .direction = {light.direction[0], light.direction[1], light.direction[2], 0},
             .cone = {light.cosInner, light.cosOuter, 0, 0}});
    }
    if (made.empty()) {
        made.emplace_back();
    }
    return made;
}

} // namespace rawframe::render_scene_gpu
