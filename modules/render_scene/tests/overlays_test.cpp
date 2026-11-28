// What an editor's preview draws over its scene: a box's twelve
// edges at its corners, turned as its pose turns; a sphere's three great
// circles; a mesh nothing; a point light's three circles of its range and
// a spot's way and the ring its cone reaches. Each body's lines are
// colored by what it is: static, kinematic, moving, at rest, or a
// sensor; and one touching another so, its hardest hit along its normal.
// What a game's present systems draw to debug it: each shape by its
// lines, on the channels asked for, until the scene's lines are full.

#include "../src/overlays.h"
#include "rawframe/test/test.h"

#include <cmath>
#include <numbers>

using namespace rawframe;
using namespace rawframe::render_scene;

namespace {

bool near(double value, double expected) {
    return std::abs(value - expected) < 1e-4;
}

} // namespace

RAWFRAME_TEST(ABoxIsOutlinedByItsTwelveEdges) {
    std::vector<SceneLine> lines;
    addColliderLines(physics3d::Body3D{.shape = 1, .width = 1, .height = 2, .depth = 3},
                     physics3d::Pose3D{.x = 10, .y = 0, .z = 0},
                     lines);
    RAWFRAME_EXPECT(lines.size() == 12);
    // Every end a corner ten along X, its half sides 1, 2, and 3.
    bool corners = true;
    for (const SceneLine& kLine : lines) {
        for (const auto& kEnd : {kLine.from, kLine.to}) {
            corners =
                corners && near(std::abs(kEnd[0] - 10), 1) && near(std::abs(kEnd[1]), 2) && near(std::abs(kEnd[2]), 3);
        }
    }
    RAWFRAME_EXPECT(corners && lines[0].ofView && !lines[0].over);
    // A quarter turn about Y: its depth along X.
    lines.clear();
    const float kHalf = std::sqrt(0.5F);
    addColliderLines(physics3d::Body3D{.shape = 1, .width = 1, .height = 2, .depth = 3},
                     physics3d::Pose3D{.qy = kHalf, .qw = kHalf},
                     lines);
    RAWFRAME_EXPECT(lines.size() == 12 && near(std::abs(lines[0].from[0]), 3) && near(std::abs(lines[0].from[2]), 1));
}

RAWFRAME_TEST(ABodysColorSaysHowItMoves) {
    const auto kBody = [](std::uint8_t motion) {
        return physics3d::Body3D{.motion = motion, .shape = 1};
    };
    const physics3d::Velocity3D kStill{};
    const physics3d::Velocity3D kFalling{.y = -2};
    const physics3d::Velocity3D kSpinning{.angularY = 1};
    const auto kStatic = colliderColor(kBody(0), std::nullopt);
    const auto kKinematic = colliderColor(kBody(1), kFalling);
    const auto kMoving = colliderColor(kBody(2), kFalling);
    const auto kResting = colliderColor(kBody(2), kStill);
    // Four colors, none alike.
    RAWFRAME_EXPECT(kStatic != kKinematic && kStatic != kMoving && kStatic != kResting && kKinematic != kMoving &&
                    kKinematic != kResting && kMoving != kResting);
    // Turning is moving; a body whose velocity is unknown is drawn moving.
    RAWFRAME_EXPECT(colliderColor(kBody(2), kSpinning) == kMoving);
    RAWFRAME_EXPECT(colliderColor(kBody(2), std::nullopt) == kMoving);
    // A sensor is a sensor however it moves, and its lines take the color.
    physics3d::Body3D sensor = kBody(2);
    sensor.sensor = true;
    RAWFRAME_EXPECT(colliderColor(sensor, kStill) == colliderColor(sensor, kFalling));
    std::vector<SceneLine> lines;
    addColliderLines(kBody(0), physics3d::Pose3D{}, lines, kStill);
    RAWFRAME_EXPECT(!lines.empty() && lines[0].color == kStatic);
}

RAWFRAME_TEST(ATouchingBodyIsDrawnSoAndItsHitAlongItsNormal) {
    const physics3d::Body3D kBody{.motion = 2, .shape = 1};
    physics3d::Contact3D touching{
        .touching = 1, .hit = world::EntityHandle{.slot = 3, .generation = 1}, .hitNormalY = 2};
    // Touching is told apart from moving and resting; a sensor stays one.
    RAWFRAME_EXPECT(colliderColor(kBody, std::nullopt, touching) != colliderColor(kBody, std::nullopt));
    physics3d::Body3D sensor = kBody;
    sensor.sensor = true;
    RAWFRAME_EXPECT(colliderColor(sensor, std::nullopt, touching) == colliderColor(sensor, std::nullopt));
    // The hit half a meter up from the body, however long its normal.
    std::vector<SceneLine> lines;
    addContactLines(physics3d::Pose3D{.x = 1, .y = 2, .z = 3}, touching, lines);
    RAWFRAME_EXPECT(lines.size() == 1 && near(lines[0].from[1], 2) && near(lines[0].to[1], 2.5) &&
                    near(lines[0].to[0], 1) && near(lines[0].to[2], 3));
    // Nothing hit, nothing drawn.
    lines.clear();
    touching.hit = world::EntityHandle{};
    addContactLines(physics3d::Pose3D{}, touching, lines);
    RAWFRAME_EXPECT(lines.empty());
}

RAWFRAME_TEST(OtherShapesAndLightsAreOutlined) {
    std::vector<SceneLine> lines;
    addColliderLines(physics3d::Body3D{.shape = 0, .sensor = true, .width = 2}, physics3d::Pose3D{}, lines);
    // Three circles of sixteen pieces, two meters out, a sensor's cyan.
    RAWFRAME_EXPECT(lines.size() == 48 && near(std::hypot(lines[0].from[0], lines[0].from[1], lines[0].from[2]), 2) &&
                    lines[0].color[2] == 1);
    lines.clear();
    addColliderLines(physics3d::Body3D{.shape = 4}, physics3d::Pose3D{}, lines);
    RAWFRAME_EXPECT(lines.empty());
    LightInstance lamp{.light = SpotLight{.range = 5}, .position = {0, 3, 0}};
    addLightLines(lamp, lines);
    RAWFRAME_EXPECT(lines.size() == 48 && near(lines[0].from[1], 3));
    // A spot with no turn shines down -Z: its way ends five meters along.
    lines.clear();
    lamp.spot = true;
    lamp.light.outer = static_cast<float>(std::numbers::pi / 4);
    addLightLines(lamp, lines);
    RAWFRAME_EXPECT(lines.size() == 17 && near(lines[0].to[2], -5) && near(lines[0].to[1], 3));
}

RAWFRAME_TEST(AGamesDebugShapesAreDrawnByTheirLines) {
    using Kind = world_kest::DebugShape::Kind;
    world_kest::PresentedDrawings drawings;
    RAWFRAME_EXPECT(drawings.add({.kind = Kind::Line, .a = {0, 0, 0}, .b = {1, 0, 0}, .color = 0xFF0000FF}));
    RAWFRAME_EXPECT(drawings.add({.kind = Kind::Box, .a = {0, 0, 0}, .b = {1, 2, 3}, .channel = 1}));
    RAWFRAME_EXPECT(drawings.add({.kind = Kind::Sphere, .a = {5, 0, 0}, .b = {2, 0, 0}, .channel = 2}));
    RAWFRAME_EXPECT(drawings.add({.kind = Kind::Arrow, .a = {0, 0, 0}, .b = {0, 0, 4}, .channel = 3}));
    std::vector<SceneLine> lines;
    RAWFRAME_EXPECT(addDebugLines(drawings, UINT32_MAX, lines) == 0);
    // A line, twelve edges, three circles of sixteen pieces, an arrow and
    // its head's four strokes.
    RAWFRAME_EXPECT(lines.size() == 1 + 12 + 48 + 5);
    if (lines.size() != 66) {
        return;
    }
    RAWFRAME_EXPECT(lines[0].color[0] == 1 && lines[0].color[1] == 0 && lines[0].color[3] == 1 && !lines[0].over &&
                    lines[0].ofView);
    // The box's edges run between its corners; the sphere's circles lie
    // its radius from its center; the head's strokes run from the tip to
    // half a meter back.
    for (std::size_t at = 1; at < 13; ++at) {
        const double kLength = std::abs(lines[at].to[0] - lines[at].from[0]) +
                               std::abs(lines[at].to[1] - lines[at].from[1]) +
                               std::abs(lines[at].to[2] - lines[at].from[2]);
        RAWFRAME_EXPECT(near(kLength, 1) || near(kLength, 2) || near(kLength, 3));
    }
    for (std::size_t at = 13; at < 61; ++at) {
        const double kX = lines[at].from[0] - 5;
        RAWFRAME_EXPECT(near(
            std::sqrt((kX * kX) + (lines[at].from[1] * lines[at].from[1]) + (lines[at].from[2] * lines[at].from[2])),
            2));
    }
    for (std::size_t at = 62; at < 66; ++at) {
        RAWFRAME_EXPECT(near(lines[at].from[2], 4) && near(lines[at].to[2], 3.5));
    }
    // Only the channels asked for.
    lines.clear();
    RAWFRAME_EXPECT(addDebugLines(drawings, 0b0110U, lines) == 0 && lines.size() == 12 + 48);
    // A scene whose lines are full leaves the rest out, counted.
    lines.assign(kMostSceneLines - 20, SceneLine{});
    RAWFRAME_EXPECT(addDebugLines(drawings, UINT32_MAX, lines) == 1 &&
                    lines.size() == kMostSceneLines - 20 + 1 + 12 + 5);
}
