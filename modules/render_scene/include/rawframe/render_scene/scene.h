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
#include "rawframe/mesh/mesh.h"
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
};

/// `rawframe.model.Sun` as C++ reads it.
struct Sun {
    float directionX = 0;
    float directionY = -1;
    float directionZ = 0;
    float illuminance = 0;
    std::uint32_t color = 0xFFFFFFFF;
};

/// `rawframe.model.Sky` as C++ reads it.
struct Sky {
    float luminance = 0;
    std::uint32_t color = 0xFFFFFFFF;
};

/// `rawframe.model.PointLight` as C++ reads it.
struct PointLight {
    float lumens = 0;
    float range = 0;
    std::uint32_t color = 0xFFFFFFFF;
};

/// `rawframe.model.SpotLight` as C++ reads it.
struct SpotLight {
    float lumens = 0;
    float range = 0;
    float inner = 0;
    float outer = 0;
    std::uint32_t color = 0xFFFFFFFF;
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
};

/// Column-major, as shaders read them.
using Matrix = std::array<float, 16>;

/// One model to draw: its mesh, where it is relative to the eye (the
/// World's axes, meters), and its surface's base color in linear light.
struct SceneDraw {
    std::uint64_t mesh = 0;
    /// Model space to eye-relative World space: turn, scale, and place.
    Matrix model{};
    /// Normals' turn: the inverse transpose of the model's upper 3 by 3,
    /// columns of four with the last nought.
    Matrix normal{};
    std::array<float, 4> color{1, 1, 1, 1};
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
    /// Each cluster's first index and count in `indices`, x fastest, then
    /// y from the top, then slices from the eye.
    std::vector<std::uint32_t> ranges;
    /// Lights by their place in the frame's lights.
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
};

/// The sun's shadows as the view stage derives them (ADR-0051's cascaded
/// shadow maps): up to four cascades, nearest first, and the models that
/// cast into them, in draw order. None without a sun or shadows.
struct SceneShadows {
    std::size_t count = 0;
    std::array<ShadowCascade, 4> cascades{};
    /// Each cascade's square's side in texels.
    std::uint32_t side = 0;
    /// Where shadows end, in meters from the eye; they fade over the last
    /// cascade's last tenth.
    float distance = 0;
    std::vector<SceneDraw> casters;
    /// Casters left out past the models' limit.
    std::size_t overLimit = 0;
};

/// What the queue stage builds: the view, the light, and the draws in the
/// order a device draws them, grouped by mesh; and what was left out.
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
    /// The punctual lights that reach the view, and the clusters they are
    /// culled into (D290).
    std::vector<SceneLight> lights3d;
    SceneClusters clusters;
    std::size_t lightsCulled = 0;
    std::size_t lightsOverLimit = 0;
    /// Lights a full cluster could not name, counted once each time.
    std::size_t clusterOverflow = 0;
    /// EV100.
    float exposure = 15;
    SceneLights lights;
    std::vector<SceneDraw> draws;
    std::size_t drawn = 0;
    /// Outside the view.
    std::size_t culled = 0;
    /// Drawing nothing: mesh nought, alpha nought, or a scale of nought.
    std::size_t hidden = 0;
    /// A value not finite.
    std::size_t malformed = 0;
    /// A mesh neither the engine's nor the game's.
    std::size_t unknownMeshes = 0;
    std::size_t overLimit = 0;
};

/// SPEC-0024's limit points for the scene: the models one frame queues
/// (SPEC-0024's `recorded_commands_per_frame`, one draw each). Models past
/// it are left out, from the last in draw order, counted as over the limit.
struct SceneLimits {
    std::size_t maximumModels = 16384;
    /// ADR-0051's maximum clustered lights per view and per-cluster cap for
    /// lights (D290).
    std::size_t maximumLights = 256;
    std::size_t maximumLightsPerCluster = 64;
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
};

/// ADR-0051's anti-aliasing methods that exist so far (D291): none, and the
/// first-party temporal one, the default. Multisampling and FXAA join the
/// closed set when they are built.
enum class AntiAliasing : std::uint8_t {
    Off,
    Taa
};

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
    /// The game's meshes, by their identities.
    std::vector<SceneMesh> meshes;
    SceneLimits limits;
    ShadowSettings shadows;
    AntiAliasing antiAliasing = AntiAliasing::Taa;
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

    /// The mesh a Model names by `id`: the game's or the engine's; none for
    /// another.
    [[nodiscard]] std::shared_ptr<const mesh::Mesh> mesh(std::uint64_t id) const;

    struct State;

private:
    explicit Scene(std::unique_ptr<State> state) noexcept;
    std::unique_ptr<State> state_;
};

/// A game's scene, declared: its model components, its camera, sun, and
/// sky components, if it has them, and its meshes.
struct GameScene {
    std::vector<schema::ComponentTypeId> models;
    std::optional<schema::ComponentTypeId> camera;
    std::optional<schema::ComponentTypeId> sun;
    std::optional<schema::ComponentTypeId> sky;
    std::vector<schema::ComponentTypeId> points;
    std::vector<schema::ComponentTypeId> spots;
    std::vector<SceneMesh> meshes;
};

/// Finds the game's components of `rawframe.model`'s types, whose layouts
/// in `program` must be what this module reads, and its meshes. Refuses
/// (`NoModels`) a game with no model component, and (`BadComponents`) one
/// with two cameras, suns, or skies.
[[nodiscard]] result::Result<GameScene> loadGameScene(const world_kest::GameFiles& game, const kest::Program& program);

} // namespace rawframe::render_scene
