#include "overlays.h"

#include "rawframe/view/preview.h"
#include "space.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <numbers>

namespace rawframe::render_scene {

namespace {

using Point = std::array<double, 3>;

/// A hundred and eightieth of the view's height wide, a pixel even in a
/// small viewport, behind what is nearer: an outline, thinner than the
/// mark's gizmo.
constexpr float kWidth = 1.0F / 180;
constexpr std::size_t kPieces = 16;
constexpr std::array<float, 4> kSolid{0.2F, 1, 0.3F, 1};
constexpr std::array<float, 4> kSensor{0.2F, 0.9F, 1, 1};
constexpr std::array<float, 4> kLit{1, 0.85F, 0.2F, 1};

Point along(const Point& from, const Vector& way, double by) {
    return {from[0] + (way[0] * by), from[1] + (way[1] * by), from[2] + (way[2] * by)};
}

void line(const Point& from, const Point& to, const std::array<float, 4>& color, std::vector<SceneLine>& lines) {
    lines.push_back(SceneLine{.from = from, .to = to, .color = color, .width = kWidth, .ofView = true});
}

/// A circle of `radius` about `middle` in the plane of `first` and
/// `second`.
void ring(const Point& middle,
          const Vector& first,
          const Vector& second,
          double radius,
          const std::array<float, 4>& color,
          std::vector<SceneLine>& lines) {
    const auto kOn = [&](std::size_t at) {
        const double kAngle = 2 * std::numbers::pi * static_cast<double>(at) / static_cast<double>(kPieces);
        return along(along(middle, first, radius * std::cos(kAngle)), second, radius * std::sin(kAngle));
    };
    for (std::size_t piece = 0; piece < kPieces; ++piece) {
        line(kOn(piece), kOn(piece + 1), color, lines);
    }
}

} // namespace

void addColliderLines(const physics3d::Body3D& body, const physics3d::Pose3D& pose, std::vector<SceneLine>& lines) {
    const std::array<float, 4> kQuaternion{pose.qx, pose.qy, pose.qz, pose.qw};
    const bool kTurned = std::ranges::any_of(kQuaternion, [](float value) {
        return value != 0;
    });
    const std::array<Vector, 3> kAxes = turnOf(kTurned ? kQuaternion : std::array<float, 4>{0, 0, 0, 1});
    const Point kMiddle{pose.x, pose.y, pose.z};
    const std::array<float, 4> kColor = body.sensor ? kSensor : kSolid;
    const double kWide = body.width;
    const double kHigh = body.height;
    switch (static_cast<physics3d::Shape>(body.shape)) {
    case physics3d::Shape::Box: {
        // Half sides along the body's own axes; an edge joins corners one
        // sign apart.
        const double kDeep = body.depth;
        const auto kCorner = [&](int corner) {
            return along(along(along(kMiddle, kAxes[0], (corner & 1) != 0 ? kWide : -kWide),
                               kAxes[1],
                               (corner & 2) != 0 ? kHigh : -kHigh),
                         kAxes[2],
                         (corner & 4) != 0 ? kDeep : -kDeep);
        };
        for (int corner = 0; corner < 8; ++corner) {
            for (const int kBit : {1, 2, 4}) {
                if ((corner & kBit) == 0) {
                    line(kCorner(corner), kCorner(corner | kBit), kColor, lines);
                }
            }
        }
        break;
    }
    case physics3d::Shape::Sphere:
        ring(kMiddle, kAxes[0], kAxes[1], kWide, kColor, lines);
        ring(kMiddle, kAxes[1], kAxes[2], kWide, kColor, lines);
        ring(kMiddle, kAxes[0], kAxes[2], kWide, kColor, lines);
        break;
    case physics3d::Shape::Capsule:
    case physics3d::Shape::Cylinder: {
        // Upright along the body's Y: a ring at each end, joined at four
        // sides. A capsule's ends are its two centers.
        const Point kTop = along(kMiddle, kAxes[1], kHigh);
        const Point kBottom = along(kMiddle, kAxes[1], -kHigh);
        ring(kTop, kAxes[0], kAxes[2], kWide, kColor, lines);
        ring(kBottom, kAxes[0], kAxes[2], kWide, kColor, lines);
        for (const auto& [kAxis, kSign] :
             {std::pair{0, 1.0}, std::pair{0, -1.0}, std::pair{2, 1.0}, std::pair{2, -1.0}}) {
            line(along(kTop, kAxes[kAxis], kSign * kWide), along(kBottom, kAxes[kAxis], kSign * kWide), kColor, lines);
        }
        break;
    }
    case physics3d::Shape::Mesh:
        break;
    }
}

void addLightLines(const LightInstance& light, std::vector<SceneLine>& lines) {
    const double kRange = light.light.range;
    if (!(kRange > 0)) {
        return;
    }
    const Vector kX{1, 0, 0};
    const Vector kY{0, 1, 0};
    const Vector kZ{0, 0, 1};
    if (!light.spot) {
        ring(light.position, kX, kY, kRange, kLit, lines);
        ring(light.position, kY, kZ, kRange, kLit, lines);
        ring(light.position, kX, kZ, kRange, kLit, lines);
        return;
    }
    const bool kTurned = std::ranges::any_of(light.rotation, [](float value) {
        return value != 0;
    });
    const std::array<Vector, 3> kAxes = turnOf(kTurned ? light.rotation : std::array<float, 4>{0, 0, 0, 1});
    // A spot shines along its -Z; its cone reaches a ring at its range.
    const Vector kAhead{-kAxes[2][0], -kAxes[2][1], -kAxes[2][2]};
    const Point kEnd = along(light.position, kAhead, kRange);
    line(light.position, kEnd, kLit, lines);
    const double kOuter = std::clamp(static_cast<double>(light.light.outer), 0.0, 1.5);
    ring(kEnd, kAxes[0], kAxes[1], kRange * std::tan(kOuter), kLit, lines);
}

void drawOverlays(world::World& world,
                  std::uint8_t overlays,
                  std::span<const LightInstance> lights,
                  std::optional<world::ColumnQuery>& bodies,
                  std::vector<SceneLine>& lines) {
    if ((overlays & static_cast<std::uint8_t>(view::Overlay::Colliders)) != 0) {
        if (!bodies.has_value()) {
            const auto kBody = world.registry().find(physics3d::Body3D::kComponentTypeId);
            const auto kPose = world.registry().find(physics3d::Pose3D::kComponentTypeId);
            if (kBody.has_value() && kPose.has_value()) {
                const std::array<world::ColumnTerm, 2> kTerms = {world::ColumnTerm{*kBody, world::Access::Read},
                                                                 world::ColumnTerm{*kPose, world::Access::Read}};
                if (auto made = world::ColumnQuery::resolve(kTerms, world.registry()); made.has_value()) {
                    bodies = std::move(*made);
                }
            }
        }
        if (bodies.has_value()) {
            bodies->forEachChunk(world, [&lines](const world::ColumnChunk& chunk) {
                for (std::size_t row = 0; row < chunk.entities.size() && lines.size() < kMostSceneLines; ++row) {
                    physics3d::Body3D body;
                    physics3d::Pose3D pose;
                    std::memcpy(&body, chunk.columns[0] + (row * sizeof(body)), sizeof(body));
                    std::memcpy(&pose, chunk.columns[1] + (row * sizeof(pose)), sizeof(pose));
                    addColliderLines(body, pose, lines);
                }
            });
        }
    }
    if ((overlays & static_cast<std::uint8_t>(view::Overlay::Lights)) != 0) {
        for (const LightInstance& kLight : lights) {
            if (lines.size() >= kMostSceneLines) {
                break;
            }
            addLightLines(kLight, lines);
        }
    }
    if (lines.size() > kMostSceneLines) {
        lines.resize(kMostSceneLines);
    }
}

} // namespace rawframe::render_scene
