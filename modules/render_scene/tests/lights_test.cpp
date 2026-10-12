// The scene's lights on the CPU: the sun's cascades cover the view and
// hold still as the eye moves; point and spot lights light in candela, are
// culled by their reach, and name the clusters they reach; punctual
// shadows share one atlas by cover; reflection probes in view are ordered
// as a point takes them and clustered; and decals in view are placed into
// their boxes and clustered.

#include "rawframe/physics3d/components.h"
#include "rawframe/render_scene/scene.h"
#include "rawframe/test/test.h"
#include "scene_rig.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <memory>
#include <utility>
#include <vector>

using namespace rawframe;
using namespace rawframe::render_scene;
using namespace rawframe::render_scene::test_rig;

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
    RAWFRAME_EXPECT(kFrame.draws.size() == 2);
    // Each cascade names its own casters, one run after another (D298):
    // the far box in none, the others where their spheres reach.
    std::uint32_t named = 0;
    std::array<std::uint32_t, 4> ahead{};
    std::array<std::uint32_t, 4> behind{};
    for (std::size_t at = 0; at < kShadows.count; ++at) {
        const ShadowCascade& kCascade = kShadows.cascades[at];
        RAWFRAME_EXPECT(kCascade.firstCaster == named);
        named += kCascade.casterCount;
        for (std::uint32_t caster = 0; caster < kCascade.casterCount; ++caster) {
            const float kZ = kShadows.casters[kCascade.firstCaster + caster].model[14];
            RAWFRAME_EXPECT(near(kZ, -10) || near(kZ, 10));
            (kZ < 0 ? ahead : behind)[at] += 1;
        }
    }
    std::printf("cascades' casters ahead %u %u %u %u, behind %u %u %u %u\n",
                ahead[0],
                ahead[1],
                ahead[2],
                ahead[3],
                behind[0],
                behind[1],
                behind[2],
                behind[3]);
    // The sun is high: the box ahead reaches even the first cascade, which
    // ends 5.5 m ahead; the one behind, twenty meters from the first two's
    // spheres, only the last two.
    RAWFRAME_EXPECT(named == kShadows.casters.size() && ahead == (std::array<std::uint32_t, 4>{1, 1, 1, 1}) &&
                    behind == (std::array<std::uint32_t, 4>{0, 0, 1, 1}));
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
    RAWFRAME_EXPECT(kClusters.ranges.size() == std::size_t{kClusters.tilesX} * kClusters.tilesY * kClusters.slices * 4);
    const auto kNames = [&kClusters](std::uint32_t x, std::uint32_t y, std::uint32_t slice, std::uint32_t light) {
        const std::size_t kCluster = (((std::size_t{slice} * kClusters.tilesY) + y) * kClusters.tilesX) + x;
        const std::uint32_t kFirst = kClusters.ranges[kCluster * 4];
        const std::uint32_t kCount = kClusters.ranges[(kCluster * 4) + 1];
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
    for (std::size_t at = 1; at < kCrowded.clusters.ranges.size(); at += 4) {
        RAWFRAME_EXPECT(kCrowded.clusters.ranges[at] <= 3);
    }
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

RAWFRAME_TEST(ProbesInViewAreOrderedAsAPointTakesThemAndClustered) {
    // Far from the origin, so the boxes are placed relative to the eye.
    constexpr double kX = 100000;
    const auto kAt = [](double x, double y, double z) {
        return physics3d::Pose3D{.x = kX + x, .y = y, .z = z, .qw = 1};
    };
    const auto kPlace = [&kAt](Rig& rig, ReflectionProbe probe, double x, double z) {
        const world::EntityHandle kEntity = *rig.world.create();
        RAWFRAME_EXPECT(rig.world.insertErased(kEntity, *rig.schema->find(kProbeId), &probe).has_value());
        RAWFRAME_EXPECT(rig.world.insert(kEntity, *rig.schema->key<physics3d::Pose3D>(), kAt(x, 0, z)).has_value());
    };
    const auto kRoom = [&kPlace](Rig& rig) {
        // A hall; a closet in it, smaller; a stage overlapping the hall's
        // side, of a higher priority; and three that are no probe in view:
        // one with no picture, one with no size, and one behind the eye.
        kPlace(rig, {.halfX = 10, .halfY = 10, .halfZ = 10, .intensity = 1, .environment = 0xe1}, 0, -10);
        kPlace(rig, {.halfX = 2, .halfY = 2, .halfZ = 2, .intensity = 1, .environment = 0xe2}, 0, -10);
        kPlace(rig, {.halfX = 3, .halfY = 3, .halfZ = 3, .intensity = 2, .priority = 1, .environment = 0xe3}, 8, -10);
        kPlace(rig, {.halfX = 50, .halfY = 50, .halfZ = 50, .intensity = 1}, 0, 0);
        kPlace(rig, {.halfX = 50, .halfY = 0, .halfZ = 50, .intensity = 1, .environment = 0xe4}, 0, 0);
        kPlace(rig, {.halfX = 2, .halfY = 2, .halfZ = 2, .intensity = 1, .environment = 0xe5}, 0, 60);
    };
    Rig rig;
    kRoom(rig);
    rig.spawn(Model{.mesh = kBox, .color = 0xFF0000FF}, kAt(0, 0, -10));
    SceneFrame frame = rig.frame({.eye = {kX, 0, 0}, .fovY = 1.5F});
    RAWFRAME_EXPECT(frame.probes.size() == 3 && frame.probesOverLimit == 0);
    if (frame.probes.size() != 3) {
        return;
    }
    // The higher priority first, then the smaller box.
    RAWFRAME_EXPECT(frame.probes[0].environment == 0xe3 && frame.probes[1].environment == 0xe2 &&
                    frame.probes[2].environment == 0xe1 && near(frame.probes[0].intensity, 2));
    // Placed relative to the eye, as the draws are.
    for (const SceneProbe& kProbe : frame.probes) {
        RAWFRAME_EXPECT(near(kProbe.position[2], -10) && std::abs(kProbe.position[0]) <= 8);
    }
    // Each is named by clusters, after the lights and the decals there, in
    // the order a point takes them; the closet's clusters name the hall too.
    std::array<std::size_t, 3> named{};
    bool ordered = true;
    bool shared = false;
    for (std::size_t at = 0; at + 3 < frame.clusters.ranges.size(); at += 4) {
        const std::size_t kFirst =
            frame.clusters.ranges[at] + frame.clusters.ranges[at + 1] + frame.clusters.ranges[at + 2];
        std::vector<std::uint32_t> held;
        for (std::size_t each = 0; each < frame.clusters.ranges[at + 3]; ++each) {
            held.push_back(frame.clusters.indices[kFirst + each]);
            named[held.back()] += 1;
        }
        ordered = ordered && std::ranges::is_sorted(held);
        shared = shared || (std::ranges::contains(held, 1U) && std::ranges::contains(held, 2U));
    }
    RAWFRAME_EXPECT(ordered && shared && named[0] > 0 && named[1] > 0 && named[2] > named[1]);
    // As a probe's bake sees the scene: none, in the list or the clusters.
    withoutProbes(frame);
    std::size_t left = 0;
    for (std::size_t at = 3; at < frame.clusters.ranges.size(); at += 4) {
        left += frame.clusters.ranges[at];
    }
    RAWFRAME_EXPECT(frame.probes.empty() && left == 0);
    // Over the limit, the farthest from the eye are left out and counted.
    Rig few{{.maximumProbes = 2}};
    kRoom(few);
    const SceneFrame& kFew = few.frame({.eye = {kX, 0, 0}, .fovY = 1.5F});
    RAWFRAME_EXPECT(kFew.probes.size() == 2 && kFew.probesOverLimit == 1 && kFew.probes[0].environment == 0xe2 &&
                    kFew.probes[1].environment == 0xe1);
}

RAWFRAME_TEST(DecalsInViewArePlacedIntoTheirBoxesAndClustered) {
    // Far from the origin, so the boxes are placed relative to the eye.
    constexpr double kX = 100000;
    const auto kPlace = [](Rig& rig, Decal decal, double x, double z, std::array<float, 4> turn) {
        const world::EntityHandle kEntity = *rig.world.create();
        RAWFRAME_EXPECT(rig.world.insertErased(kEntity, *rig.schema->find(kDecalId), &decal).has_value());
        const physics3d::Pose3D kPose{
            .x = kX + x, .y = 0, .z = z, .qx = turn[0], .qy = turn[1], .qz = turn[2], .qw = turn[3]};
        RAWFRAME_EXPECT(rig.world.insert(kEntity, *rig.schema->key<physics3d::Pose3D>(), kPose).has_value());
    };
    const float kHalfTurn = std::sqrt(0.5F);
    const auto kYard = [&](Rig& rig) {
        // Ahead, square to the World, with normals and a roughness past
        // one; ahead, turned a quarter about up; and four that are no decal
        // in view: behind the eye, with no texture, with no depth, and with
        // a roughness not a number.
        kPlace(rig,
               {.halfX = 1,
                .halfY = 1,
                .halfZ = 0.5F,
                .color = 0xFF000080,
                .texture = 0xd1,
                .normal = 0xb1,
                .roughness = 2},
               0,
               -10,
               {0, 0, 0, 1});
        kPlace(rig, {.halfX = 2, .halfY = 1, .halfZ = 1, .texture = 0xd2}, 5, -10, {0, kHalfTurn, 0, kHalfTurn});
        kPlace(rig, {.halfX = 1, .halfY = 1, .halfZ = 1, .texture = 0xd3}, 0, 30, {0, 0, 0, 1});
        kPlace(rig, {.halfX = 1, .halfY = 1, .halfZ = 1}, 0, -10, {0, 0, 0, 1});
        kPlace(rig, {.halfX = 1, .halfY = 1, .halfZ = 0, .texture = 0xd4}, 0, -10, {0, 0, 0, 1});
        kPlace(rig,
               {.halfX = 1, .halfY = 1, .halfZ = 1, .texture = 0xd5, .roughness = std::nanf("")},
               0,
               -10,
               {0, 0, 0, 1});
    };
    Rig rig;
    kYard(rig);
    const SceneFrame& kFrame = rig.frame({.eye = {kX, 0, 0}, .fovY = 1.5F});
    RAWFRAME_EXPECT(kFrame.decals.size() == 2 && kFrame.decalsCulled == 4 && kFrame.decalsOverLimit == 0);
    if (kFrame.decals.size() != 2) {
        return;
    }
    const auto kInto = [](const SceneDecal& decal, std::array<float, 3> place) {
        std::array<float, 3> into{};
        for (std::size_t row = 0; row < 3; ++row) {
            into[row] = decal.toBox[12 + row];
            for (std::size_t column = 0; column < 3; ++column) {
                into[row] += decal.toBox[(column * 4) + row] * place[column];
            }
        }
        return into;
    };
    const SceneDecal& kSquare = kFrame.decals[0];
    const SceneDecal& kTurned = kFrame.decals[1];
    const auto kMiddle = kInto(kSquare, {0, 0, -10});
    const auto kSide = kInto(kSquare, {1, 0, -10});
    const auto kBack = kInto(kSquare, {0, 0, -10.5F});
    RAWFRAME_EXPECT(near(kMiddle[0], 0) && near(kMiddle[1], 0) && near(kMiddle[2], 0) && near(kSide[0], 1) &&
                    near(kBack[2], -1));
    // A quarter turn about up takes its +X to the World's -Z.
    const auto kTurnedSide = kInto(kTurned, {5, 0, -12});
    RAWFRAME_EXPECT(near(kTurnedSide[0], 1, 1e-3F) && near(kTurnedSide[1], 0, 1e-3F) && near(kTurnedSide[2], 0, 1e-3F));
    RAWFRAME_EXPECT(near(kSquare.color[0], 1) && near(kSquare.color[1], 0) && near(kSquare.color[3], 128.0F / 255) &&
                    kSquare.texture == 0xd1 && kTurned.texture == 0xd2);
    // Its normals' texture, and its roughness at most one; the other's
    // none, leaving the surface's.
    RAWFRAME_EXPECT(kSquare.normal == 0xb1 && kSquare.roughness == 1 && kTurned.normal == 0 && kTurned.roughness == 0);
    // Each is named by clusters, after the lights there.
    std::size_t named = 0;
    for (std::size_t at = 0; at + 3 < kFrame.clusters.ranges.size(); at += 4) {
        named += kFrame.clusters.ranges[at + 2];
    }
    RAWFRAME_EXPECT(named > 0);
    // Past the limit, the later are left out and counted.
    Rig few{{.maximumDecals = 1}};
    kYard(few);
    const SceneFrame& kFew = few.frame({.eye = {kX, 0, 0}, .fovY = 1.5F});
    RAWFRAME_EXPECT(kFew.decals.size() == 1 && kFew.decalsOverLimit == 1 && kFew.decals[0].texture == 0xd1);
}
