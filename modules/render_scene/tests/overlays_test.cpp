// What an editor's preview draws over its scene (D798): a box's twelve
// edges at its corners, turned as its pose turns; a sphere's three great
// circles; a mesh nothing; a point light's three circles of its range and
// a spot's way and the ring its cone reaches. Each body's lines are
// colored by what it is (D839): static, kinematic, moving, at rest, or a
// sensor.

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
