// The scene's CPU half: the engine's meshes are valid, closed, and face
// out; a model is placed relative to the eye as its pose and scale put it,
// even far from the origin; the projection is reversed-Z with no far
// plane; what is off the view, draws nothing, is malformed, or names an
// unknown mesh is left out and counted; models draw in the order of their
// meshes, then entities, and the limit leaves out a suffix of it; the sun
// and sky light in linear physical units; a model's material is found by
// its identity, read again in place of those it was made with (D734),
// and translucent ones come last; an emitter emits the way
// its pose turns it, placed relative to the eye; and a game's scene loads
// against its program. A camera's effects are camera_test's, the other
// lights' lights_test's.

#include "rawframe/physics3d/components.h"
#include "rawframe/render_scene/errors.h"
#include "rawframe/render_scene/scene.h"
#include "rawframe/test/test.h"
#include "scene_rig.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>
#include <memory>
#include <numbers>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

using namespace rawframe;
using namespace rawframe::render_scene;
using namespace rawframe::render_scene::test_rig;

namespace {

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

RAWFRAME_TEST(TheEnginesGridIsAFlatSquareFacingUp) {
    // For a surface its material moves (D691): every vertex on the square
    // of half sides one, a cell a 128th of its side, every face up.
    const auto kGridMesh = engineMesh(kGrid);
    RAWFRAME_EXPECT(kGridMesh != nullptr && mesh::validate(*kGridMesh).has_value());
    if (kGridMesh == nullptr) {
        return;
    }
    constexpr std::size_t kAcross = kGridCells + 1;
    RAWFRAME_EXPECT(kGridMesh->positions.size() == kAcross * kAcross &&
                    kGridMesh->indices.size() == 6 * std::size_t{kGridCells} * kGridCells);
    for (std::size_t vertex = 0; vertex < kGridMesh->positions.size(); ++vertex) {
        const mesh::Vector3& kAt = kGridMesh->positions[vertex];
        RAWFRAME_EXPECT(kAt[1] == 0 && std::abs(kAt[0]) <= 1 && std::abs(kAt[2]) <= 1 &&
                        (kGridMesh->normals[vertex] == mesh::Vector3{0, 1, 0}));
    }
    RAWFRAME_EXPECT((kGridMesh->positions.front() == mesh::Vector3{-1, 0, -1}) &&
                    (kGridMesh->positions.back() == mesh::Vector3{1, 0, 1}));
    for (std::size_t at = 0; at + 2 < kGridMesh->indices.size(); at += 3) {
        const mesh::Vector3& kA = kGridMesh->positions[kGridMesh->indices[at]];
        const mesh::Vector3& kB = kGridMesh->positions[kGridMesh->indices[at + 1]];
        const mesh::Vector3& kC = kGridMesh->positions[kGridMesh->indices[at + 2]];
        // The face's normal's height: (B - A) x (C - A), its y.
        const float kUp = ((kB[2] - kA[2]) * (kC[0] - kA[0])) - ((kB[0] - kA[0]) * (kC[2] - kA[2]));
        RAWFRAME_EXPECT(kUp > 0);
    }
}

RAWFRAME_TEST(AFrameShowsThePresentedWorldsTimeAndTheOneBefore) {
    // What a material moving its vertices reads (D691): the time a scene
    // was told, and the one its frame before showed; the eye's place.
    Rig rig;
    rig.scene->shownAt(2.5);
    const SceneFrame& kFirst = rig.frame({.eye = {4, 5, 6}});
    RAWFRAME_EXPECT(kFirst.time == (std::array<double, 2>{2.5, 2.5}) && kFirst.eye == (std::array<double, 3>{4, 5, 6}));
    rig.scene->shownAt(2.75);
    const SceneFrame& kNext = rig.frame({});
    RAWFRAME_EXPECT(kNext.time == (std::array<double, 2>{2.75, 2.5}));
    rig.scene->shownAt(std::numeric_limits<double>::quiet_NaN());
    RAWFRAME_EXPECT(rig.frame({}).time == (std::array<double, 2>{0, 2.75}));
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
    // The default sun is the Sun's size, half a degree (D347).
    RAWFRAME_EXPECT(near(kDefault.lights.sunWidth, 0.0093F, 0.0001F));
    // The default ground gives back a fifth of the sun's light at its
    // height and the sky's (D304).
    const float kFifth = std::pow((0x7C / 255.0F + 0.055F) / 1.055F, 2.4F);
    RAWFRAME_EXPECT(
        near(kDefault.lights.ground[0],
             kFifth * ((100000 * kDefault.lights.toSun[1] / std::numbers::pi_v<float>)+kDefault.lights.sky[0]),
             1));
    const world::EntityHandle kLight = *rig.world.create();
    Sun sun{
        .directionX = 0, .directionY = -2, .directionZ = 0, .illuminance = 1000, .color = 0x808080FF, .angle = 0.2F};
    Sky sky{.luminance = 10, .color = 0x0000FFFF};
    RAWFRAME_EXPECT(rig.world.insertErased(kLight, *rig.schema->find(kSunId), &sun).has_value());
    RAWFRAME_EXPECT(rig.world.insertErased(kLight, *rig.schema->find(kSkyId), &sky).has_value());
    const SceneFrame& kLit = rig.frame({});
    // sRGB 0x80 is about 0.216 linear.
    RAWFRAME_EXPECT(near(kLit.lights.toSun[1], 1) && near(kLit.lights.sun[0], 215.86F, 0.1F) &&
                    near(kLit.lights.sky[0], 0) && near(kLit.lights.sky[2], 10));
    // Its width is twice the tangent of half its angle.
    RAWFRAME_EXPECT(near(kLit.lights.sunWidth, 2 * std::tan(0.1F), 0.0001F));
    // A ground giving back nothing is black below the horizon (D304).
    RAWFRAME_EXPECT(kLit.lights.ground == (std::array<float, 3>{0, 0, 0}));
    sky.ground = 0xFFFFFFFF;
    RAWFRAME_EXPECT(rig.world.insertErased(kLight, *rig.schema->find(kSkyId), &sky).has_value());
    const SceneFrame& kWhite = rig.frame({});
    RAWFRAME_EXPECT(near(kWhite.lights.ground[2], (215.86F / std::numbers::pi_v<float>)+10, 0.1F));
    // The sky's picture is named through to the frame (D322); none without.
    RAWFRAME_EXPECT(kWhite.lights.environment == 0);
    sky.environment = 0x5eed5eed5eed5eedULL;
    RAWFRAME_EXPECT(rig.world.insertErased(kLight, *rig.schema->find(kSkyId), &sky).has_value());
    RAWFRAME_EXPECT(rig.frame({}).lights.environment == 0x5eed5eed5eed5eedULL);
    // A sun not finite gives no light rather than a poisoned frame.
    Sun broken{.illuminance = std::numeric_limits<float>::infinity()};
    RAWFRAME_EXPECT(rig.world.insertErased(kLight, *rig.schema->find(kSunId), &broken).has_value());
    RAWFRAME_EXPECT(rig.frame({}).lights.sun[0] == 0);
    // A sun's angle is held to twenty degrees, and one not finite is a point.
    broken = Sun{.illuminance = 1, .angle = 3};
    RAWFRAME_EXPECT(rig.world.insertErased(kLight, *rig.schema->find(kSunId), &broken).has_value());
    RAWFRAME_EXPECT(near(rig.frame({}).lights.sunWidth, 2 * std::tan(0.175F), 0.0001F));
    broken.angle = std::numeric_limits<float>::quiet_NaN();
    RAWFRAME_EXPECT(rig.world.insertErased(kLight, *rig.schema->find(kSunId), &broken).has_value());
    RAWFRAME_EXPECT(rig.frame({}).lights.sunWidth == 0);
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
        "        grades: [model.Grading], probes: [model.ReflectionProbe], occlusions: [model.AmbientOcclusion],\n"
        "        blooms: [model.Bloom], mirrors: [model.ScreenSpaceReflections], blurs: [model.MotionBlur],\n"
        "        focuses: [model.DepthOfField], contacts: [model.ContactShadows], marks: [model.Decal],\n"
        "        processes: [model.PostProcess], fountains: [model.ParticleEmitter], trails: [model.Trail],\n"
        "        beams: [model.Beam]) {\n}\n";
    const std::string kModel = "component 3c8e1f52-7d04-4a2b-9e61-0f5a2c7d3b18 shown.look rawframe.model.Model\n";
    const std::string kLights = "component 5c8e1f52-7d04-4a2b-9e61-0f5a2c7d3b18 shown.sun rawframe.model.Sun\n"
                                "component 6c8e1f52-7d04-4a2b-9e61-0f5a2c7d3b18 shown.sky rawframe.model.Sky\n";
    const std::string kLamps = "component 9c8e1f52-7d04-4a2b-9e61-0f5a2c7d3b18 shown.lamp rawframe.model.PointLight\n"
                               "component ac8e1f52-7d04-4a2b-9e61-0f5a2c7d3b18 shown.torch rawframe.model.SpotLight\n"
                               "component dc8e1f52-7d04-4a2b-9e61-0f5a2c7d3b18 shown.probe "
                               "rawframe.model.ReflectionProbe\n";
    const std::string kView =
        "component 7c8e1f52-7d04-4a2b-9e61-0f5a2c7d3b18 shown.view rawframe.model.Camera\n"
        "component 7f8e1f52-7d04-4a2b-9e61-0f5a2c7d3b18 shown.lens rawframe.model.Camera\n"
        "component bc8e1f52-7d04-4a2b-9e61-0f5a2c7d3b18 shown.meter rawframe.model.AutoExposure\n"
        "component cc8e1f52-7d04-4a2b-9e61-0f5a2c7d3b18 shown.grade rawframe.model.Grading\n"
        "component ec8e1f52-7d04-4a2b-9e61-0f5a2c7d3b18 shown.hidden "
        "rawframe.model.AmbientOcclusion\n"
        "component fc8e1f52-7d04-4a2b-9e61-0f5a2c7d3b18 shown.bloom rawframe.model.Bloom\n"
        "component 0d8e1f52-7d04-4a2b-9e61-0f5a2c7d3b18 shown.mirror "
        "rawframe.model.ScreenSpaceReflections\n"
        "component 1d8e1f52-7d04-4a2b-9e61-0f5a2c7d3b18 shown.blur rawframe.model.MotionBlur\n"
        "component 2d8e1f52-7d04-4a2b-9e61-0f5a2c7d3b18 shown.focus rawframe.model.DepthOfField\n"
        "component 3d8e1f52-7d04-4a2b-9e61-0f5a2c7d3b18 shown.contact rawframe.model.ContactShadows\n"
        "component 4d8e1f52-7d04-4a2b-9e61-0f5a2c7d3b18 shown.mark rawframe.model.Decal\n"
        "component 5d8e1f52-7d04-4a2b-9e61-0f5a2c7d3b18 shown.warm rawframe.model.PostProcess\n"
        "component 6d8e1f52-7d04-4a2b-9e61-0f5a2c7d3b18 shown.fade rawframe.model.PostProcess\n"
        "component 7d8e1f52-7d04-4a2b-9e61-0f5a2c7d3b18 shown.fountain rawframe.model.ParticleEmitter\n"
        "component 8d8e1f52-7d04-4a2b-9e61-0f5a2c7d3b18 shown.trail rawframe.model.Trail\n"
        "component 9d8e1f52-7d04-4a2b-9e61-0f5a2c7d3b18 shown.beam rawframe.model.Beam\n";
    const auto kLoaded = kLoad(kUses, kModel + kLights + kView + kLamps);
    RAWFRAME_EXPECT(
        kLoaded.has_value() && kLoaded->models == (std::vector<schema::ComponentTypeId>{kModelId}) &&
        kLoaded->sun == kSunId && kLoaded->sky == kSkyId &&
        kLoaded->cameras == (std::vector<schema::ComponentTypeId>{
                                schema::ComponentTypeId::fromText("7c8e1f52-7d04-4a2b-9e61-0f5a2c7d3b18"),
                                schema::ComponentTypeId::fromText("7f8e1f52-7d04-4a2b-9e61-0f5a2c7d3b18")}) &&
        kLoaded->meshes.empty() && kLoaded->points == (std::vector<schema::ComponentTypeId>{kPointId}) &&
        kLoaded->spots == (std::vector<schema::ComponentTypeId>{kSpotId}) &&
        kLoaded->probes == (std::vector<schema::ComponentTypeId>{kProbeId}) &&
        kLoaded->autoExposure == schema::ComponentTypeId::fromText("bc8e1f52-7d04-4a2b-9e61-0f5a2c7d3b18") &&
        kLoaded->grading == schema::ComponentTypeId::fromText("cc8e1f52-7d04-4a2b-9e61-0f5a2c7d3b18") &&
        kLoaded->occlusion == schema::ComponentTypeId::fromText("ec8e1f52-7d04-4a2b-9e61-0f5a2c7d3b18") &&
        kLoaded->bloom == schema::ComponentTypeId::fromText("fc8e1f52-7d04-4a2b-9e61-0f5a2c7d3b18") &&
        kLoaded->reflections == schema::ComponentTypeId::fromText("0d8e1f52-7d04-4a2b-9e61-0f5a2c7d3b18") &&
        kLoaded->motionBlur == schema::ComponentTypeId::fromText("1d8e1f52-7d04-4a2b-9e61-0f5a2c7d3b18") &&
        kLoaded->depthOfField == schema::ComponentTypeId::fromText("2d8e1f52-7d04-4a2b-9e61-0f5a2c7d3b18") &&
        kLoaded->contactShadows == schema::ComponentTypeId::fromText("3d8e1f52-7d04-4a2b-9e61-0f5a2c7d3b18") &&
        kLoaded->decals == (std::vector<schema::ComponentTypeId>{kDecalId}) &&
        kLoaded->postProcesses == (std::vector<schema::ComponentTypeId>{
                                      schema::ComponentTypeId::fromText("5d8e1f52-7d04-4a2b-9e61-0f5a2c7d3b18"),
                                      schema::ComponentTypeId::fromText("6d8e1f52-7d04-4a2b-9e61-0f5a2c7d3b18")}) &&
        kLoaded->emitters == (std::vector<schema::ComponentTypeId>{
                                 schema::ComponentTypeId::fromText("7d8e1f52-7d04-4a2b-9e61-0f5a2c7d3b18")}) &&
        kLoaded->trails == (std::vector<schema::ComponentTypeId>{
                               schema::ComponentTypeId::fromText("8d8e1f52-7d04-4a2b-9e61-0f5a2c7d3b18")}) &&
        kLoaded->beams == (std::vector<schema::ComponentTypeId>{
                              schema::ComponentTypeId::fromText("9d8e1f52-7d04-4a2b-9e61-0f5a2c7d3b18")}));
    const auto kPlain = kLoad(kUses, kModel);
    RAWFRAME_EXPECT(kPlain.has_value() && kPlain->cameras.empty() && !kPlain->sun && !kPlain->sky);
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
                    kCheap.temporal.jitter == (std::array<float, 2>{0, 0}) && kCheap.samples == 1);
    // Multisampling takes its samples in the models' passes alone: no
    // jitter, no history, no FXAA (D343).
    auto sampled =
        *Scene::create(*rig.schema, {.models = {kModelId}, .antiAliasing = AntiAliasing::Msaa, .multisamples = 2});
    sampled->extract(rig.world);
    const SceneFrame& kSampled = sampled->queue(kCamera);
    RAWFRAME_EXPECT(kSampled.samples == 2 && !kSampled.fxaa && !kSampled.temporal.enabled &&
                    kSampled.temporal.jitter == (std::array<float, 2>{0, 0}) && kPlain.samples == 1);
}

RAWFRAME_TEST(AModelsMaterialIsFoundByItsIdentity) {
    // A game with two materials, the second given twice: the later wins.
    MaterialBlob brass = noMaterial();
    brass[3] = 1;
    MaterialBlob stone = noMaterial();
    stone[7] = 0.9F;
    MaterialBlob stoner = stone;
    stoner[7] = 1;
    auto schema = registry();
    world::World world{schema};
    auto scene = *Scene::create(
        *schema,
        {.models = {kModelId},
         .materials = {{.id = 0xa1, .blob = brass}, {.id = 0xa2, .blob = stone}, {.id = 0xa2, .blob = stoner}}});
    for (const std::uint64_t kMaterial : {0xa1ULL, 0ULL, 0xa2ULL, 0xffULL}) {
        const world::EntityHandle kEntity = *world.create();
        Model model{.mesh = kBox, .material = kMaterial};
        RAWFRAME_EXPECT(world.insertErased(kEntity, *schema->find(kModelId), &model).has_value());
        RAWFRAME_EXPECT(world
                            .insert(kEntity,
                                    *schema->key<physics3d::Pose3D>(),
                                    physics3d::Pose3D{.z = -10.0 - static_cast<double>(kMaterial & 1U), .qw = 1})
                            .has_value());
    }
    scene->extract(world);
    const SceneFrame& kFrame = scene->queue({.fovY = 1, .near = 0.1F, .aspect = 1});
    RAWFRAME_EXPECT(kFrame.materials.size() == 3 && kFrame.materials[0] == noMaterial() &&
                    kFrame.materials[1] == brass && kFrame.materials[2] == stoner);
    // None's, the brass, the stone, and one the game has not, drawn with
    // none and counted.
    std::vector<std::uint32_t> places;
    for (const SceneDraw& draw : kFrame.draws) {
        places.push_back(draw.material);
    }
    std::ranges::sort(places);
    RAWFRAME_EXPECT(kFrame.draws.size() == 4 && places == (std::vector<std::uint32_t>{0, 0, 1, 2}) &&
                    kFrame.unknownMaterials == 1);
    // None is white, so a model's color is its base color.
    RAWFRAME_EXPECT(noMaterial()[0] == 1 && noMaterial()[7] == 0.3F && noMaterial()[15] == 0);
    // Read again (D734): the stone changed and the brass gone, the frames
    // from the next draw the new stone, and the brass's models with none.
    scene->replaceMaterials({{.id = 0xa2, .blob = brass}}, {});
    scene->extract(world);
    const SceneFrame& kAgain = scene->queue({.fovY = 1, .near = 0.1F, .aspect = 1});
    places.clear();
    for (const SceneDraw& draw : kAgain.draws) {
        places.push_back(draw.material);
    }
    std::ranges::sort(places);
    RAWFRAME_EXPECT(kAgain.materials.size() == 2 && kAgain.materials[1] == brass &&
                    places == (std::vector<std::uint32_t>{0, 0, 0, 1}) && kAgain.unknownMaterials == 2);
}

RAWFRAME_TEST(AMeshsPartsDrawWithTheirOwnMaterials) {
    // Four parts: two of the brass, then one of none, then one of the stone.
    auto crate = std::make_shared<mesh::Mesh>(*engineMesh(kBox));
    const auto kThird = static_cast<std::uint32_t>(crate->indices.size() / 3);
    const auto kSixth = kThird / 2;
    crate->parts = {{.firstIndex = 0, .indexCount = kSixth, .material = 0xa1},
                    {.firstIndex = kSixth, .indexCount = kSixth, .material = 0xa1},
                    {.firstIndex = kThird, .indexCount = kThird, .material = 0},
                    {.firstIndex = 2 * kThird,
                     .indexCount = static_cast<std::uint32_t>(crate->indices.size()) - (2 * kThird),
                     .material = 0xa2}};
    constexpr std::uint64_t kCrate = 0xc4a7e;
    auto schema = registry();
    world::World world{schema};
    auto scene =
        *Scene::create(*schema,
                       {.models = {kModelId},
                        .meshes = {{.id = kCrate, .mesh = crate}},
                        .materials = {{.id = 0xa1, .blob = noMaterial()}, {.id = 0xa2, .blob = noMaterial()}}});
    const auto kPlace = [&](std::uint64_t material, double z) {
        const world::EntityHandle kEntity = *world.create();
        Model model{.mesh = kCrate, .material = material};
        RAWFRAME_EXPECT(world.insertErased(kEntity, *schema->find(kModelId), &model).has_value());
        RAWFRAME_EXPECT(
            world.insert(kEntity, *schema->key<physics3d::Pose3D>(), physics3d::Pose3D{.z = z, .qw = 1}).has_value());
    };
    kPlace(0, -10);
    scene->extract(world);
    const SceneFrame& kOwn = scene->queue({.fovY = 1, .near = 0.1F, .aspect = 1});
    // A draw for each run of one material, the two brass parts joined.
    std::vector<std::tuple<std::uint32_t, std::uint32_t, std::uint32_t>> runs;
    for (const SceneDraw& draw : kOwn.draws) {
        runs.emplace_back(draw.firstIndex, draw.indexCount, draw.material);
    }
    std::ranges::sort(runs);
    RAWFRAME_EXPECT(kOwn.drawn == 1 &&
                    runs == (std::vector<std::tuple<std::uint32_t, std::uint32_t, std::uint32_t>>{
                                {0, kThird, 1},
                                {kThird, kThird, 0},
                                {2 * kThird, static_cast<std::uint32_t>(crate->indices.size()) - (2 * kThird), 2}}));

    // A Model naming a material draws every part with it, in one draw.
    kPlace(0xa2, -12);
    scene->extract(world);
    const SceneFrame& kBoth = scene->queue({.fovY = 1, .near = 0.1F, .aspect = 1});
    RAWFRAME_EXPECT(kBoth.drawn == 2 && kBoth.draws.size() == 4);
    RAWFRAME_EXPECT(std::ranges::count_if(kBoth.draws, [&](const SceneDraw& draw) {
                        return draw.firstIndex == 0 && draw.indexCount == crate->indices.size() && draw.material == 2;
                    }) == 1);
    RAWFRAME_EXPECT(kBoth.unknownMaterials == 0);
}

RAWFRAME_TEST(TranslucentModelsComeLastFarthestFirst) {
    auto schema = registry();
    world::World world{schema};
    auto scene = *Scene::create(
        *schema,
        {.models = {kModelId},
         .materials = {{.id = 0xa1, .blob = noMaterial(), .translucent = true}, {.id = 0xa2, .blob = noMaterial()}}});
    // Glass at 5, 15, and 10 meters; opaque boxes at 20 and 8, one of them
    // with an opaque material.
    const auto kPlace = [&](std::uint64_t material, double away) {
        const world::EntityHandle kEntity = *world.create();
        Model model{.mesh = kBox, .material = material};
        RAWFRAME_EXPECT(world.insertErased(kEntity, *schema->find(kModelId), &model).has_value());
        RAWFRAME_EXPECT(world.insert(kEntity, *schema->key<physics3d::Pose3D>(), {.z = -away, .qw = 1}).has_value());
    };
    kPlace(0xa1, 5);
    kPlace(0, 20);
    kPlace(0xa1, 15);
    kPlace(0xa2, 8);
    kPlace(0xa1, 10);
    scene->extract(world);
    const SceneFrame& kFrame = scene->queue({.fovY = 1, .near = 0.1F, .aspect = 1});
    RAWFRAME_EXPECT(kFrame.draws.size() == 5 && kFrame.translucent == 3);
    if (kFrame.draws.size() != 5) {
        return;
    }
    RAWFRAME_EXPECT(kFrame.draws[0].material != 1 && kFrame.draws[1].material != 1);
    RAWFRAME_EXPECT(near(kFrame.draws[2].model[14], -15) && near(kFrame.draws[3].model[14], -10) &&
                    near(kFrame.draws[4].model[14], -5));
    // The glass's blob says it blends, so its shadow is cut by what it
    // covers (D726); the opaque material's does not.
    const auto kBlends = [&kFrame](std::uint32_t at) {
        return (static_cast<std::uint32_t>(kFrame.materials[at][kFlagsAt]) & kBlendsFlag) != 0;
    };
    RAWFRAME_EXPECT(kFrame.materials.size() == 3 && kBlends(1) && !kBlends(2) && !kBlends(0));
}

RAWFRAME_TEST(OpaqueModelsAreGroupedByTheirMaterialsTexture) {
    auto schema = registry();
    world::World world{schema};
    const SceneTexture kTiles{.id = 0x77, .filter = material::Filter::Nearest};
    const SceneTexture kBark{.id = 0x55, .address = material::Address::Clamp};
    auto scene = *Scene::create(*schema,
                                {.models = {kModelId},
                                 .materials = {{.id = 0xa1, .blob = noMaterial(), .textures = {.base = kTiles}},
                                               {.id = 0xa2, .blob = noMaterial(), .textures = {.packed = kBark}},
                                               {.id = 0xa3, .blob = noMaterial()}}});
    // Boxes and spheres of each material, in no order.
    std::uint32_t at = 0;
    for (const std::uint64_t kMaterial : {0xa1ULL, 0xa3ULL, 0xa2ULL, 0xa1ULL, 0xa2ULL, 0xa3ULL}) {
        for (const std::uint64_t kMesh : {kSphere, kBox}) {
            const world::EntityHandle kEntity = *world.create();
            Model model{.mesh = kMesh, .material = kMaterial};
            RAWFRAME_EXPECT(world.insertErased(kEntity, *schema->find(kModelId), &model).has_value());
            RAWFRAME_EXPECT(world
                                .insert(kEntity,
                                        *schema->key<physics3d::Pose3D>(),
                                        {.x = static_cast<double>(at++ % 4) - 2, .z = -12, .qw = 1})
                                .has_value());
        }
    }
    scene->extract(world);
    const SceneFrame& kFrame = scene->queue({.fovY = 1.5F, .near = 0.1F, .aspect = 1});
    // Each material's texture at its blob's place: none's, then the game's.
    RAWFRAME_EXPECT(kFrame.textures == (std::vector<SceneTextures>{{}, {.base = kTiles}, {.packed = kBark}, {}}));
    // No texture, then bark, then tiles; by mesh within each.
    std::vector<std::pair<SceneTextures, std::uint64_t>> order;
    for (const SceneDraw& draw : kFrame.draws) {
        order.emplace_back(kFrame.textures[draw.material], draw.mesh);
    }
    RAWFRAME_EXPECT(kFrame.draws.size() == 12 && std::ranges::is_sorted(order));
}

RAWFRAME_TEST(AnEmitterEmitsTheWayItsPoseTurnsIt) {
    // Far from the origin, so its anchor is placed relative to the eye.
    constexpr double kX = 100000;
    Rig rig;
    const auto kPlace = [&rig](double z, std::array<float, 4> turn) {
        particles::ParticleEmitter emitter{.rate = 10, .lifetime = 1, .speed = 1, .sizeStart = 0.1F};
        const world::EntityHandle kEntity = *rig.world.create();
        RAWFRAME_EXPECT(rig.world.insertErased(kEntity, *rig.schema->find(kEmitterId), &emitter).has_value());
        const physics3d::Pose3D kPose{
            .x = kX, .y = 0, .z = z, .qx = turn[0], .qy = turn[1], .qz = turn[2], .qw = turn[3]};
        RAWFRAME_EXPECT(rig.world.insert(kEntity, *rig.schema->key<physics3d::Pose3D>(), kPose).has_value());
    };
    // Ahead, turned a quarter about the view's axis so its up is the
    // World's -X; and behind the eye, out of view.
    const float kHalfTurn = std::sqrt(0.5F);
    kPlace(-10, {0, 0, kHalfTurn, kHalfTurn});
    kPlace(30, {0, 0, 0, 1});
    const SceneFrame& kFrame = rig.frame({.eye = {kX, 0, 0}, .fovY = 1.2F, .aspect = 1});
    RAWFRAME_EXPECT(kFrame.particles.emitters.size() == 1 && kFrame.particles.emittersLeftOut == 0);
    if (kFrame.particles.emitters.size() == 1) {
        const particles::EmitterDraw& kDrawn = kFrame.particles.emitters[0];
        RAWFRAME_EXPECT(near(kDrawn.direction[0], -1) && near(kDrawn.direction[1], 0));
        RAWFRAME_EXPECT(near(kDrawn.anchor[0], 0) && near(kDrawn.anchor[2], -10));
    }
}
