#pragma once

#include "rawframe/mesh/mesh.h"
#include "rawframe/render_scene/scene.h"

#include <array>
#include <vector>

namespace rawframe::render_scene_gpu {

/// One column-major matrix, as a cascade's view is written.
using Matrix4 = std::array<float, 16>;

/// A vertex as the scene pipeline reads it: its position, then its normal.
constexpr std::uint32_t kVertexBytes = 24;

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
    /// The view and projection without the jitter, and the frame before's
    /// taking this frame's places (D291).
    Matrix4 unjittered{};
    Matrix4 previous{};
};
static_assert(sizeof(FrameBlock) == 608, "the scene's shaders read the frame as 608 bytes");

/// A point or spot light as the scene's shaders read it (std430, D290).
struct LightBlock {
    std::array<float, 4> placeRange{};
    std::array<float, 4> intensity{};
    std::array<float, 4> direction{};
    std::array<float, 4> cone{};
    /// Its first square of the shadows' atlas and how many (D292).
    std::array<float, 4> shadow{};
};
static_assert(sizeof(LightBlock) == 80, "the scene's shaders read a light as 80 bytes");

/// A square of the punctual shadows' atlas as the scene's shaders read it
/// (std430, D292): where it lies and its side as fractions of the atlas,
/// and its near plane; the light's axes, with how wide it sees; and the
/// light's place, with a texel's width a meter ahead.
struct SlotBlock {
    std::array<float, 4> rect{};
    std::array<float, 4> right{};
    std::array<float, 4> up{};
    std::array<float, 4> forward{};
    std::array<float, 4> position{};
};
static_assert(sizeof(SlotBlock) == 80, "the scene's shaders read a shadow square as 80 bytes");

/// What the temporal pass reads (D291): whether the picture before may be
/// reused.
struct TemporalBlock {
    std::array<float, 4> state{};
};

/// The frame's view and light as the scene's shaders read them, for a
/// target of `width` by `height`: the jitter moves the picture by its
/// fraction of a pixel right and down.
FrameBlock blockOf(const render_scene::SceneFrame& frame, std::uint32_t width, std::uint32_t height) noexcept;

/// The frame's lights as the shaders read them, never none: a buffer bound
/// is never empty.
std::vector<LightBlock> lightsOf(const render_scene::SceneFrame& frame);

/// The frame's squares of the punctual shadows' atlas as the shaders read
/// them, never none.
std::vector<SlotBlock> slotsOf(const render_scene::SceneFrame& frame);

/// A mesh's vertices as the pipeline reads them, its normals made from its
/// faces where it has none.
std::vector<float> verticesOf(const mesh::Mesh& made);

/// The bytes a mesh takes on the device: its vertices and indices.
std::uint64_t bytesOf(const mesh::Mesh& made) noexcept;

} // namespace rawframe::render_scene_gpu
