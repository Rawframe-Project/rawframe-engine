#pragma once

// A body's values as the physics module checks, compares, and turns them,
// and what it keeps of each entity's body: private to the module, as Maul3D
// is.

#include "rawframe/physics/filters.h"
#include "rawframe/physics3d/components.h"
#include "rawframe/world/entity.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <maul3d/maul3d.h>
#include <optional>
#include <vector>

namespace rawframe::physics3d {

/// Bit for bit, so a value written back is told from any other, NaNs and
/// signed noughts included. Only for the components without padding:
/// Pose3D, Velocity3D, and Impulse3D.
template <typename T> [[nodiscard]] bool same(const T& left, const T& right) noexcept {
    return std::memcmp(&left, &right, sizeof(T)) == 0;
}

/// Field by field: a Body3D has padding, which holds whatever it holds.
[[nodiscard]] inline bool same(const Body3D& left, const Body3D& right) noexcept {
    const auto kBits = [](float value) {
        return std::bit_cast<std::uint32_t>(value);
    };
    return left.motion == right.motion && left.shape == right.shape && left.fixedRotation == right.fixedRotation &&
           left.bullet == right.bullet && left.sensor == right.sensor && left.collisionClass == right.collisionClass &&
           kBits(left.width) == kBits(right.width) && kBits(left.height) == kBits(right.height) &&
           kBits(left.depth) == kBits(right.depth) && kBits(left.density) == kBits(right.density) &&
           kBits(left.friction) == kBits(right.friction) && kBits(left.restitution) == kBits(right.restitution) &&
           kBits(left.linearDamping) == kBits(right.linearDamping) &&
           kBits(left.angularDamping) == kBits(right.angularDamping);
}

[[nodiscard]] inline bool finite(float value) noexcept {
    return std::isfinite(value);
}

/// A sensor twin's density: Maul3D wants one, and a twin must not weigh.
constexpr float kTwinDensity = 1e-6F;

/// Sides of a cylinder's prism.
constexpr std::int32_t kCylinderSides = 16;

/// Whether a body can be made of these: sizes positive, the rest finite and
/// in range.
[[nodiscard]] inline bool makeable(const Body3D& body, const Pose3D& pose, const Velocity3D& velocity) noexcept {
    bool shaped = false;
    switch (static_cast<Shape>(body.shape)) {
    case Shape::Sphere:
        shaped = body.width > 0;
        break;
    case Shape::Box:
        shaped = body.width > 0 && body.height > 0 && body.depth > 0;
        break;
    case Shape::Capsule:
        // A capsule of no length is a sphere, which Maul3D wants made as one.
        shaped = body.width > 0 && body.height > 0;
        break;
    case Shape::Cylinder:
        shaped = body.width > 0 && body.height > 0;
        break;
    case Shape::Mesh:
        // Triangles have no mass to move with, nor an inside to sense.
        shaped = body.motion == static_cast<std::uint8_t>(physics::Motion::Static) && !body.sensor;
        break;
    }
    return body.motion <= static_cast<std::uint8_t>(physics::Motion::Dynamic) && shaped && finite(body.width) &&
           finite(body.height) && finite(body.depth) && body.density >= 0 && finite(body.density) &&
           body.friction >= 0 && finite(body.friction) && body.restitution >= 0 && body.restitution <= 1 &&
           body.linearDamping >= 0 && finite(body.linearDamping) && body.angularDamping >= 0 &&
           finite(body.angularDamping) && std::isfinite(pose.x) && std::isfinite(pose.y) && std::isfinite(pose.z) &&
           finite(pose.qx) && finite(pose.qy) && finite(pose.qz) && finite(pose.qw) && finite(velocity.x) &&
           finite(velocity.y) && finite(velocity.z) && finite(velocity.angularX) && finite(velocity.angularY) &&
           finite(velocity.angularZ);
}

/// A pose's rotation made unit, all noughts (or nothing to make unit) as
/// none: what a body is made with, and what presentation shows of a blended
/// pose.
[[nodiscard]] inline m3Quat rotationOf(const Pose3D& pose) noexcept {
    const double kLength = std::sqrt((double{pose.qx} * pose.qx) + (double{pose.qy} * pose.qy) +
                                     (double{pose.qz} * pose.qz) + (double{pose.qw} * pose.qw));
    if (!(kLength > 0)) {
        return m3Quat{0, 0, 0, 1};
    }
    return m3Quat{static_cast<float>(pose.qx / kLength),
                  static_cast<float>(pose.qy / kLength),
                  static_cast<float>(pose.qz / kLength),
                  static_cast<float>(pose.qw / kLength)};
}

[[nodiscard]] inline m3Transform transformOf(const Pose3D& pose) noexcept {
    return m3Transform{.p = {pose.x, pose.y, pose.z}, .q = rotationOf(pose)};
}

/// A vector turned by a unit quaternion, or by its inverse; in doubles.
struct Turned {
    double x = 0;
    double y = 0;
    double z = 0;
};
[[nodiscard]] inline Turned turn(const m3Quat& rotation, Turned vector, bool inverse) noexcept {
    const double kW = rotation.w;
    const double kX = inverse ? -double{rotation.x} : double{rotation.x};
    const double kY = inverse ? -double{rotation.y} : double{rotation.y};
    const double kZ = inverse ? -double{rotation.z} : double{rotation.z};
    // v + 2w (q x v) + 2 q x (q x v)
    const double kCx = (kY * vector.z) - (kZ * vector.y);
    const double kCy = (kZ * vector.x) - (kX * vector.z);
    const double kCz = (kX * vector.y) - (kY * vector.x);
    return Turned{vector.x + (2 * kW * kCx) + (2 * ((kY * kCz) - (kZ * kCy))),
                  vector.y + (2 * kW * kCy) + (2 * ((kZ * kCx) - (kX * kCz))),
                  vector.z + (2 * kW * kCz) + (2 * ((kX * kCy) - (kY * kCx)))};
}

/// Where a pose's frame puts a world point or direction, and back.
[[nodiscard]] inline Turned toLocal(const m3Transform& frame, Turned point) noexcept {
    return turn(frame.q, Turned{point.x - frame.p.x, point.y - frame.p.y, point.z - frame.p.z}, true);
}
[[nodiscard]] inline Turned toWorld(const m3Transform& frame, Turned local) noexcept {
    const Turned kTurned = turn(frame.q, local, false);
    return Turned{frame.p.x + kTurned.x, frame.p.y + kTurned.y, frame.p.z + kTurned.z};
}

/// How far from its origin any of a body's shape reaches.
[[nodiscard]] inline double reachOf(const Body3D& body) noexcept {
    const double kWidth = body.width;
    const double kHeight = body.height;
    const double kDepth = body.depth;
    switch (static_cast<Shape>(body.shape)) {
    case Shape::Sphere:
        return kWidth;
    case Shape::Box:
        return std::sqrt((kWidth * kWidth) + (kHeight * kHeight) + (kDepth * kDepth));
    case Shape::Capsule:
        return kWidth + kHeight;
    case Shape::Cylinder:
        return std::sqrt((kWidth * kWidth) + (kHeight * kHeight));
    case Shape::Mesh:
        break;
    }
    return 0;
}

/// Whether the segment from `origin` along `toward` passes within `reach` of
/// `center`.
[[nodiscard]] inline bool passesNear(Turned origin, Turned toward, Turned center, double reach) noexcept {
    const Turned kTo{center.x - origin.x, center.y - origin.y, center.z - origin.z};
    const double kLength = (toward.x * toward.x) + (toward.y * toward.y) + (toward.z * toward.z);
    const double kAlong =
        kLength > 0 ? std::clamp(((kTo.x * toward.x) + (kTo.y * toward.y) + (kTo.z * toward.z)) / kLength, 0.0, 1.0)
                    : 0.0;
    const Turned kOff{kTo.x - (toward.x * kAlong), kTo.y - (toward.y * kAlong), kTo.z - (toward.z * kAlong)};
    // A margin for the float geometry the shape is tested with.
    const double kWithin = reach + 0.01;
    return (kOff.x * kOff.x) + (kOff.y * kOff.y) + (kOff.z * kOff.z) <= kWithin * kWithin;
}

/// One entity's body, and what the last step wrote, to tell gameplay's
/// writes from its own.
struct Mapped {
    m3BodyId body{};
    m3ShapeId shape{};
    /// A mesh's shapes past its first.
    std::vector<m3ShapeId> pieces;
    /// The mesh it was made of, and how far from its origin it reaches.
    std::uint64_t mesh = 0;
    double reach = 0;
    /// The sensor twin of a body whose class triggers with another.
    m3ShapeId trigger{};
    /// Its pose after each of the last steps, by tick modulo the history's
    /// length, and the first tick of its unbroken trail.
    std::vector<Pose3D> history;
    std::optional<std::uint64_t> since;
    bool refused = false;
    /// Made as a character's, which character queries do not see.
    bool character = false;
    Body3D made;
    Pose3D pose;
    Velocity3D velocity;
};

struct Row {
    world::EntityHandle entity;
    const Body3D* body = nullptr;
    Pose3D* pose = nullptr;
    Velocity3D* velocity = nullptr;
    Character3D* character = nullptr;
    const Mesh3D* mesh = nullptr;
};

/// The mesh a row's body is made of: its Mesh3D's for a mesh body, else
/// none.
[[nodiscard]] inline std::uint64_t meshOf(const Row& row) noexcept {
    return row.body->shape == static_cast<std::uint8_t>(Shape::Mesh) && row.mesh != nullptr ? row.mesh->mesh : 0;
}

[[nodiscard]] inline bool sameShape(m3ShapeId left, m3ShapeId right) noexcept {
    return left.index1 == right.index1 && left.generation == right.generation && left.world == right.world;
}

[[nodiscard]] inline bool sameBody(m3BodyId left, m3BodyId right) noexcept {
    return left.index1 == right.index1 && left.generation == right.generation && left.world == right.world;
}

} // namespace rawframe::physics3d
