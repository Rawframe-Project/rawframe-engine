// The scene's CPU half: the engine's meshes are valid, closed, and face
// out; a model is placed relative to the eye as its pose and scale put it,
// even far from the origin; the projection is reversed-Z with no far
// plane; what is off the view, draws nothing, is malformed, or names an
// unknown mesh is left out and counted; models draw in the order of their
// meshes, then entities, and the limit leaves out a suffix of it; the sun
// and sky light in linear physical units; point and spot lights light in
// candela, are culled by their reach, and name the clusters they reach; and
// a game's scene loads against its program.

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
constexpr auto kPointId = schema::ComponentTypeId::fromText("9c8e1f52-7d04-4a2b-9e61-0f5a2c7d3b18");
constexpr auto kSpotId = schema::ComponentTypeId::fromText("ac8e1f52-7d04-4a2b-9e61-0f5a2c7d3b18");
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
    builder.add(plain<PointLight>(kPointId, "test.lamp"));
    builder.add(plain<SpotLight>(kSpotId, "test.torch"));
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
                                .points = {kPointId},
                                .spots = {kSpotId},
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
        "fn show(models: [model.Model], views: [model.Camera], suns: [model.Sun], skies: [model.Sky],\n"
        "        lamps: [model.PointLight], torches: [model.SpotLight], meters: [model.AutoExposure],\n"
        "        grades: [model.Grading]) {\n}\n";
    const std::string kModel = "component 3c8e1f52-7d04-4a2b-9e61-0f5a2c7d3b18 shown.look rawframe.model.Model\n";
    const std::string kLights = "component 5c8e1f52-7d04-4a2b-9e61-0f5a2c7d3b18 shown.sun rawframe.model.Sun\n"
                                "component 6c8e1f52-7d04-4a2b-9e61-0f5a2c7d3b18 shown.sky rawframe.model.Sky\n";
    const std::string kLamps = "component 9c8e1f52-7d04-4a2b-9e61-0f5a2c7d3b18 shown.lamp rawframe.model.PointLight\n"
                               "component ac8e1f52-7d04-4a2b-9e61-0f5a2c7d3b18 shown.torch rawframe.model.SpotLight\n";
    const std::string kView = "component 7c8e1f52-7d04-4a2b-9e61-0f5a2c7d3b18 shown.view rawframe.model.Camera\n"
                              "component bc8e1f52-7d04-4a2b-9e61-0f5a2c7d3b18 shown.meter rawframe.model.AutoExposure\n"
                              "component cc8e1f52-7d04-4a2b-9e61-0f5a2c7d3b18 shown.grade rawframe.model.Grading\n";
    const auto kLoaded = kLoad(kUses, kModel + kLights + kView + kLamps);
    RAWFRAME_EXPECT(kLoaded.has_value() && kLoaded->models == (std::vector<schema::ComponentTypeId>{kModelId}) &&
                    kLoaded->sun == kSunId && kLoaded->sky == kSkyId &&
                    kLoaded->camera == schema::ComponentTypeId::fromText("7c8e1f52-7d04-4a2b-9e61-0f5a2c7d3b18") &&
                    kLoaded->meshes.empty() && kLoaded->points == (std::vector<schema::ComponentTypeId>{kPointId}) &&
                    kLoaded->spots == (std::vector<schema::ComponentTypeId>{kSpotId}) &&
                    kLoaded->autoExposure ==
                        schema::ComponentTypeId::fromText("bc8e1f52-7d04-4a2b-9e61-0f5a2c7d3b18") &&
                    kLoaded->grading == schema::ComponentTypeId::fromText("cc8e1f52-7d04-4a2b-9e61-0f5a2c7d3b18"));
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

namespace {

/// Where `matrix` puts an eye-relative point, x, y, and depth.
std::array<float, 3> clipOf(const Matrix& matrix, const std::array<float, 3>& point) {
    std::array<float, 3> out{};
    for (std::size_t row = 0; row < 3; ++row) {
        out[row] =
            (matrix[row] * point[0]) + (matrix[4 + row] * point[1]) + (matrix[8 + row] * point[2]) + matrix[12 + row];
    }
    return out;
}

} // namespace

RAWFRAME_TEST(TheSunsCascadesCoverTheViewAndHoldStill) {
    Rig rig;
    const auto kAt = [](double x, double y, double z) {
        return physics3d::Pose3D{.x = x, .y = y, .z = z, .qw = 1};
    };
    // Ahead, behind the eye but near, and beyond the shadows' reach.
    rig.spawn(Model{.mesh = kBox}, kAt(1000, 0, -10));
    rig.spawn(Model{.mesh = kBox}, kAt(1000, 0, 10));
    rig.spawn(Model{.mesh = kBox}, kAt(1000, 0, -300));
    const SceneCamera kCamera{.eye = {1000, 2, 0}, .fovY = 1, .near = 0.1F, .aspect = 1.5F};
    const SceneFrame& kFrame = rig.frame(kCamera);
    const SceneShadows& kShadows = kFrame.shadows;
    RAWFRAME_EXPECT(kShadows.count == 4 && kShadows.side == 1024 && near(kShadows.distance, 100));
    // Nearest first, the last at the shadows' distance, each texel its
    // square's side over the texels.
    float before = 0;
    for (std::size_t at = 0; at < kShadows.count; ++at) {
        RAWFRAME_EXPECT(kShadows.cascades[at].far > before && kShadows.cascades[at].texel > 0);
        before = kShadows.cascades[at].far;
    }
    RAWFRAME_EXPECT(near(kShadows.cascades[3].far, 100, 0.01F));
    // The box ahead is in the first cascades' squares, its depth between
    // nought and one; the two near ones cast, the far one does not, though
    // it is drawn (the view has no far plane); the one behind is not.
    const std::array<float, 3> kInFirst = clipOf(kShadows.cascades[1].viewProjection, {0, -2, -10});
    RAWFRAME_EXPECT(std::abs(kInFirst[0]) < 1 && std::abs(kInFirst[1]) < 1 && kInFirst[2] > 0 && kInFirst[2] < 1);
    RAWFRAME_EXPECT(kShadows.casters.size() == 2 && kFrame.draws.size() == 2);
    // Nearer the sun is deeper: a point above another is nearer one.
    const std::array<float, 3> kAbove = clipOf(kShadows.cascades[1].viewProjection, {0, 8, -10});
    RAWFRAME_EXPECT(kAbove[2] > kInFirst[2]);

    // The eye moves a little: a point of the World stays on the same part
    // of its texel, so the shadows do not shimmer.
    const auto kTexelPart = [](const SceneFrame& frame, const std::array<double, 3>& eye) {
        const std::array<float, 3> kPoint = {
            static_cast<float>(1003 - eye[0]), static_cast<float>(0 - eye[1]), static_cast<float>(-12 - eye[2])};
        const std::array<float, 3> kClip = clipOf(frame.shadows.cascades[0].viewProjection, kPoint);
        const float kTexels = (kClip[0] + 1) / 2 * static_cast<float>(frame.shadows.side);
        return kTexels - std::floor(kTexels);
    };
    const float kFirst = kTexelPart(kFrame, kCamera.eye);
    SceneCamera moved = kCamera;
    moved.eye = {1000.013, 2.007, 0.021};
    const float kSecond = kTexelPart(rig.frame(moved), moved.eye);
    RAWFRAME_EXPECT(std::abs(kFirst - kSecond) < 0.02F || std::abs(std::abs(kFirst - kSecond) - 1) < 0.02F);

    // Without the sun's light, no shadows.
    const world::EntityHandle kLight = *rig.world.create();
    Sun dark{.illuminance = 0};
    RAWFRAME_EXPECT(rig.world.insertErased(kLight, *rig.schema->find(kSunId), &dark).has_value());
    const SceneFrame& kDark = rig.frame(kCamera);
    RAWFRAME_EXPECT(kDark.shadows.count == 0 && kDark.shadows.casters.empty());
}

RAWFRAME_TEST(PointAndSpotLightsLightInCandelaAndNameTheirClusters) {
    Rig rig;
    const auto kPlace = [&rig](auto light, schema::ComponentTypeId as, physics3d::Pose3D pose) {
        const world::EntityHandle kEntity = *rig.world.create();
        RAWFRAME_EXPECT(rig.world.insertErased(kEntity, *rig.schema->find(as), &light).has_value());
        RAWFRAME_EXPECT(rig.world.insert(kEntity, *rig.schema->key<physics3d::Pose3D>(), pose).has_value());
    };
    // Ahead of the eye: a white lamp, and a torch turned to shine down.
    kPlace(PointLight{.lumens = 800, .range = 5, .color = 0xFFFFFFFF}, kPointId, {.x = 500, .y = 1, .z = -10, .qw = 1});
    const float kDown = std::sin(std::numbers::pi_v<float> / 4);
    kPlace(SpotLight{.lumens = 314.159F, .range = 8, .inner = 0.2F, .outer = 0.5F, .color = 0xFFFFFFFF},
           kSpotId,
           {.x = 503, .y = 3, .z = -20, .qx = -kDown, .qw = kDown});
    // Behind the eye, out of reach; and one that gives no light.
    kPlace(PointLight{.lumens = 800, .range = 5, .color = 0xFFFFFFFF}, kPointId, {.x = 500, .y = 1, .z = 20, .qw = 1});
    kPlace(PointLight{.lumens = 0, .range = 5, .color = 0xFFFFFFFF}, kPointId, {.x = 500, .y = 1, .z = -5, .qw = 1});
    RAWFRAME_EXPECT(rig.scene->extractedLights().empty());
    const SceneCamera kCamera{.eye = {500, 1, 0}, .fovY = 1, .near = 0.1F, .aspect = 16.0F / 9};
    const SceneFrame& kFrame = rig.frame(kCamera);
    RAWFRAME_EXPECT(rig.scene->extractedLights().size() == 4);
    RAWFRAME_EXPECT(kFrame.lights3d.size() == 2 && kFrame.lightsCulled == 2 && kFrame.lightsOverLimit == 0);
    // A lumen over the sphere is a candela for a point, over π for a spot;
    // each is placed relative to the eye, and the spot shines along its -Z.
    const SceneLight& kLamp = kFrame.lights3d[0];
    const SceneLight& kTorch = kFrame.lights3d[1];
    RAWFRAME_EXPECT(!kLamp.spot && near(kLamp.intensity[0], 800 / (4 * std::numbers::pi_v<float>), 0.01F) &&
                    near(kLamp.position[2], -10) && near(kLamp.position[1], 0));
    RAWFRAME_EXPECT(kTorch.spot && near(kTorch.intensity[1], 100, 0.01F) && near(kTorch.direction[1], -1, 1e-3F) &&
                    near(kTorch.cosInner, std::cos(0.2F)) && near(kTorch.cosOuter, std::cos(0.5F)));
    // The lamp, ten ahead in the middle of the view, names the middle
    // tiles of the slice ten ahead, and no cluster far from it.
    const SceneClusters& kClusters = kFrame.clusters;
    RAWFRAME_EXPECT(kClusters.ranges.size() == std::size_t{kClusters.tilesX} * kClusters.tilesY * kClusters.slices * 2);
    const auto kNames = [&kClusters](std::uint32_t x, std::uint32_t y, std::uint32_t slice, std::uint32_t light) {
        const std::size_t kCluster = (((std::size_t{slice} * kClusters.tilesY) + y) * kClusters.tilesX) + x;
        const std::uint32_t kFirst = kClusters.ranges[kCluster * 2];
        const std::uint32_t kCount = kClusters.ranges[(kCluster * 2) + 1];
        for (std::uint32_t at = kFirst; at < kFirst + kCount; ++at) {
            if (kClusters.indices[at] == light) {
                return true;
            }
        }
        return false;
    };
    const auto kSliceOf = [&kClusters](float ahead) {
        return static_cast<std::uint32_t>(std::log(ahead / kClusters.near) * static_cast<float>(kClusters.slices) /
                                          std::log(kClusters.far / kClusters.near));
    };
    RAWFRAME_EXPECT(kNames(kClusters.tilesX / 2, kClusters.tilesY / 2, kSliceOf(10), 0));
    RAWFRAME_EXPECT(!kNames(0, 0, kSliceOf(10), 0) && !kNames(kClusters.tilesX / 2, kClusters.tilesY / 2, 0, 0) &&
                    !kNames(kClusters.tilesX / 2, kClusters.tilesY / 2, kSliceOf(100), 0));
    RAWFRAME_EXPECT(kNames(kClusters.tilesX / 2, kClusters.tilesY / 2, kSliceOf(20), 1) && kFrame.clusterOverflow == 0);

    // More lights than a cluster holds: the rest are counted, not lost
    // silently.
    SceneLimits limits;
    limits.maximumLightsPerCluster = 3;
    Rig crowded(limits);
    for (int at = 0; at < 5; ++at) {
        const world::EntityHandle kEntity = *crowded.world.create();
        PointLight lamp{.lumens = 100, .range = 2, .color = 0xFFFFFFFF};
        RAWFRAME_EXPECT(crowded.world.insertErased(kEntity, *crowded.schema->find(kPointId), &lamp).has_value());
        const physics3d::Pose3D kPose{.z = -6.0 - (0.01 * at), .qw = 1};
        RAWFRAME_EXPECT(crowded.world.insert(kEntity, *crowded.schema->key<physics3d::Pose3D>(), kPose).has_value());
    }
    const SceneFrame& kCrowded = crowded.frame({.fovY = 1, .near = 0.1F, .aspect = 1});
    RAWFRAME_EXPECT(kCrowded.lights3d.size() == 5 && kCrowded.clusterOverflow > 0);
    for (std::size_t at = 1; at < kCrowded.clusters.ranges.size(); at += 2) {
        RAWFRAME_EXPECT(kCrowded.clusters.ranges[at] <= 3);
    }
}

RAWFRAME_TEST(TheTemporalInputsFollowTheEyeAndTheModels) {
    Rig rig;
    const auto kAt = [](double x, double y, double z) {
        return physics3d::Pose3D{.x = x, .y = y, .z = z, .qw = 1};
    };
    const world::EntityHandle kRockEntity = rig.spawn(Model{.mesh = kBox}, kAt(2000, 0, -10));
    const SceneCamera kCamera{.eye = {2000, 0, 0}, .fovY = 1, .near = 0.1F, .aspect = 1};
    // The first frame: jittered, with nothing before it to reuse; the
    // model was where it is.
    const SceneFrame& kFirst = rig.frame(kCamera);
    RAWFRAME_EXPECT(kFirst.temporal.enabled && !kFirst.temporal.history &&
                    kFirst.temporal.jitter == temporalJitter(0) && kFirst.draws.size() == 1 &&
                    kFirst.draws[0].previous == kFirst.draws[0].model);
    const Matrix kFirstView = kFirst.temporal.previousViewProjection;
    const std::array<float, 3> kFirstClip = clipOf(kFirstView, {0, 0, -10});
    // The model moves a meter away and the eye a meter back: the frame
    // before's view takes the model's place before to where it was seen.
    RAWFRAME_EXPECT(
        rig.world.insert(kRockEntity, *rig.schema->key<physics3d::Pose3D>(), kAt(2000, 0, -11)).has_value());
    SceneCamera back = kCamera;
    back.eye = {2000, 0, 1};
    const SceneFrame& kSecond = rig.frame(back);
    RAWFRAME_EXPECT(kSecond.temporal.history && kSecond.temporal.jitter == temporalJitter(1) &&
                    kSecond.draws.size() == 1);
    RAWFRAME_EXPECT(near(kSecond.draws[0].previous[14], -11) && near(kSecond.draws[0].model[14], -12));
    const std::array<float, 3> kBefore = clipOf(kSecond.temporal.previousViewProjection, {0, 0, -11});
    RAWFRAME_EXPECT(near(kBefore[0], kFirstClip[0]) && near(kBefore[1], kFirstClip[1]) &&
                    near(kBefore[2], kFirstClip[2]));
    // A cut: the eye jumps, and nothing before is reused.
    SceneCamera away = kCamera;
    away.eye = {2050, 0, 0};
    RAWFRAME_EXPECT(!rig.frame(away).temporal.history);
    // Eight jitters, each within the pixel, then again.
    for (std::uint64_t frame = 0; frame < 8; ++frame) {
        const std::array<float, 2> kJitter = temporalJitter(frame);
        RAWFRAME_EXPECT(kJitter[0] >= -0.5F && kJitter[0] < 0.5F && kJitter[1] >= -0.5F && kJitter[1] < 0.5F &&
                        temporalJitter(frame + 8) == kJitter);
        for (std::uint64_t other = 0; other < frame; ++other) {
            RAWFRAME_EXPECT(temporalJitter(other) != kJitter);
        }
    }
    // Without anti-aliasing, no jitter.
    auto plain = *Scene::create(*rig.schema, {.models = {kModelId}, .antiAliasing = AntiAliasing::Off});
    plain->extract(rig.world);
    const SceneFrame& kPlain = plain->queue(kCamera);
    RAWFRAME_EXPECT(!kPlain.temporal.enabled && !kPlain.temporal.history &&
                    kPlain.temporal.jitter == (std::array<float, 2>{0, 0}) && !kPlain.fxaa);
    // FXAA works on the one picture: no jitter and no history (D296).
    auto cheap = *Scene::create(*rig.schema, {.models = {kModelId}, .antiAliasing = AntiAliasing::Fxaa});
    cheap->extract(rig.world);
    const SceneFrame& kCheap = cheap->queue(kCamera);
    RAWFRAME_EXPECT(kCheap.fxaa && !kCheap.temporal.enabled && !kCheap.temporal.history &&
                    kCheap.temporal.jitter == (std::array<float, 2>{0, 0}));
}

RAWFRAME_TEST(PunctualShadowsShareOneAtlasByCover) {
    const auto kPlace = [](Rig& rig, auto light, schema::ComponentTypeId as, physics3d::Pose3D pose) {
        const world::EntityHandle kEntity = *rig.world.create();
        RAWFRAME_EXPECT(rig.world.insertErased(kEntity, *rig.schema->find(as), &light).has_value());
        RAWFRAME_EXPECT(rig.world.insert(kEntity, *rig.schema->key<physics3d::Pose3D>(), pose).has_value());
    };
    const float kDown = std::sin(std::numbers::pi_v<float> / 4);
    const auto kFill = [&kPlace, kDown](Rig& rig) {
        // A shadowed spot two meters ahead, shining down on a box below it;
        // a shadowed lamp forty ahead; an unshadowed lamp; a box far off.
        kPlace(rig,
               SpotLight{.lumens = 500, .range = 8, .inner = 0.3F, .outer = 0.6F, .color = 0xFFFFFFFF, .shadows = true},
               kSpotId,
               {.x = 100, .y = 4, .z = -2, .qx = -kDown, .qw = kDown});
        kPlace(rig,
               PointLight{.lumens = 800, .range = 10, .color = 0xFFFFFFFF, .shadows = true},
               kPointId,
               {.x = 100, .y = 2, .z = -40, .qw = 1});
        kPlace(
            rig, PointLight{.lumens = 800, .range = 10, .color = 0xFFFFFFFF}, kPointId, {.x = 104, .z = -6, .qw = 1});
        rig.spawn(Model{.mesh = kBox}, physics3d::Pose3D{.x = 100, .y = 1, .z = -2, .qw = 1});
        rig.spawn(Model{.mesh = kBox}, physics3d::Pose3D{.x = 160, .y = 1, .z = -2, .qw = 1});
    };
    const SceneCamera kCamera{.eye = {100, 2, 0}, .fovY = 1, .near = 0.1F, .aspect = 1.5F};
    Rig rig;
    kFill(rig);
    const SceneFrame& kFrame = rig.frame(kCamera);
    const SceneLightShadows& kShadows = kFrame.lightShadows;
    RAWFRAME_EXPECT(kFrame.lights3d.size() == 3 && kShadows.side == 2048 && kShadows.slots.size() == 7 &&
                    kShadows.evicted == 0);
    // The spot covers the most: the largest square first, at the atlas's
    // corner; the lamp's six faces a size smaller beside it, in Morton
    // order; the unshadowed lamp has none.
    const SceneLight& kSpot = kFrame.lights3d[0];
    const SceneLight& kLamp = kFrame.lights3d[1];
    RAWFRAME_EXPECT(kSpot.spot && kSpot.shadowSlots == 1 && kLamp.shadowSlots == 6 &&
                    kFrame.lights3d[2].shadowSlots == 0);
    const ShadowSlot& kSpotSlot = kShadows.slots[kSpot.shadowSlot];
    RAWFRAME_EXPECT(kSpotSlot.side == 512 && kSpotSlot.x == 0 && kSpotSlot.y == 0);
    const std::array<std::array<std::uint32_t, 2>, 6> kFaces = {
        {{512, 0}, {768, 0}, {512, 256}, {768, 256}, {0, 512}, {256, 512}}};
    for (std::uint32_t face = 0; face < 6; ++face) {
        const ShadowSlot& kFace = kShadows.slots[kLamp.shadowSlot + face];
        RAWFRAME_EXPECT(kFace.side == 256 && kFace.x == kFaces[face][0] && kFace.y == kFaces[face][1] &&
                        near(kFace.tangent, 1));
    }
    // The spot looks down: the box below it is its one caster, seen in the
    // middle of its square, deeper the nearer; the far box casts nowhere.
    RAWFRAME_EXPECT(kSpotSlot.casterCount == 1 && near(kSpotSlot.forward[1], -1, 1e-3F));
    // Two meters below the light, then three: straight ahead of it, the
    // distance ahead its w and the near plane over that its depth.
    const auto kW = [](const Matrix& matrix, const std::array<float, 3>& point) {
        return (matrix[3] * point[0]) + (matrix[7] * point[1]) + (matrix[11] * point[2]) + matrix[15];
    };
    const std::array<float, 3> kBelow = clipOf(kSpotSlot.viewProjection, {0, 0, -2});
    const std::array<float, 3> kLower = clipOf(kSpotSlot.viewProjection, {0, -1, -2});
    RAWFRAME_EXPECT(near(kBelow[0], 0) && near(kBelow[1], 0) && near(kLower[0], 0) && near(kLower[1], 0) &&
                    near(kW(kSpotSlot.viewProjection, {0, 0, -2}), 2) &&
                    near(kW(kSpotSlot.viewProjection, {0, -1, -2}), 3) && near(kBelow[2], kSpotSlot.near));
    for (const ShadowSlot& kSlot : kShadows.slots) {
        for (std::uint32_t at = kSlot.firstCaster; at < kSlot.firstCaster + kSlot.casterCount; ++at) {
            RAWFRAME_EXPECT(kShadows.casters[at].model[12] < 50);
        }
    }

    // A budget of one light: the spot keeps its shadows, the lamp's are
    // counted as lost; an atlas of nought gives none.
    SceneLimits one;
    one.maximumShadowedLights = 1;
    Rig budget(one);
    kFill(budget);
    const SceneFrame& kBudget = budget.frame(kCamera);
    RAWFRAME_EXPECT(kBudget.lightShadows.slots.size() == 1 && kBudget.lightShadows.evicted == 1 &&
                    kBudget.lights3d[0].shadowSlots == 1 && kBudget.lights3d[1].shadowSlots == 0);
    auto none = *Scene::create(*rig.schema,
                               {.models = {kModelId},
                                .points = {kPointId},
                                .spots = {kSpotId},
                                .lightShadows = {.side = 0, .largest = 512, .smallest = 128}});
    none->extract(rig.world);
    const SceneFrame& kNone = none->queue(kCamera);
    RAWFRAME_EXPECT(kNone.lightShadows.side == 0 && kNone.lightShadows.slots.empty() &&
                    kNone.lights3d[0].shadowSlots == 0);
}

RAWFRAME_TEST(AMeteredCameraIsMadeSound) {
    Rig rig;
    SceneCamera camera{.fovY = 1, .near = 0.1F, .aspect = 1};
    RAWFRAME_EXPECT(!rig.frame(camera).metering.enabled);
    // A negative rate, fractions past one, and a long pause: stopped,
    // clamped, and cut to a quarter second; the first metered frame goes
    // at once to what it measures, the next at the rates, and one after
    // the eye cuts away at once again.
    camera.metering = AutoExposure{
        .minimum = 8, .maximum = 16, .brighten = -1, .darken = 2, .compensation = 1, .low = 0.9F, .high = 1.5F};
    camera.elapsed = 3;
    const SceneMetering kMetering = rig.frame(camera).metering;
    RAWFRAME_EXPECT(kMetering.enabled && kMetering.settings.minimum == 8 && kMetering.settings.maximum == 16 &&
                    kMetering.settings.brighten == 0 && kMetering.settings.darken == 2 &&
                    kMetering.settings.low == 0.9F && kMetering.settings.high == 1 && kMetering.elapsed == 0.25F &&
                    kMetering.snap);
    RAWFRAME_EXPECT(!rig.frame(camera).metering.snap);
    camera.eye = {0, 0, 40};
    RAWFRAME_EXPECT(rig.frame(camera).metering.snap);
    RAWFRAME_EXPECT(!rig.frame(camera).metering.snap);
    // A meter not yet set, all nought, or with its bounds out of order,
    // meters nothing.
    SceneCamera unset = camera;
    unset.metering = AutoExposure{};
    RAWFRAME_EXPECT(!rig.frame(unset).metering.enabled);
    unset.metering = AutoExposure{.minimum = 16, .maximum = 8, .high = 1};
    RAWFRAME_EXPECT(!rig.frame(unset).metering.enabled);
    // A value not finite meters nothing.
    camera.metering->compensation = std::numeric_limits<float>::quiet_NaN();
    RAWFRAME_EXPECT(!rig.frame(camera).metering.enabled);
}

RAWFRAME_TEST(ACamerasGradeAndTonemapperAreMadeSound) {
    // Neutral: the balance is the identity, and the rest passes light on.
    const SceneGrading kNeutral = gradingOf(Grading{});
    RAWFRAME_EXPECT(kNeutral.enabled);
    for (std::size_t at = 0; at < 9; ++at) {
        RAWFRAME_EXPECT(near(kNeutral.balance[at], at % 4 == 0 ? 1.0F : 0.0F, 2e-3F));
    }
    // Warmer: white comes out redder than blue; cooler, bluer; a tint
    // above nought lowers green.
    const auto kWhiteOf = [](const SceneGrading& grade) {
        std::array<float, 3> out{};
        for (std::size_t row = 0; row < 3; ++row) {
            out[row] = grade.balance[row * 3] + grade.balance[(row * 3) + 1] + grade.balance[(row * 3) + 2];
        }
        return out;
    };
    const std::array<float, 3> kWarm = kWhiteOf(gradingOf(Grading{.temperature = 1}));
    const std::array<float, 3> kCool = kWhiteOf(gradingOf(Grading{.temperature = -1}));
    const std::array<float, 3> kMagenta = kWhiteOf(gradingOf(Grading{.tint = 1}));
    std::printf("warm %f %f %f cool %f %f %f magenta %f %f %f\n",
                kWarm[0],
                kWarm[1],
                kWarm[2],
                kCool[0],
                kCool[1],
                kCool[2],
                kMagenta[0],
                kMagenta[1],
                kMagenta[2]);
    RAWFRAME_EXPECT(kWarm[0] > kWarm[2] && kCool[2] > kCool[0] && kMagenta[1] < kMagenta[0] &&
                    kMagenta[1] < kMagenta[2]);
    // Not yet set (all nought), or not finite: no grade; a negative
    // saturation is none.
    RAWFRAME_EXPECT(!gradingOf(Grading{.powerR = 0, .powerG = 0, .powerB = 0}).enabled &&
                    !gradingOf(std::nullopt).enabled);
    RAWFRAME_EXPECT(!gradingOf(Grading{.slopeG = std::numeric_limits<float>::infinity()}).enabled);
    RAWFRAME_EXPECT(gradingOf(Grading{.saturation = -2}).saturation == 0);
    // A camera's grade reaches its frame.
    Rig rig;
    SceneCamera camera{.fovY = 1, .near = 0.1F, .aspect = 1};
    RAWFRAME_EXPECT(!rig.frame(camera).grading.enabled);
    camera.grading = Grading{.slopeR = 2};
    const SceneGrading kGraded = rig.frame(camera).grading;
    RAWFRAME_EXPECT(kGraded.enabled && kGraded.slope[0] == 2);
    // The camera's tonemapper, AgX for a number outside the set (D295).
    RAWFRAME_EXPECT(rig.frame(camera).tonemapper == Tonemapper::Agx);
    camera.tonemapper = 1;
    RAWFRAME_EXPECT(rig.frame(camera).tonemapper == Tonemapper::PbrNeutral);
    camera.tonemapper = 2;
    RAWFRAME_EXPECT(rig.frame(camera).tonemapper == Tonemapper::Linear);
    camera.tonemapper = 7;
    RAWFRAME_EXPECT(rig.frame(camera).tonemapper == Tonemapper::Agx);
}
