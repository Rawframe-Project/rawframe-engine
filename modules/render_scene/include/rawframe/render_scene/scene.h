#pragma once

// The 3D scene's CPU half (SPEC-0024's scene contract, D283): entities with
// `rawframe.model` models, copied out of a World where their poses put
// them, then seen through a camera, culled, and put in an order a device
// draws, lit by the sun and the sky. Client only; a server carries the same
// components as plain values and links none of this.
//
// Positions stay in doubles until the view: every instance is placed
// relative to the eye before it becomes floats (ADR-0046's large worlds).
// The projection is reversed-Z with an infinite far plane (ADR-0051): the
// near plane maps to depth one, the horizon to nought.

#include "rawframe/kest/program.h"
#include "rawframe/material/material.h"
#include "rawframe/material/post_process.h"
#include "rawframe/mesh/mesh.h"
#include "rawframe/particles/particles.h"
#include "rawframe/result/result.h"
#include "rawframe/schema/registry.h"
#include "rawframe/world/world.h"
#include "rawframe/world_kest/game_files.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <tuple>
#include <vector>

namespace rawframe::render_scene {

/// `rawframe.model.Model` as C++ reads it; checked against the program's
/// layout when a game's scene loads.
struct Model {
    std::uint64_t mesh = 0;
    float scaleX = 1;
    float scaleY = 1;
    float scaleZ = 1;
    std::uint32_t color = 0xFFFFFFFF;
    std::uint64_t material = 0;
};

/// `rawframe.model.Camera` as C++ reads it.
struct Camera {
    float offsetX = 0;
    float offsetY = 0;
    float offsetZ = 0;
    float yaw = 0;
    float pitch = 0;
    float fovY = 0;
    float near = 0;
    float exposure = 0;
    std::uint32_t tonemapper = 0;
};

/// `rawframe.model.View` as C++ reads it (ADR-0052, D361): the render
/// texture its entity's camera draws into, and its order among the views
/// naming it.
struct View {
    std::uint64_t target = 0;
    std::int32_t order = 0;
};

/// ADR-0047's closed tonemapper set, as a camera names it (D295): AgX, the
/// default; Khronos PBR Neutral; and linear, for measuring.
enum class Tonemapper : std::uint8_t {
    Agx = 0,
    PbrNeutral = 1,
    Linear = 2
};

/// `rawframe.model.AmbientOcclusion` as C++ reads it (D327).
struct AmbientOcclusion {
    float radius = 0;
    float intensity = 0;
};

/// `rawframe.model.Bloom` as C++ reads it (D328).
struct Bloom {
    float intensity = 0;
};

/// `rawframe.model.ScreenSpaceReflections` as C++ reads it (D331).
struct ScreenSpaceReflections {
    float distance = 0;
};

/// `rawframe.model.MotionBlur` as C++ reads it (D334).
struct MotionBlur {
    float shutter = 0;
};

/// `rawframe.model.DepthOfField` as C++ reads it (D336).
struct DepthOfField {
    float focus = 0;
    float aperture = 0;
};

/// `rawframe.model.ContactShadows` as C++ reads it (D338).
struct ContactShadows {
    float length = 0;
};

/// `rawframe.model.Grading` as C++ reads it.
struct Grading {
    float slopeR = 1;
    float slopeG = 1;
    float slopeB = 1;
    float offsetR = 0;
    float offsetG = 0;
    float offsetB = 0;
    float powerR = 1;
    float powerG = 1;
    float powerB = 1;
    float saturation = 1;
    float contrast = 1;
    float temperature = 0;
    float tint = 0;
    /// A grading table, a game texture; nought for none (D344).
    std::uint64_t table = 0;
};

/// `rawframe.model.AutoExposure` as C++ reads it.
struct AutoExposure {
    float minimum = 0;
    float maximum = 0;
    float brighten = 0;
    float darken = 0;
    float compensation = 0;
    float low = 0;
    float high = 0;
    /// How much more the middle of the picture counts (D345).
    float centered = 0;
};

/// `rawframe.model.Sun` as C++ reads it.
struct Sun {
    float directionX = 0;
    float directionY = -1;
    float directionZ = 0;
    float illuminance = 0;
    std::uint32_t color = 0xFFFFFFFF;
    /// The angle its disc spans, in radians (D347).
    float angle = 0;
};

/// `rawframe.model.Sky` as C++ reads it.
struct Sky {
    float luminance = 0;
    std::uint32_t color = 0xFFFFFFFF;
    std::uint32_t ground = 0;
    std::uint64_t environment = 0;
};

/// `rawframe.model.Decal` as C++ reads it (D339).
struct Decal {
    float halfX = 0;
    float halfY = 0;
    float halfZ = 0;
    std::uint32_t color = 0xFFFFFFFF;
    std::uint64_t texture = 0;
    /// The texture bending the normals it covers, nought for none; the
    /// roughness it lays, nought for the surface's (D342).
    std::uint64_t normal = 0;
    float roughness = 0;
};

/// `rawframe.model.PostProcess` as C++ reads it (D349).
struct PostProcess {
    std::uint64_t material = 0;
    float weight = 0;
};

/// `rawframe.model.ReflectionProbe` as C++ reads it (D325).
struct ReflectionProbe {
    float halfX = 0;
    float halfY = 0;
    float halfZ = 0;
    float intensity = 0;
    std::uint32_t priority = 0;
    std::uint64_t environment = 0;
};

/// `rawframe.model.PointLight` as C++ reads it.
struct PointLight {
    float lumens = 0;
    float range = 0;
    std::uint32_t color = 0xFFFFFFFF;
    /// Whether it casts shadows, within the view's budget (D292).
    bool shadows = false;
};

/// `rawframe.model.SpotLight` as C++ reads it.
struct SpotLight {
    float lumens = 0;
    float range = 0;
    float inner = 0;
    float outer = 0;
    std::uint32_t color = 0xFFFFFFFF;
    bool shadows = false;
};

/// The engine's own meshes (`rawframe.model`'s BOX, SPHERE, CYLINDER, and
/// CAPSULE), each one meter from its center to its sides.
inline constexpr std::uint64_t kBox = 0x3ff8cd61f01cf46bULL;
inline constexpr std::uint64_t kSphere = 0x2c6b82d75f7be712ULL;
inline constexpr std::uint64_t kCylinder = 0xaee26a717a3a62a0ULL;
inline constexpr std::uint64_t kCapsule = 0x63ed148327123836ULL;

/// One of the engine's own meshes, made anew; none for another identity.
[[nodiscard]] std::shared_ptr<const mesh::Mesh> engineMesh(std::uint64_t id);

/// A mesh the scene may draw, by the identity a Model names it by.
/// A compiled material as a device reads it (`material::blobOf`, D303).
using MaterialBlob = std::array<float, material::kBlobFloats>;

/// A model without a material: OpenPBR's surface but for a white base
/// color, so the model's color is its base color.
[[nodiscard]] MaterialBlob noMaterial() noexcept;

/// The texture a material samples, as a device binds it (D309): the game's
/// texture by the identity its `texture` line gives it, none when nought,
/// and the material's declared sampler state.
struct SceneTexture {
    std::uint64_t id = 0;
    material::Filter filter = material::Filter::Linear;
    material::Address address = material::Address::Repeat;

    friend bool operator==(const SceneTexture&, const SceneTexture&) = default;
};

/// A material's textures (D312, D313): its base, packed, emission, and
/// normal textures, each none when its identity is nought.
struct SceneTextures {
    SceneTexture base;
    SceneTexture packed;
    SceneTexture emission;
    SceneTexture normal;

    friend bool operator==(const SceneTextures&, const SceneTextures&) = default;
    friend auto operator<=>(const SceneTextures& left, const SceneTextures& right) noexcept {
        return std::tuple{left.base.id, left.packed.id, left.emission.id, left.normal.id} <=>
               std::tuple{right.base.id, right.packed.id, right.emission.id, right.normal.id};
    }
};

/// A game's material, by the identity its `material` line gives it.
struct SceneMaterial {
    std::uint64_t id = 0;
    MaterialBlob blob{};
    /// Whether it blends over what is behind it (SPEC-0026's `translucent`,
    /// D305).
    bool translucent = false;
    SceneTextures textures;
};

/// A game's post-process material (D348, D349): its identity, where it
/// runs, what a device reads of it, and the texture it samples.
struct ScenePostProcessMaterial {
    std::uint64_t id = 0;
    material::Insertion insertion = material::Insertion::AfterTonemap;
    std::array<float, material::kPostProcessBlobFloats> blob{};
    SceneTexture texture;
};

/// A post process a frame runs (D349): where, what the device reads of
/// its material, its texture, and how much of it, above nought and at
/// most one.
struct ScenePostProcess {
    material::Insertion insertion = material::Insertion::AfterTonemap;
    std::array<float, material::kPostProcessBlobFloats> blob{};
    SceneTexture texture;
    float weight = 1;
};

struct SceneMesh {
    std::uint64_t id = 0;
    std::shared_ptr<const mesh::Mesh> mesh;
};

/// A model as the extract stage copies it out of the World: owned values,
/// placed where its entity's `rawframe.physics3d.pose` puts it (the origin,
/// unturned, without one).
struct ModelInstance {
    world::EntityHandle entity;
    /// Which of the game's model components it is, by its place.
    std::uint32_t component = 0;
    Model model;
    std::array<double, 3> position{};
    /// The pose's turn as a unit quaternion, x, y, z, w.
    std::array<float, 4> rotation{0, 0, 0, 1};
};

/// The view as the queue stage takes it: the eye's place, its direction
/// and lens, and the view's aspect.
struct SceneCamera {
    std::array<double, 3> eye{0, 0, 0};
    /// Radians about +Y (nought looks along -Z), and above the ground.
    float yaw = 0;
    float pitch = 0;
    float fovY = 1;
    float near = 0.1F;
    /// EV100 (ADR-0047).
    float exposure = 15;
    float aspect = 16.0F / 9.0F;
    /// The exposure metered from what the view sees, starting from
    /// `exposure`, if the camera asks (D293); and the seconds since the
    /// frame before, on the Host's timeline.
    std::optional<AutoExposure> metering;
    /// The camera's grading, if it asks (D294).
    std::optional<Grading> grading;
    /// The camera's ambient occlusion, if it asks (D327).
    std::optional<AmbientOcclusion> occlusion;
    /// The camera's bloom, if it asks (D328).
    std::optional<Bloom> bloom;
    /// The camera's screen-space reflections, if it asks (D331).
    std::optional<ScreenSpaceReflections> reflections;
    /// The camera's motion blur, if it asks (D334).
    std::optional<MotionBlur> motionBlur;
    /// The camera's depth of field, if it asks (D336).
    std::optional<DepthOfField> depthOfField;
    /// The camera's contact shadows, if it asks (D338).
    std::optional<ContactShadows> contactShadows;
    /// The camera's tonemapper, as its component numbers it: another
    /// number is AgX (D295).
    std::uint32_t tonemapper = 0;
    /// The camera's post processes, in the game's order (D349).
    std::vector<PostProcess> postProcesses;
    float elapsed = 0;
};

/// Column-major, as shaders read them.
using Matrix = std::array<float, 16>;

/// One model to draw, or a run of its mesh's parts that draw with one
/// material (D314): its mesh and the run's indices, where it is relative to
/// the eye (the World's axes, meters), and its surface's base color in
/// linear light.
struct SceneDraw {
    std::uint64_t mesh = 0;
    std::uint32_t firstIndex = 0;
    /// Nought for every index from the first.
    std::uint32_t indexCount = 0;
    /// Model space to eye-relative World space: turn, scale, and place.
    Matrix model{};
    /// Normals' turn: the inverse transpose of the model's upper 3 by 3,
    /// columns of four with the last nought.
    Matrix normal{};
    std::array<float, 4> color{1, 1, 1, 1};
    /// Its material's place in the frame's materials (D303): the Model's
    /// when it names one, else its parts' own (D314).
    std::uint32_t material = 0;
    world::EntityHandle entity;
    /// Where the model was the frame before, relative to this frame's eye:
    /// what its motion is measured from (D291); the model itself where it
    /// was not drawn then.
    Matrix previous{};
};

/// The light a frame is drawn in, linear Rec. 709 (ADR-0047) in physical
/// units (ADR-0051): the sun's illuminance in lux and the direction toward
/// it, and the sky's luminance in candela per square meter.
struct SceneLights {
    std::array<float, 3> toSun{0, 1, 0};
    std::array<float, 3> sun{0, 0, 0};
    std::array<float, 3> sky{0, 0, 0};
    /// The sun's width (D347): twice the tangent of half the angle its disc
    /// spans, how far its penumbra spreads for each meter between a caster
    /// and what it shades.
    float sunWidth = 0;
    /// The ground's luminance below the horizon (D304): its albedo times
    /// the sun's illuminance on it and the sky's, over π.
    std::array<float, 3> ground{0, 0, 0};
    /// The sky's picture (D322): the texture the game names, a cube whose
    /// light `sky` scales, in place of the sky's and the ground's; nought
    /// for none.
    std::uint64_t environment = 0;
};

/// A punctual light as the extract stage copies it out of the World, where
/// its entity's pose puts it (D290): a point's or a spot's.
struct LightInstance {
    world::EntityHandle entity;
    bool spot = false;
    SpotLight light;
    std::array<double, 3> position{};
    std::array<float, 4> rotation{0, 0, 0, 1};
};

/// A reflection probe as the extract stage copies it out of the World,
/// where its entity's pose puts it (D325).
struct ProbeInstance {
    world::EntityHandle entity;
    ReflectionProbe probe;
    std::array<double, 3> position{};
};

/// A decal as the extract stage copies it out of the World, where its
/// entity's pose puts it (D339).
struct DecalInstance {
    world::EntityHandle entity;
    Decal decal;
    std::array<double, 3> position{};
    std::array<float, 4> rotation{0, 0, 0, 1};
};

/// A decal as a device reads it (D339): the eye-relative World into its
/// box, which spans -1 to 1 along each axis and is seen along -Z, x to the
/// texture's right and y to its top; its tint in linear light, with how
/// much it covers; its texture; and its normal texture and the roughness
/// it lays, nought for none (D342).
struct SceneDecal {
    Matrix toBox{};
    std::array<float, 4> color{};
    std::uint64_t texture = 0;
    std::uint64_t normal = 0;
    float roughness = 0;
};

/// A reflection probe as a device reads it (D325): its box's middle
/// relative to the eye, its half sides, what it holds, and its light's
/// scale.
struct SceneProbe {
    std::array<float, 3> position{};
    std::array<float, 3> half{};
    std::uint64_t environment = 0;
    float intensity = 0;
};

/// A punctual light as a device reads it (D290): where it is relative to
/// the eye, its reach, its intensity (linear color times candela), and, for
/// a spot, the way it shines and the cosines of its cone.
struct SceneLight {
    std::array<float, 3> position{};
    float range = 0;
    std::array<float, 3> intensity{};
    bool spot = false;
    std::array<float, 3> direction{0, 0, -1};
    /// Nought and minus one for a point: every way is inside.
    float cosInner = -1;
    float cosOuter = -1;
    /// Its first square in the punctual shadows' atlas, and how many: one
    /// for a spot, six for a point; none without shadows (D292).
    std::uint32_t shadowSlot = 0;
    std::uint32_t shadowSlots = 0;
};

/// One square of the punctual lights' shadow atlas (D292): a spot's map or
/// one face of a point's cube, seen from the light along `forward` through
/// a perspective `tangent` wide each way (reversed-Z with no far plane,
/// depth one at `near`), placed relative to the eye; where it lies in the
/// atlas, in texels; and its casters, a run of `SceneLightShadows::casters`.
struct ShadowSlot {
    Matrix viewProjection{};
    std::array<float, 3> position{};
    std::array<float, 3> right{};
    std::array<float, 3> up{};
    std::array<float, 3> forward{};
    float tangent = 1;
    float near = 0.05F;
    std::uint32_t x = 0;
    std::uint32_t y = 0;
    std::uint32_t side = 0;
    std::uint32_t firstCaster = 0;
    std::uint32_t casterCount = 0;
};

/// A punctual light's square of the shadows' atlas as seen from it, not yet
/// placed in the atlas nor given casters (D292): a spot's along its
/// direction, as wide as its cone; a point's `face` along +X, -X, +Y, -Y,
/// +Z, or -Z, a quarter turn wide.
[[nodiscard]] ShadowSlot shadowSlotOf(const SceneLight& light, std::uint32_t face) noexcept;

/// The punctual lights' shadows for a frame (D292): the atlas's side (none
/// without a shadowed light), its squares, and their casters; the lights
/// that asked for shadows and got none for the budget, and casters past
/// the models' limit.
struct SceneLightShadows {
    std::uint32_t side = 0;
    std::vector<ShadowSlot> slots;
    std::vector<SceneDraw> casters;
    std::size_t evicted = 0;
    std::size_t overLimit = 0;
};

/// ADR-0051's one clustered structure for the view (D290): screen tiles by
/// exponential depth slices, each cluster naming the lights that reach it.
/// Built by the view stage; a device reads a cluster's lights by where a
/// point is on the screen and how far ahead.
struct SceneClusters {
    std::uint32_t tilesX = 16;
    std::uint32_t tilesY = 9;
    std::uint32_t slices = 24;
    /// The depths the slices divide, exponentially: nearer than `near` is
    /// the first slice, farther than `far` the last.
    float near = 0.1F;
    float far = 500;
    /// Each cluster's first index in `indices`, how many lights follow it,
    /// how many decals follow those (D339), and how many reflection probes
    /// follow those (D340); x fastest, then y from the top, then slices
    /// from the eye.
    std::vector<std::uint32_t> ranges;
    /// Lights by their place in the frame's lights, decals by theirs in
    /// the frame's decals, and probes by theirs in the frame's probes.
    std::vector<std::uint32_t> indices;
};

/// One cascade of the sun's shadow map (ADR-0051): a light-space box
/// holding the part of the view from the cascade before it to `far`.
struct ShadowCascade {
    /// Eye-relative World space to the cascade's clip space: x and y across
    /// its square, reversed-Z depth, one toward the sun.
    Matrix viewProjection{};
    /// Where along the view it ends, in meters from the eye.
    float far = 0;
    /// The World size of one of its texels, for the normal bias.
    float texel = 0;
    /// Its casters, a run of `SceneShadows::casters` (D298).
    std::uint32_t firstCaster = 0;
    std::uint32_t casterCount = 0;
};

/// ADR-0051's typed ladder of shadow filters (D330): the hardware's two by
/// two blend of four compared texels, its base; a five by five
/// tent-weighted blend from nine of those (Castaño's optimized PCF), the
/// middle class; and contact hardening (PCSS), the top class (D347): the
/// sun's shadows sharp where they meet their casters and wider with the
/// distance from them, as wide as the sun's disc makes them. A point or
/// spot light has no size, so its shadows are filtered by the middle class
/// in the top class.
enum class ShadowFilter : std::uint8_t {
    Hardware,
    Soft,
    ContactHardening
};

/// The sun's shadows as the view stage derives them (ADR-0051's cascaded
/// shadow maps): up to four cascades, nearest first, and the models that
/// cast into each, cascade by cascade, in draw order (D298). None without
/// a sun or shadows.
struct SceneShadows {
    std::size_t count = 0;
    std::array<ShadowCascade, 4> cascades{};
    /// Each cascade's square's side in texels.
    std::uint32_t side = 0;
    /// Where shadows end, in meters from the eye; they fade over the last
    /// cascade's last tenth.
    float distance = 0;
    /// How every shadow map is filtered, the punctual lights' too (D330).
    ShadowFilter filter = ShadowFilter::Soft;
    std::vector<SceneDraw> casters;
    /// Casters left out past the models' limit, once for each cascade.
    std::size_t overLimit = 0;
};

/// What the queue stage builds: the view, the light, and the draws in the
/// order a device draws them, grouped by mesh; and what was left out.
/// ADR-0051's metering (D293): whether the frame's exposure follows what it
/// sees; its bounds, rates, compensation, and the fractions of its pixels
/// left out, made sound (finite, the maximum above the minimum, the rates
/// not negative, the fractions within nought and one, low below high); the
/// seconds since the frame before, at most a quarter; and whether the
/// exposure goes at once to what it measures, not at the rates: when the
/// metering starts, or the eye cuts away.
struct SceneMetering {
    bool enabled = false;
    AutoExposure settings;
    float elapsed = 0;
    bool snap = false;
};

/// ADR-0051's grading form (D294), as the display stage applies it to the
/// scene's linear light before the tonemapper: whether it grades; the white
/// balance as one matrix of linear Rec. 709 (rows of three, the camera's
/// temperature and tint taken through LMS); the ASC CDL slope, offset, and
/// power; saturation; contrast about middle grey; and the grading table
/// looked up last, nought for none (D344). A grade with a value not finite
/// or a power not above nought grades nothing.
struct SceneGrading {
    bool enabled = false;
    std::array<float, 9> balance{1, 0, 0, 0, 1, 0, 0, 0, 1};
    std::array<float, 3> slope{1, 1, 1};
    std::array<float, 3> offset{0, 0, 0};
    std::array<float, 3> power{1, 1, 1};
    float saturation = 1;
    float contrast = 1;
    std::uint64_t table = 0;
};

/// The grading a camera's `Grading` asks for, made sound (D294).
[[nodiscard]] SceneGrading gradingOf(const std::optional<Grading>& asked) noexcept;

/// ADR-0051's screen-space ambient occlusion (D327): whether the view has
/// it, how far around a point it looks, in meters (a hundredth to ten),
/// and how strongly it takes what it finds (up to four).
struct SceneOcclusion {
    bool enabled = false;
    float radius = 0;
    float intensity = 0;
};

/// The occlusion a camera's `AmbientOcclusion` asks for, made sound
/// (D327): none for a value not finite, or a radius or an intensity not
/// above nought.
[[nodiscard]] SceneOcclusion occlusionOf(const std::optional<AmbientOcclusion>& asked) noexcept;

/// ADR-0051's bloom (D328): whether the view has it, and how much of the
/// spread light is mixed with the light as it is (up to one).
struct SceneBloom {
    bool enabled = false;
    float intensity = 0;
};

/// The bloom a camera's `Bloom` asks for, made sound (D328): none for an
/// intensity not finite or not above nought.
[[nodiscard]] SceneBloom bloomOf(const std::optional<Bloom>& asked) noexcept;

/// ADR-0051's screen-space reflections (D331): whether the view has them,
/// and how far a reflection is followed, in meters (a tenth to a hundred).
struct SceneScreenReflections {
    bool enabled = false;
    float distance = 0;
};

/// The reflections a camera's `ScreenSpaceReflections` asks for, made
/// sound (D331): none for a distance not finite or not above nought.
[[nodiscard]] SceneScreenReflections reflectionsOf(const std::optional<ScreenSpaceReflections>& asked) noexcept;

/// ADR-0051's motion blur (D334): whether the view has it, and for what
/// share of each frame's time its shutter is open (above nought, at most
/// one).
struct SceneMotionBlur {
    bool enabled = false;
    float shutter = 0;
};

/// The motion blur a camera's `MotionBlur` asks for, made sound (D334):
/// none for a shutter not finite or not above nought.
[[nodiscard]] SceneMotionBlur motionBlurOf(const std::optional<MotionBlur>& asked) noexcept;

/// ADR-0051's depth of field (D336): whether the view has it, the distance
/// in focus in meters (a tenth to ten thousand), and the lens's f-number
/// (a half to 64).
struct SceneDepthOfField {
    bool enabled = false;
    float focus = 0;
    float aperture = 0;
};

/// The depth of field a camera's `DepthOfField` asks for, made sound
/// (D336): none for a focus or an aperture not finite or not above nought.
[[nodiscard]] SceneDepthOfField depthOfFieldOf(const std::optional<DepthOfField>& asked) noexcept;

/// ADR-0051's contact shadows (D338): whether the view has them, and how
/// far toward the sun a point looks for what shades it, in meters (a
/// hundredth to ten).
struct SceneContactShadows {
    bool enabled = false;
    float length = 0;
};

/// The contact shadows a camera's `ContactShadows` asks for, made sound
/// (D338): none for a length not finite or not above nought.
[[nodiscard]] SceneContactShadows contactShadowsOf(const std::optional<ContactShadows>& asked) noexcept;

/// ADR-0051's temporal inputs (D291). Whether the frame is antialiased
/// over time; its subpixel jitter, a pixel's fraction across and down in
/// [-0.5, 0.5), which the GPU half applies to the projection at its size;
/// the frame before's view and projection, unjittered, taking this frame's
/// eye-relative places; and whether that frame's picture may be reused: not
/// on the first frame, nor across a cut.
struct SceneTemporal {
    bool enabled = false;
    std::array<float, 2> jitter{0, 0};
    Matrix previousViewProjection{};
    bool history = false;
};

/// The subpixel jitter of a frame counted from nought: Halton (2, 3) over
/// eight frames, centered on the pixel (ADR-0051).
[[nodiscard]] std::array<float, 2> temporalJitter(std::uint64_t frame) noexcept;

struct SceneFrame {
    /// World axes to the eye's, and the eye's to clip space (reversed-Z,
    /// infinite far).
    Matrix view{};
    Matrix projection{};
    /// Where the eye looks, in the World's axes: what a cascade is chosen
    /// by.
    std::array<float, 3> forward{0, 0, -1};
    SceneShadows shadows;
    SceneTemporal temporal;
    SceneLightShadows lightShadows;
    SceneMetering metering;
    SceneGrading grading;
    SceneOcclusion occlusion;
    SceneBloom bloom;
    SceneScreenReflections reflections;
    SceneMotionBlur motionBlur;
    SceneDepthOfField depthOfField;
    SceneContactShadows contactShadows;
    Tonemapper tonemapper = Tonemapper::Agx;
    /// Whether the tonemapped picture is antialiased by FXAA (D296): never
    /// with the temporal inputs, which are then off.
    bool fxaa = false;
    /// The samples a pixel of the models' passes takes: one, or more where
    /// multisampling antialiases the frame (D343), never with the temporal
    /// inputs or FXAA.
    std::uint32_t samples = 1;
    /// Whether the picture is dithered by under one step of its eight-bit
    /// encoding (ADR-0051's debanding, D332): on unless a test compares
    /// pixels exactly.
    bool dither = true;
    /// The punctual lights that reach the view, and the clusters they are
    /// culled into (D290).
    std::vector<SceneLight> lights3d;
    SceneClusters clusters;
    std::size_t lightsCulled = 0;
    std::size_t lightsOverLimit = 0;
    /// Lights a full cluster could not name, counted once each time.
    std::size_t clusterOverflow = 0;
    /// The reflection probes that reach the view (D325), the nearest to the
    /// eye kept up to the limit, then ordered as a point takes them: the
    /// highest priority first, then the smaller box (D340); and those past
    /// the limit.
    std::vector<SceneProbe> probes;
    std::size_t probesOverLimit = 0;
    /// The decals that reach the view, in their entities' order, at most
    /// the limit; those out of view or not sound, and those past the limit
    /// (D339).
    std::vector<SceneDecal> decals;
    std::size_t decalsCulled = 0;
    std::size_t decalsOverLimit = 0;
    /// The camera's post processes to run, in its order (D349); and those
    /// left out: a material the game has not, a weight not finite, or past
    /// the limit. Material nought, or a weight of nought or less, runs
    /// nothing and is not counted.
    std::vector<ScenePostProcess> postProcesses;
    std::size_t postProcessesLeftOut = 0;
    /// The particle emitters, trails, and beams that reach the view, and
    /// what they spawn and leave out (D352, D354, D357), a material named
    /// by its place among the frame's materials.
    rawframe::particles::Frame particles;
    /// EV100.
    float exposure = 15;
    SceneLights lights;
    /// The opaque draws, grouped by their material's texture, then by mesh
    /// (D309); then the last `translucent` of them the translucent ones,
    /// farthest first (D305).
    std::vector<SceneDraw> draws;
    std::size_t translucent = 0;
    std::size_t drawn = 0;
    /// Outside the view.
    std::size_t culled = 0;
    /// Drawing nothing: mesh nought, alpha nought, or a scale of nought.
    std::size_t hidden = 0;
    /// A value not finite.
    std::size_t malformed = 0;
    /// A mesh neither the engine's nor the game's.
    std::size_t unknownMeshes = 0;
    /// A material the game has not, drawn with none.
    std::size_t unknownMaterials = 0;
    /// Every material's blob (ADR-0031, D303), a draw naming its place: the
    /// first none's, then the game's in the order they were given.
    std::vector<MaterialBlob> materials;
    /// Every material's texture, at its blob's place (D309).
    std::vector<SceneTextures> textures;
    std::size_t overLimit = 0;
};

/// `frame` without its reflection probes, in its list or its clusters: as
/// a probe's own bake sees the scene (D326, D340).
void withoutProbes(SceneFrame& frame) noexcept;

/// SPEC-0024's limit points for the scene: the models one frame queues
/// (SPEC-0024's `recorded_commands_per_frame`, one draw each). Models past
/// it are left out, from the last in draw order, counted as over the limit.
struct SceneLimits {
    std::size_t maximumModels = 16384;
    /// ADR-0051's maximum clustered lights per view and per-cluster cap for
    /// lights (D290).
    std::size_t maximumLights = 256;
    std::size_t maximumLightsPerCluster = 64;
    /// ADR-0051's decals a view draws, and a cluster holds (D339).
    std::size_t maximumDecals = 64;
    std::size_t maximumDecalsPerCluster = 16;
    /// ADR-0051's maximum shadow-casting punctual lights per view (D292).
    std::size_t maximumShadowedLights = 8;
    /// ADR-0051's reflection probes a view resolves at once (D325), and a
    /// cluster holds (D340).
    std::size_t maximumProbes = 32;
    std::size_t maximumProbesPerCluster = 8;
    /// The post processes a view runs (D349).
    std::size_t maximumPostProcesses = 8;
    /// ADR-0053's particle, trail, and beam limit points (D352, D354).
    rawframe::particles::Limits particles;
};

/// ADR-0051's one typed atlas for the punctual lights' shadows (D292), a
/// profile's values: its side, and its squares' sides, the largest for a
/// light covering much of the view and halving as it covers less, never
/// below the smallest. A side of nought gives no punctual shadows.
struct LightShadowSettings {
    std::uint32_t side = 2048;
    std::uint32_t largest = 512;
    std::uint32_t smallest = 128;
};

/// ADR-0051's typed cascade configuration, a profile's values: how many
/// cascades, how far shadows reach, how the splits blend a logarithmic
/// scheme (one) with a uniform one (nought), and each cascade's side in
/// texels. Cascades of nought draw no shadows.
struct ShadowSettings {
    std::uint32_t cascades = 4;
    float distance = 100;
    float logarithmicBlend = 0.8F;
    std::uint32_t side = 1024;
    /// For the sun's cascades and the punctual lights' atlas alike.
    ShadowFilter filter = ShadowFilter::Soft;
};

/// ADR-0051's closed set of anti-aliasing methods: none, the first-party
/// temporal one, the default (D291), FXAA, the cheap one, run on the
/// display-referred picture after the tonemapper (D296), and multisampling,
/// the forward path's own, the models' passes taking several samples a
/// pixel (D343).
enum class AntiAliasing : std::uint8_t {
    Off,
    Taa,
    Fxaa,
    Msaa
};

/// The samples a pixel multisampling may take (ADR-0051's typed limit
/// point, D343): two or four, four unless a profile says otherwise.
inline constexpr std::uint32_t kDefaultMultisamples = 4;

/// An eye moving farther than this in a frame cuts: the picture before is
/// not reused.
inline constexpr double kCutDistance = 10;

struct SceneSettings {
    /// The game's components of `rawframe.model.Model`'s type, in its order:
    /// an entity may show one of each.
    std::vector<schema::ComponentTypeId> models;
    /// The game's sun and sky components, if it declares them.
    std::optional<schema::ComponentTypeId> sun;
    std::optional<schema::ComponentTypeId> sky;
    /// The game's point and spot light components.
    std::vector<schema::ComponentTypeId> points;
    std::vector<schema::ComponentTypeId> spots;
    /// The game's reflection probe components (D325).
    std::vector<schema::ComponentTypeId> probes;
    /// The game's decal components (D339).
    std::vector<schema::ComponentTypeId> decals;
    /// The game's particle emitter components (D352).
    std::vector<schema::ComponentTypeId> emitters;
    /// The game's trail and beam components (D354).
    std::vector<schema::ComponentTypeId> trails;
    std::vector<schema::ComponentTypeId> beams;
    /// The game's meshes, by their identities.
    std::vector<SceneMesh> meshes;
    /// The game's materials, by their identities (D303).
    std::vector<SceneMaterial> materials;
    /// The game's post-process materials, by their identities (D349).
    std::vector<ScenePostProcessMaterial> postProcesses;
    SceneLimits limits;
    ShadowSettings shadows;
    LightShadowSettings lightShadows;
    AntiAliasing antiAliasing = AntiAliasing::Taa;
    /// With multisampling, the samples a pixel takes: two or four (D343).
    std::uint32_t multisamples = kDefaultMultisamples;
};

class Scene {
public:
    [[nodiscard]] static result::Result<std::unique_ptr<Scene>> create(const schema::SchemaRegistry& registry,
                                                                       SceneSettings settings);

    Scene(const Scene&) = delete;
    Scene& operator=(const Scene&) = delete;
    ~Scene();

    /// The extract stage, in `presentation_extract`: every model of the
    /// World, copied with its pose, and the sun and sky, if one is set; read
    /// only (the World is not const because its queries cache what they
    /// matched). Nothing of it is kept.
    void extract(world::World& world);

    /// The view and queue stages, in `present`: the extracted models seen
    /// through `camera` and culled to its view, those in view in the order
    /// of their meshes, then of their entities, then of their components.
    const SceneFrame& queue(const SceneCamera& camera);

    [[nodiscard]] std::span<const ModelInstance> extracted() const noexcept;
    [[nodiscard]] std::span<const LightInstance> extractedLights() const noexcept;
    [[nodiscard]] std::span<const ProbeInstance> extractedProbes() const noexcept;
    [[nodiscard]] std::span<const DecalInstance> extractedDecals() const noexcept;
    [[nodiscard]] std::span<const particles::EmitterInstance> extractedEmitters() const noexcept;
    [[nodiscard]] std::span<const particles::TrailInstance> extractedTrails() const noexcept;
    [[nodiscard]] std::span<const particles::BeamInstance> extractedBeams() const noexcept;

    /// The mesh a Model names by `id`: the game's or the engine's; none for
    /// another.
    [[nodiscard]] std::shared_ptr<const mesh::Mesh> mesh(std::uint64_t id) const;

    struct State;

private:
    explicit Scene(std::unique_ptr<State> state) noexcept;
    std::unique_ptr<State> state_;
};

/// A game's scene, declared: its model components, its camera
/// components (a player's and its views', D361), its sun and sky
/// components, if it has them, and its meshes.
struct GameScene {
    std::vector<schema::ComponentTypeId> models;
    std::vector<schema::ComponentTypeId> cameras;
    /// The views into render textures (D361).
    std::optional<schema::ComponentTypeId> view;
    std::optional<schema::ComponentTypeId> autoExposure;
    std::optional<schema::ComponentTypeId> grading;
    std::optional<schema::ComponentTypeId> occlusion;
    std::optional<schema::ComponentTypeId> bloom;
    std::optional<schema::ComponentTypeId> reflections;
    std::optional<schema::ComponentTypeId> motionBlur;
    std::optional<schema::ComponentTypeId> depthOfField;
    std::optional<schema::ComponentTypeId> contactShadows;
    std::optional<schema::ComponentTypeId> sun;
    std::optional<schema::ComponentTypeId> sky;
    std::vector<schema::ComponentTypeId> points;
    std::vector<schema::ComponentTypeId> spots;
    std::vector<schema::ComponentTypeId> probes;
    std::vector<schema::ComponentTypeId> decals;
    /// A camera's post processes (D349), in the game's order.
    std::vector<schema::ComponentTypeId> postProcesses;
    /// The particle emitters (D352), trails, and beams (D354).
    std::vector<schema::ComponentTypeId> emitters;
    std::vector<schema::ComponentTypeId> trails;
    std::vector<schema::ComponentTypeId> beams;
    std::vector<SceneMesh> meshes;
};

/// Finds the game's components of `rawframe.model`'s types, whose layouts
/// in `program` must be what this module reads, and its meshes. Refuses
/// (`NoModels`) a game with no model component, and (`BadComponents`) one
/// with two cameras, auto-exposures, gradings, ambient occlusions, blooms,
/// screen-space reflections, motion blurs, depths of field, contact
/// shadows, suns, or skies.
[[nodiscard]] result::Result<GameScene> loadGameScene(const world_kest::GameFiles& game, const kest::Program& program);

} // namespace rawframe::render_scene
