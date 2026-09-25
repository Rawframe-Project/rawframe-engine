#include "characters.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace rawframe::physics3d {

namespace {

/// Trace and slide passes a move may take: each gathers the planes where it
/// stands, solves for the wish, and sweeps as far as that goes.
constexpr int kPasses = 5;
/// A sweep stops this short of what it meets, and is made with a capsule
/// this much thinner, so the next pass starts clear rather than touching.
constexpr float kSkin = 0.005F;
/// How far around the capsule planes are gathered.
constexpr float kCollar = 4 * kSkin;
/// A pass that moves less than this ends the move.
constexpr float kStill = 1.0e-4F;
/// A plane this near after the move is one the character is on.
constexpr float kTouching = 4 * kSkin;
/// A surface whose normal is less upward than this is a wall or a ceiling,
/// not a slope to slide down.
constexpr float kSlope = 0.05F;
/// Planes gathered before those the filter does not see are left out.
constexpr std::int32_t kGathered = 64;

[[nodiscard]] float lengthOf(m3Vec3 vector) noexcept {
    return std::sqrt((vector.x * vector.x) + (vector.y * vector.y) + (vector.z * vector.z));
}

void shift(m3Pos3& at, m3Vec3 by) noexcept {
    at.x += static_cast<double>(by.x);
    at.y += static_cast<double>(by.y);
    at.z += static_cast<double>(by.z);
}

/// Whether a query with `filter` sees a shape: Maul3D's own rule, applied
/// to the mover's planes, which it gathers from every shape.
[[nodiscard]] bool sees(m3ShapeId shape, m3QueryFilter filter) noexcept {
    std::uint64_t category = 0;
    std::uint64_t mask = 0;
    std::int32_t group = 0;
    m3Shape_GetFilter(shape, &category, &mask, &group);
    return (filter.categoryBits & mask) != 0 && (category & filter.maskBits) != 0;
}

/// The planes around a capsule at `at` that the filter sees, in Maul3D's
/// order; at most M3_MOVER_PLANES.
std::int32_t collide(m3WorldId world,
                     m3Pos3 at,
                     float halfHeight,
                     float radius,
                     m3QueryFilter filter,
                     std::array<m3MoverPlane, M3_MOVER_PLANES>& planes) noexcept {
    std::array<m3MoverPlane, kGathered> gathered{};
    const std::int32_t kCount =
        m3World_CollideMover(world, at, halfHeight, radius, kCollar, gathered.data(), kGathered);
    std::int32_t kept = 0;
    for (std::int32_t index = 0; index < kCount && kept < M3_MOVER_PLANES; ++index) {
        if (sees(gathered[static_cast<std::size_t>(index)].shapeId, filter)) {
            planes[static_cast<std::size_t>(kept++)] = gathered[static_cast<std::size_t>(index)];
        }
    }
    return kept;
}

/// The sweep of a thinner capsule from `at` along `translation`: the part
/// of it that is clear, from 0 to 1. A sweep that starts inside something
/// is left to the planes, which push out of it.
[[nodiscard]] float clearPart(
    m3WorldId world, float halfHeight, float thinner, m3Pos3 at, m3Vec3 translation, m3QueryFilter filter) noexcept {
    const float kLength = lengthOf(translation);
    if (!(kLength > 0)) {
        return 0;
    }
    const m3RayCastResult kHit = m3World_CastCapsuleClosest(
        world, at, m3Vec3{0, -halfHeight, 0}, m3Vec3{0, halfHeight, 0}, thinner, translation, filter);
    if (!kHit.hit || (kHit.normal.x == 0 && kHit.normal.y == 0 && kHit.normal.z == 0)) {
        return 1;
    }
    return std::max(0.0F, kHit.fraction - (kSkin / kLength));
}

} // namespace

CharacterMove moveCharacter(m3WorldId world,
                            float radius,
                            float halfHeight,
                            m3Pos3 from,
                            m3Vec3 wish,
                            const Character3D& character,
                            bool wasGrounded,
                            m3QueryFilter filter) noexcept {
    const float kThinner = std::max(radius - kSkin, radius / 2);
    std::array<m3MoverPlane, M3_MOVER_PLANES> planes{};

    CharacterMove move;
    m3Pos3 at = from;
    for (int pass = 0; pass < kPasses; ++pass) {
        const std::int32_t kCount = collide(world, at, halfHeight, radius, filter, planes);
        const m3Vec3 kLeft{wish.x - move.translation.x, wish.y - move.translation.y, wish.z - move.translation.z};
        const m3MoverMove kSolved = m3SolveMover(kLeft, planes.data(), kCount);
        const float kPart = clearPart(world, halfHeight, kThinner, at, kSolved.translation, filter);
        const m3Vec3 kStep{kSolved.translation.x * kPart, kSolved.translation.y * kPart, kSolved.translation.z * kPart};
        shift(at, kStep);
        move.translation.x += kStep.x;
        move.translation.y += kStep.y;
        move.translation.z += kStep.z;
        if ((kPart == 1 && kSolved.pressed == 0) || lengthOf(kStep) < kStill) {
            break;
        }
    }

    // What it is on: of the planes it touches, the most upward.
    const auto kClassify = [&](m3Pos3 where) {
        const std::int32_t kCount = collide(world, where, halfHeight, radius, filter, planes);
        CharacterMove found;
        for (std::int32_t index = 0; index < kCount; ++index) {
            const m3MoverPlane& plane = planes[static_cast<std::size_t>(index)];
            if (plane.separation <= kTouching && plane.normal.y > kSlope &&
                (found.ground == physics::Ground::Airborne || plane.normal.y > found.normal.y)) {
                found.normal = plane.normal;
                found.ground =
                    plane.normal.y >= character.groundNormal ? physics::Ground::Grounded : physics::Ground::Sliding;
            }
        }
        return found;
    };
    CharacterMove on = kClassify(at);

    // Walked off ground it stood on, and not rising: down to ground within
    // the snap below, if there is any.
    if (on.ground != physics::Ground::Grounded && wasGrounded && character.snap > 0 && wish.y <= 0) {
        const float kReach = character.snap + kSkin;
        const m3RayCastResult kHit = m3World_CastCapsuleClosest(
            world, at, m3Vec3{0, -halfHeight, 0}, m3Vec3{0, halfHeight, 0}, kThinner, m3Vec3{0, -kReach, 0}, filter);
        if (kHit.hit && kHit.normal.y >= character.groundNormal) {
            const float kDrop = std::max(0.0F, (kHit.fraction * kReach) - kSkin);
            shift(at, m3Vec3{0, -kDrop, 0});
            move.translation.y -= kDrop;
            on = kClassify(at);
        }
    }
    move.ground = on.ground;
    move.normal = on.normal;
    return move;
}

} // namespace rawframe::physics3d
