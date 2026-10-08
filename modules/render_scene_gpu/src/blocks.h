#pragma once

#include "rawframe/mesh/mesh.h"
#include "rawframe/render_scene/scene.h"

#include <array>
#include <span>
#include <vector>

namespace rawframe::render_scene_gpu {

/// One column-major matrix, as a cascade's view is written.
using Matrix4 = std::array<float, 16>;

/// A vertex as the scene pipeline reads it: its position, its normal, its
/// texture coordinates, nought for a mesh without them (D309), then its
/// tangent and the bitangent's sign (D313).
constexpr std::uint32_t kVertexBytes = 48;
/// A skinned vertex's influences as skinning reads them (D508): its four
/// joints two to a word, low half first, then its four weights' bits.
constexpr std::uint32_t kInfluenceWords = 6;

/// The frame's view and light as the scene's shaders read them (std140).
struct FrameBlock {
    std::array<float, 16> viewProjection{};
    std::array<float, 4> toSun{};
    /// The sun's light, and its width (D347).
    std::array<float, 4> sun{};
    std::array<float, 4> sky{};
    std::array<float, 4> exposure{};
    /// The eye's forward; each cascade's far end and texel; the cascades,
    /// the shadows' distance, a cascade's side (D289), and their filter's
    /// class (D330, D347).
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
    /// The ground's luminance below the horizon (D304).
    std::array<float, 4> ground{};
    /// The sky's picture (D322): its levels, nought for none; and the
    /// irradiance over π it gives, per unit of the sky's light, as nine
    /// spherical harmonics' coefficients.
    std::array<float, 4> environment{};
    std::array<std::array<float, 4>, 9> irradiance{};
    /// One where the view's ambient occlusion is on (D327), where its
    /// screen-space reflections are (D331), where its contact shadows are
    /// (D338), and where it draws decals (D339).
    std::array<float, 4> occlusion{};
    std::array<float, 4> reflections{};
    std::array<float, 4> contact{};
    std::array<float, 4> decals{};
};
static_assert(sizeof(FrameBlock) == 848, "the scene's shaders read the frame as 848 bytes");

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

/// The exposure the device holds (D293): its EV100 and the factor it
/// scales light by, 1 / (1.2 * 2^EV100) (ADR-0047).
struct ExposureBlock {
    std::array<float, 4> value{};
};

/// The exposure `ev100` sets, as the device holds it.
ExposureBlock exposureOf(float ev100) noexcept;

/// What the metering's steps read (D293): the bounds and rates (minimum,
/// maximum, brighten, darken); the fractions left out, the compensation,
/// and the seconds since the frame before; and whether to go at once, and
/// how much more the middle counts (D345).
struct MeterBlock {
    std::array<float, 4> bounds{};
    std::array<float, 4> fractions{};
    std::array<float, 4> snap{};
};

/// The frame's metering as its steps read it.
MeterBlock meterOf(const render_scene::SceneFrame& frame) noexcept;

/// The metering's histogram: its bins, the first for no light.
inline constexpr std::uint32_t kHistogramBins = 128;

/// What the picture's pass reads (std140): the camera's grade (D294), the
/// white balance's rows; the slope, with the saturation; the offset, with
/// the contrast; the power, with one when it grades at all; and its
/// tonemapper (D295): its number, and the factor that keeps middle grey
/// where AgX puts it.
struct PictureBlock {
    std::array<std::array<float, 4>, 3> balance{};
    std::array<float, 4> slope{};
    std::array<float, 4> offset{};
    std::array<float, 4> power{};
    std::array<float, 4> tonemapper{};
    /// The bloom's share, and one over its chain's levels (D328).
    std::array<float, 4> bloom{};
    /// One where the picture is dithered (D332); one where its colors are
    /// looked up in the grading table (D344).
    std::array<float, 4> display{};
};
static_assert(sizeof(PictureBlock) == 144, "the picture's shader reads its block as 144 bytes");

/// The frame's grade and tonemapper as the picture's pass reads them.
PictureBlock pictureOf(const render_scene::SceneFrame& frame) noexcept;

/// What the sky's pass reads (D293): its light in candela per square meter.
struct SkyBlock {
    std::array<float, 4> light{};
    /// The sky's picture's levels, nought for none (D322); where a point
    /// of the target looks, the jittered view's inverse; and the view
    /// unjittered and the frame before's, for the picture's motion.
    std::array<float, 4> environment{};
    Matrix4 toDirection{};
    Matrix4 unjittered{};
    Matrix4 previous{};
};
static_assert(sizeof(SkyBlock) == 224, "the sky's shaders read it as 224 bytes");

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
/// faces where it has none, its texture coordinates nought where it has
/// none, and its tangents made from its faces' coordinates (D313).
std::vector<float> verticesOf(const mesh::Mesh& made);

/// Each vertex's tangent, the direction its texture coordinates' u rises
/// across its normal, and the sign that makes the bitangent the normal
/// crossed with it point up the image (glTF's convention): Lengyel's
/// per-face accumulation, not MikkTSpace (D313). Any direction across the
/// normal, and a sign of one, where the mesh has no coordinates.
std::vector<std::array<float, 4>> tangentsOf(const mesh::Mesh& made, std::span<const mesh::Vector3> normals);

/// A skinned mesh's influences, kInfluenceWords a vertex; none unskinned.
std::vector<std::uint32_t> influencesOf(const mesh::Mesh& made);

/// The bytes a mesh takes on the device: its vertices and indices, and a
/// skinned one's influences.
std::uint64_t bytesOf(const mesh::Mesh& made) noexcept;

} // namespace rawframe::render_scene_gpu
