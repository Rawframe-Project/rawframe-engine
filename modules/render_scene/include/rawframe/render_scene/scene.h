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
};

/// The light a frame is drawn in, linear Rec. 709 (ADR-0047) in physical
/// units (ADR-0051): the sun's illuminance in lux and the direction toward
/// it, and the sky's luminance in candela per square meter.
struct SceneLights {
    std::array<float, 3> toSun{0, 1, 0};
    std::array<float, 3> sun{0, 0, 0};
    std::array<float, 3> sky{0, 0, 0};
};

/// What the queue stage builds: the view, the light, and the draws in the
/// order a device draws them, grouped by mesh; and what was left out.
struct SceneFrame {
    /// World axes to the eye's, and the eye's to clip space (reversed-Z,
    /// infinite far).
    Matrix view{};
    Matrix projection{};
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
};

struct SceneSettings {
    /// The game's components of `rawframe.model.Model`'s type, in its order:
    /// an entity may show one of each.
    std::vector<schema::ComponentTypeId> models;
    /// The game's sun and sky components, if it declares them.
    std::optional<schema::ComponentTypeId> sun;
    std::optional<schema::ComponentTypeId> sky;
    /// The game's meshes, by their identities.
    std::vector<SceneMesh> meshes;
    SceneLimits limits;
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
    std::vector<SceneMesh> meshes;
};

/// Finds the game's components of `rawframe.model`'s types, whose layouts
/// in `program` must be what this module reads, and its meshes. Refuses
/// (`NoModels`) a game with no model component, and (`BadComponents`) one
/// with two cameras, suns, or skies.
[[nodiscard]] result::Result<GameScene> loadGameScene(const world_kest::GameFiles& game, const kest::Program& program);

} // namespace rawframe::render_scene
