#pragma once

// The scene tests' rig: a registry of the scene's components, a stand-in
// mesh, a World and a Scene over it, and a float's nearness.

#include "rawframe/physics3d/components.h"
#include "rawframe/render_scene/scene.h"
#include "rawframe/test/test.h"
#include "rawframe/world/layer.h"

#include <array>
#include <cmath>
#include <memory>
#include <optional>
#include <string_view>
#include <utility>

namespace rawframe::render_scene::test_rig {

inline constexpr auto kModelId = schema::ComponentTypeId::fromText("3c8e1f52-7d04-4a2b-9e61-0f5a2c7d3b18");
inline constexpr auto kHatId = schema::ComponentTypeId::fromText("4c8e1f52-7d04-4a2b-9e61-0f5a2c7d3b18");
inline constexpr auto kSunId = schema::ComponentTypeId::fromText("5c8e1f52-7d04-4a2b-9e61-0f5a2c7d3b18");
inline constexpr auto kSkyId = schema::ComponentTypeId::fromText("6c8e1f52-7d04-4a2b-9e61-0f5a2c7d3b18");
inline constexpr auto kPointId = schema::ComponentTypeId::fromText("9c8e1f52-7d04-4a2b-9e61-0f5a2c7d3b18");
inline constexpr auto kSpotId = schema::ComponentTypeId::fromText("ac8e1f52-7d04-4a2b-9e61-0f5a2c7d3b18");
inline constexpr auto kProbeId = schema::ComponentTypeId::fromText("dc8e1f52-7d04-4a2b-9e61-0f5a2c7d3b18");
inline constexpr auto kDecalId = schema::ComponentTypeId::fromText("4d8e1f52-7d04-4a2b-9e61-0f5a2c7d3b18");
inline constexpr auto kEmitterId = schema::ComponentTypeId::fromText("7d8e1f52-7d04-4a2b-9e61-0f5a2c7d3b18");
inline constexpr auto kPlacedId = schema::ComponentTypeId::fromText("8d8e1f52-7d04-4a2b-9e61-0f5a2c7d3b18");
inline constexpr auto kTrackedId = schema::ComponentTypeId::fromText("2f6c9a41-8b3e-4d17-a5c2-7e90d1b4f863");
inline constexpr std::uint64_t kRock = 0xc1;

/// The registry holds a name as a view: each is a literal.
template <typename T> inline schema::ComponentDescriptor plain(schema::ComponentTypeId id, std::string_view name) {
    return schema::ComponentDescriptor{
        .id = id, .name = name, .size = sizeof(T), .alignment = alignof(T), .plainData = true};
}

inline std::shared_ptr<const schema::SchemaRegistry> registry() {
    schema::RegistryBuilder builder;
    builder.add(plain<Model>(kModelId, "test.model"));
    builder.add(plain<Model>(kHatId, "test.hat"));
    builder.add(plain<Sun>(kSunId, "test.sun"));
    builder.add(plain<Sky>(kSkyId, "test.sky"));
    builder.add(plain<PointLight>(kPointId, "test.lamp"));
    builder.add(plain<SpotLight>(kSpotId, "test.torch"));
    builder.add(plain<ReflectionProbe>(kProbeId, "test.probe"));
    builder.add(plain<Decal>(kDecalId, "test.decal"));
    builder.add(plain<particles::ParticleEmitter>(kEmitterId, "test.emitter"));
    builder.add(plain<Placed>(kPlacedId, "test.placed"));
    builder.add(plain<Tracked>(kTrackedId, "test.tracked"));
    builder.add(plain<world::Layer>(world::Layer::kComponentTypeId, world::Layer::kComponentName));
    builder.add<physics3d::Pose3D>();
    return *builder.freeze();
}

/// A tetrahedron about the origin, standing in for a game's mesh.
inline std::shared_ptr<const mesh::Mesh> rock() {
    mesh::Mesh made{.positions = {{0, 1, 0}, {1, -1, 1}, {-1, -1, 1}, {0, -1, -1}},
                    .indices = {0, 2, 1, 0, 1, 3, 0, 3, 2, 1, 2, 3}};
    made.parts.push_back({.firstIndex = 0, .indexCount = 12});
    return std::make_shared<const mesh::Mesh>(std::move(made));
}

struct Rig {
    std::shared_ptr<const schema::SchemaRegistry> schema = registry();
    world::World world{schema};
    std::unique_ptr<Scene> scene;

    explicit Rig(SceneLimits limits = {}) {
        scene = *Scene::create(*schema,
                               {.models = {kModelId, kHatId},
                                .placements = {kPlacedId},
                                .tracking = {kTrackedId},
                                .sun = kSunId,
                                .sky = kSkyId,
                                .points = {kPointId},
                                .spots = {kSpotId},
                                .probes = {kProbeId},
                                .decals = {kDecalId},
                                .emitters = {kEmitterId},
                                .meshes = {{.id = kRock, .mesh = rock()}},
                                .limits = limits});
    }

    world::EntityHandle
    spawn(Model model, std::optional<physics3d::Pose3D> pose, schema::ComponentTypeId as = kModelId) {
        const world::EntityHandle kEntity = *world.create();
        RAWFRAME_EXPECT(world.insertErased(kEntity, *schema->find(as), &model).has_value());
        if (pose) {
            RAWFRAME_EXPECT(world.insert(kEntity, *schema->key<physics3d::Pose3D>(), *pose).has_value());
        }
        return kEntity;
    }

    const SceneFrame& frame(const SceneCamera& camera, const HandsInWorld* hands = nullptr) {
        scene->extract(world, nullptr, hands);
        return scene->queue(camera);
    }
};

inline bool near(float value, float expected, float within = 1e-4F) {
    return std::abs(value - expected) < within;
}

/// Where `matrix` puts an eye-relative point, x, y, and depth.
inline std::array<float, 3> clipOf(const Matrix& matrix, const std::array<float, 3>& point) {
    std::array<float, 3> out{};
    for (std::size_t row = 0; row < 3; ++row) {
        out[row] =
            (matrix[row] * point[0]) + (matrix[4 + row] * point[1]) + (matrix[8 + row] * point[2]) + matrix[12 + row];
    }
    return out;
}

} // namespace rawframe::render_scene::test_rig
