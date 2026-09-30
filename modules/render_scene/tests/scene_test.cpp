// The scene's CPU half: the engine's meshes are valid, closed, and face
// out; a model is placed relative to the eye as its pose and scale put it,
// even far from the origin; the projection is reversed-Z with no far
// plane; what is off the view, draws nothing, is malformed, or names an
// unknown mesh is left out and counted; models draw in the order of their
// meshes, then entities, and the limit leaves out a suffix of it; the sun
// and sky light in linear physical units; and a game's scene loads against
// its program.

#include "rawframe/physics3d/components.h"
#include "rawframe/render_scene/errors.h"
#include "rawframe/render_scene/scene.h"
#include "rawframe/test/test.h"

#include <cmath>
#include <cstdio>
#include <limits>
#include <numbers>
#include <string>
#include <utility>
#include <vector>

using namespace rawframe;
using namespace rawframe::render_scene;

namespace {

constexpr auto kModelId = schema::ComponentTypeId::fromText("3c8e1f52-7d04-4a2b-9e61-0f5a2c7d3b18");
constexpr auto kHatId = schema::ComponentTypeId::fromText("4c8e1f52-7d04-4a2b-9e61-0f5a2c7d3b18");
constexpr auto kSunId = schema::ComponentTypeId::fromText("5c8e1f52-7d04-4a2b-9e61-0f5a2c7d3b18");
constexpr auto kSkyId = schema::ComponentTypeId::fromText("6c8e1f52-7d04-4a2b-9e61-0f5a2c7d3b18");
constexpr std::uint64_t kRock = 0xc1;

/// The registry holds a name as a view: each is a literal.
template <typename T> schema::ComponentDescriptor plain(schema::ComponentTypeId id, std::string_view name) {
    return schema::ComponentDescriptor{
        .id = id, .name = name, .size = sizeof(T), .alignment = alignof(T), .plainData = true};
}

std::shared_ptr<const schema::SchemaRegistry> registry() {
    schema::RegistryBuilder builder;
    builder.add(plain<Model>(kModelId, "test.model"));
    builder.add(plain<Model>(kHatId, "test.hat"));
    builder.add(plain<Sun>(kSunId, "test.sun"));
    builder.add(plain<Sky>(kSkyId, "test.sky"));
    builder.add<physics3d::Pose3D>();
    return *builder.freeze();
}

/// A tetrahedron about the origin, standing in for a game's mesh.
std::shared_ptr<const mesh::Mesh> rock() {
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
                                .sun = kSunId,
                                .sky = kSkyId,
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

    const SceneFrame& frame(const SceneCamera& camera) {
        scene->extract(world);
        return scene->queue(camera);
    }
};

bool near(float value, float expected, float within = 1e-4F) {
    return std::abs(value - expected) < within;
}

/// The volume a closed mesh encloses: positive when its triangles face out.
float volumeOf(const mesh::Mesh& made) {
    float volume = 0;
    for (std::size_t index = 0; index + 2 < made.indices.size(); index += 3) {
        const mesh::Vector3& kA = made.positions[made.indices[index]];
        const mesh::Vector3& kB = made.positions[made.indices[index + 1]];
        const mesh::Vector3& kC = made.positions[made.indices[index + 2]];
        volume += ((kA[0] * ((kB[1] * kC[2]) - (kB[2] * kC[1]))) - (kA[1] * ((kB[0] * kC[2]) - (kB[2] * kC[0]))) +
                   (kA[2] * ((kB[0] * kC[1]) - (kB[1] * kC[0])))) /
                  6;
    }
    return volume;
}

} // namespace

RAWFRAME_TEST(TheEnginesMeshesAreClosedAndFaceOut) {
    constexpr float kPi = std::numbers::pi_v<float>;
    // Each one's volume: a polygon of 32 sides holds a little less than its
    // circle.
    const std::vector<std::pair<std::uint64_t, float>> kVolumes = {
        {kBox, 8}, {kSphere, 4 * kPi / 3}, {kCylinder, 2 * kPi}, {kCapsule, (4 * kPi / 3) + (2 * kPi)}};
    for (const auto& [kId, kVolume] : kVolumes) {
        const auto kMesh = engineMesh(kId);
        RAWFRAME_EXPECT(kMesh != nullptr && mesh::validate(*kMesh).has_value());
        if (kMesh == nullptr) {
            continue;
        }
        const float kHeld = volumeOf(*kMesh);
        RAWFRAME_EXPECT(kHeld > 0.97F * kVolume && kHeld <= kVolume * 1.0001F);
        // Every normal is a unit, and none points inward.
        for (std::size_t vertex = 0; vertex < kMesh->positions.size(); ++vertex) {
            const mesh::Vector3& kNormal = kMesh->normals[vertex];
            const mesh::Vector3& kAt = kMesh->positions[vertex];
            RAWFRAME_EXPECT(near((kNormal[0] * kNormal[0]) + (kNormal[1] * kNormal[1]) + (kNormal[2] * kNormal[2]), 1));
            RAWFRAME_EXPECT((kNormal[0] * kAt[0]) + (kNormal[1] * kAt[1]) + (kNormal[2] * kAt[2]) > 0);
        }
    }
    RAWFRAME_EXPECT(engineMesh(kRock) == nullptr);
    // Each is made anew: nothing is kept between calls.
    RAWFRAME_EXPECT(engineMesh(kBox) != engineMesh(kBox) && *engineMesh(kBox) == *engineMesh(kBox));
}

RAWFRAME_TEST(AModelIsPlacedRelativeToTheEye) {
    Rig rig;
    // A quarter turn about +Y, far from the origin: a box two meters long
    // along X, three meters ahead of an eye just as far out.
    const float kHalf = std::sqrt(0.5F);
    const world::EntityHandle kBoxed =
        rig.spawn(Model{.mesh = kBox, .scaleX = 2, .scaleY = 1, .scaleZ = 0.5F, .color = 0xFF0000FF},
                  physics3d::Pose3D{.x = 1e7, .y = 1, .z = -1e7 - 3, .qy = kHalf, .qw = kHalf});
    const SceneFrame& kFrame = rig.frame({.eye = {1e7, 1, -1e7}});
    RAWFRAME_EXPECT(kFrame.drawn == 1 && kFrame.draws.size() == 1);
    if (kFrame.draws.size() != 1) {
        return;
    }
    const SceneDraw& kDraw = kFrame.draws[0];
    RAWFRAME_EXPECT(kDraw.mesh == kBox && kDraw.entity == kBoxed);
    // Its X, turned, lies along -Z; its Z along +X.
    RAWFRAME_EXPECT(near(kDraw.model[0], 0) && near(kDraw.model[2], -2) && near(kDraw.model[8], 0.5F) &&
                    near(kDraw.model[5], 1) && near(kDraw.model[10], 0));
    RAWFRAME_EXPECT(near(kDraw.model[12], 0) && near(kDraw.model[13], 0) && near(kDraw.model[14], -3) &&
                    kDraw.model[15] == 1);
    // Normals scale inversely.
    RAWFRAME_EXPECT(near(kDraw.normal[2], -0.5F) && near(kDraw.normal[8], 2));
    // sRGB red is linear red.
    RAWFRAME_EXPECT(near(kDraw.color[0], 1) && near(kDraw.color[1], 0) && near(kDraw.color[3], 1));
    // No pose: at the origin, unturned.
    Rig unposed;
    unposed.spawn(Model{.mesh = kSphere}, std::nullopt);
    const SceneFrame& kUnposed = unposed.frame({.eye = {0, 0, 5}});
    RAWFRAME_EXPECT(kUnposed.draws.size() == 1 && near(kUnposed.draws[0].model[0], 1) &&
                    near(kUnposed.draws[0].model[14], -5));
}

RAWFRAME_TEST(TheProjectionIsReversedZWithNoFarPlane) {
    Rig rig;
    const SceneFrame& kFrame = rig.frame({.fovY = std::numbers::pi_v<float> / 2, .near = 0.5F, .aspect = 2});
    const Matrix& kP = kFrame.projection;
    // A point ahead at distance d: depth near / d, one at the near plane,
    // toward nought far away, never below.
    for (const float kDistance : {0.5F, 1.0F, 1000.0F, 1e9F}) {
        const float kZ = (kP[10] * -kDistance) + kP[14];
        const float kW = (kP[11] * -kDistance) + kP[15];
        RAWFRAME_EXPECT(kW > 0 && near(kZ / kW, 0.5F / kDistance) && kZ / kW > 0);
    }
    // A quarter turn's field: the top edge at one; the width twice that.
    RAWFRAME_EXPECT(near(kP[5], 1) && near(kP[0], 0.5F));
    // Looking along -Z from nought yaw: the World's axes are the eye's.
    RAWFRAME_EXPECT(near(kFrame.view[0], 1) && near(kFrame.view[5], 1) && near(kFrame.view[10], 1));
    // A quarter turn of yaw looks along -X: -X is ahead, -Z to the left.
    const SceneFrame& kTurned = rig.frame({.yaw = std::numbers::pi_v<float> / 2});
    RAWFRAME_EXPECT(near(kTurned.view[2], 1) && near(kTurned.view[8], -1) && near(kTurned.view[0], 0));
}

RAWFRAME_TEST(WhatCannotBeSeenIsLeftOutAndCounted) {
    Rig rig;
    const auto kAt = [](double x, double y, double z) {
        return physics3d::Pose3D{.x = x, .y = y, .z = z, .qw = 1};
    };
    rig.spawn(Model{.mesh = kBox}, kAt(0, 0, -10));
    // Behind, off to the side, and beyond the top, each by more than its
    // size; and one just past the side, whose corner is still in view.
    rig.spawn(Model{.mesh = kBox}, kAt(0, 0, 10));
    rig.spawn(Model{.mesh = kBox}, kAt(40, 0, -10));
    rig.spawn(Model{.mesh = kBox}, kAt(0, 20, -10));
    rig.spawn(Model{.mesh = kBox}, kAt(18.5, 0, -10));
    // Nearer than the near plane, whole.
    rig.spawn(Model{.mesh = kSphere, .scaleX = 0.01F, .scaleY = 0.01F, .scaleZ = 0.01F}, kAt(0, 0, -0.05));
    // Drawing nothing: mesh nought, alpha nought, a scale of nought.
    rig.spawn(Model{.mesh = 0}, kAt(0, 0, -10));
    rig.spawn(Model{.mesh = kBox, .color = 0xFFFFFF00}, kAt(0, 0, -10));
    rig.spawn(Model{.mesh = kBox, .scaleY = 0}, kAt(0, 0, -10));
    rig.spawn(Model{.mesh = kBox, .scaleX = std::nanf("")}, kAt(0, 0, -10));
    rig.spawn(Model{.mesh = kBox}, physics3d::Pose3D{.x = std::numeric_limits<double>::infinity(), .qw = 1});
    rig.spawn(Model{.mesh = 0xdead}, kAt(0, 0, -10));
    // Wider than tall (16:9): at 10 meters ahead, the side is about 17.8
    // meters out.
    const SceneFrame& kFrame = rig.frame({.fovY = std::numbers::pi_v<float> / 2});
    RAWFRAME_EXPECT(kFrame.drawn == 2 && kFrame.culled == 4 && kFrame.hidden == 3 && kFrame.malformed == 2 &&
                    kFrame.unknownMeshes == 1);
    // A camera that sees nothing culls all.
    const SceneFrame& kBlind = rig.frame({.fovY = 0});
    RAWFRAME_EXPECT(kBlind.drawn == 0 && kBlind.culled == 6);
    const SceneFrame& kNotANumber = rig.frame({.eye = {std::nan(""), 0, 0}});
    RAWFRAME_EXPECT(kNotANumber.drawn == 0 && kNotANumber.culled == 6 && std::isfinite(kNotANumber.view[0]));
}

RAWFRAME_TEST(ModelsDrawInTheOrderOfTheirMeshesThenEntities) {
    Rig rig{SceneLimits{.maximumModels = 3}};
    const physics3d::Pose3D kAhead{.z = -10, .qw = 1};
    const world::EntityHandle kFirst = rig.spawn(Model{.mesh = kSphere}, kAhead);
    const world::EntityHandle kSecond = rig.spawn(Model{.mesh = kRock}, kAhead);
    const world::EntityHandle kThird = rig.spawn(Model{.mesh = kSphere}, kAhead);
    // An entity shows one model of each component.
    Model hat{.mesh = kRock};
    RAWFRAME_EXPECT(rig.world.insertErased(kSecond, *rig.schema->find(kHatId), &hat).has_value());
    const SceneFrame& kFrame = rig.frame({});
    // The rock's identity is the smallest; the sphere's below the box's.
    RAWFRAME_EXPECT(kFrame.draws.size() == 3 && kFrame.drawn == 3 && kFrame.overLimit == 1);
    if (kFrame.draws.size() == 3) {
        RAWFRAME_EXPECT(kFrame.draws[0].entity == kSecond && kFrame.draws[1].entity == kSecond &&
                        kFrame.draws[2].entity == kFirst);
    }
    // The one past the limit is the last in the order.
    Rig all;
    all.spawn(Model{.mesh = kSphere}, kAhead);
    RAWFRAME_EXPECT(all.frame({}).drawn == 1);
    (void)kThird;
}

RAWFRAME_TEST(TheSunAndSkyLightInPhysicalUnits) {
    Rig rig;
    // Without them: a sun at noon, from above, and a clear blue sky.
    const SceneFrame& kDefault = rig.frame({});
    RAWFRAME_EXPECT(near(kDefault.lights.sun[0], 100000, 1) &&
                    near(kDefault.lights.sky[2], 5000 * std::pow((0xEB / 255.0F + 0.055F) / 1.055F, 2.4F), 0.5F) &&
                    kDefault.lights.sky[0] < kDefault.lights.sky[2] && kDefault.lights.toSun[1] > 0.8F);
    const world::EntityHandle kLight = *rig.world.create();
    Sun sun{.directionX = 0, .directionY = -2, .directionZ = 0, .illuminance = 1000, .color = 0x808080FF};
    Sky sky{.luminance = 10, .color = 0x0000FFFF};
    RAWFRAME_EXPECT(rig.world.insertErased(kLight, *rig.schema->find(kSunId), &sun).has_value());
    RAWFRAME_EXPECT(rig.world.insertErased(kLight, *rig.schema->find(kSkyId), &sky).has_value());
    const SceneFrame& kLit = rig.frame({});
    // sRGB 0x80 is about 0.216 linear.
    RAWFRAME_EXPECT(near(kLit.lights.toSun[1], 1) && near(kLit.lights.sun[0], 215.86F, 0.1F) &&
                    near(kLit.lights.sky[0], 0) && near(kLit.lights.sky[2], 10));
    // A sun not finite gives no light rather than a poisoned frame.
    Sun broken{.illuminance = std::numeric_limits<float>::infinity()};
    RAWFRAME_EXPECT(rig.world.insertErased(kLight, *rig.schema->find(kSunId), &broken).has_value());
    RAWFRAME_EXPECT(rig.frame({}).lights.sun[0] == 0);
}

RAWFRAME_TEST(AGamesSceneLoadsAgainstItsProgram) {
    const auto kLoad = [](std::string_view kest, std::string_view gameLines) {
        std::vector<std::pair<std::string, std::string>> held;
        held.emplace_back("shown.kest", std::string{"module shown\n\nimport rawframe.model\n\n"} + std::string{kest});
        held.emplace_back("shown.game", std::string{"program shown.kest\n"} + std::string{gameLines});
        const auto kFiles = world_kest::GameFiles::fromHeld("shown.game", std::move(held));
        RAWFRAME_EXPECT(kFiles.has_value());
        std::string report;
        const auto kProgram = kFiles->compile("shown.kest", {}, &report);
        RAWFRAME_EXPECT(kProgram.has_value());
        if (!kProgram.has_value()) {
            std::fprintf(stderr, "%s\n", report.c_str());
        }
        return loadGameScene(*kFiles, **kProgram);
    };
    // A Kest type is laid out when a function uses it.
    const std::string kUses =
        "fn show(models: [model.Model], views: [model.Camera], suns: [model.Sun], skies: [model.Sky]) {\n}\n";
    const std::string kModel = "component 3c8e1f52-7d04-4a2b-9e61-0f5a2c7d3b18 shown.look rawframe.model.Model\n";
    const std::string kLights = "component 5c8e1f52-7d04-4a2b-9e61-0f5a2c7d3b18 shown.sun rawframe.model.Sun\n"
                                "component 6c8e1f52-7d04-4a2b-9e61-0f5a2c7d3b18 shown.sky rawframe.model.Sky\n";
    const std::string kView = "component 7c8e1f52-7d04-4a2b-9e61-0f5a2c7d3b18 shown.view rawframe.model.Camera\n";
    const auto kLoaded = kLoad(kUses, kModel + kLights + kView);
    RAWFRAME_EXPECT(kLoaded.has_value() && kLoaded->models == (std::vector<schema::ComponentTypeId>{kModelId}) &&
                    kLoaded->sun == kSunId && kLoaded->sky == kSkyId &&
                    kLoaded->camera == schema::ComponentTypeId::fromText("7c8e1f52-7d04-4a2b-9e61-0f5a2c7d3b18") &&
                    kLoaded->meshes.empty());
    const auto kPlain = kLoad(kUses, kModel);
    RAWFRAME_EXPECT(kPlain.has_value() && !kPlain->camera && !kPlain->sun && !kPlain->sky);
    // A client has one view, and the World one sun and one sky.
    const auto kTwoSuns = kLoad(
        kUses, kModel + kLights + "component 8c8e1f52-7d04-4a2b-9e61-0f5a2c7d3b18 shown.moon rawframe.model.Sun\n");
    RAWFRAME_EXPECT(!kTwoSuns.has_value() && kTwoSuns.error().code() == code(RenderSceneError::BadComponents));
    const auto kNone = kLoad(kUses, kLights);
    RAWFRAME_EXPECT(!kNone.has_value() && kNone.error().code() == code(RenderSceneError::NoModels));
    // A type of the game's own by that name is not rawframe.model's.
    const auto kOwn = kLoad("struct Model {\n    mesh: u64\n}\n\nfn show(models: [Model]) {\n}\n",
                            "component 3c8e1f52-7d04-4a2b-9e61-0f5a2c7d3b18 shown.look Model\n");
    RAWFRAME_EXPECT(!kOwn.has_value() && kOwn.error().code() == code(RenderSceneError::BadComponents));
}
