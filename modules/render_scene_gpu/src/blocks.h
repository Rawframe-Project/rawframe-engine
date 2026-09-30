#pragma once

#include "rawframe/render_scene/scene.h"

#include <array>
#include <vector>

namespace rawframe::render_scene_gpu {

/// One column-major matrix, as a cascade's view is written.
using Matrix4 = std::array<float, 16>;

/// The frame's view and light as the scene's shaders read them (std140).
struct FrameBlock {
    std::array<float, 16> viewProjection{};
    std::array<float, 4> toSun{};
    std::array<float, 4> sun{};
    std::array<float, 4> sky{};
    std::array<float, 4> exposure{};
    /// The eye's forward; each cascade's far end and texel; the cascades,
    /// the shadows' distance, and a cascade's side (D289).
    std::array<float, 4> forward{};
    std::array<float, 4> cascadeFar{};
    std::array<float, 4> cascadeTexel{};
    std::array<float, 4> shadow{};
    std::array<Matrix4, 4> cascades{};
    /// The clusters' tiles across and down, their slices, and the lights;
    /// their near end, and the slices over the log of their far over near
    /// (D290).
    std::array<float, 4> clusterGrid{};
    std::array<float, 4> clusterDepth{};
};
static_assert(sizeof(FrameBlock) == 480, "the scene's shaders read the frame as 480 bytes");

/// A point or spot light as the scene's shaders read it (std430, D290).
struct LightBlock {
    std::array<float, 4> placeRange{};
    std::array<float, 4> intensity{};
    std::array<float, 4> direction{};
    std::array<float, 4> cone{};
};
static_assert(sizeof(LightBlock) == 64, "the scene's shaders read a light as 64 bytes");

/// The frame's view and light as the scene's shaders read them.
FrameBlock blockOf(const render_scene::SceneFrame& frame) noexcept;

/// The frame's lights as the shaders read them, never none: a buffer bound
/// is never empty.
std::vector<LightBlock> lightsOf(const render_scene::SceneFrame& frame);

} // namespace rawframe::render_scene_gpu
