#include "characters.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace rawframe::physics2d {

namespace {

/// Trace and slide passes a move may take: each gathers the planes where it
/// stands, solves for the wish, and sweeps as far as that goes.
constexpr int kPasses = 5;
/// A sweep stops this short of what it meets, and is made with a capsule
/// this much thinner, so the next pass starts clear rather than touching.
/// Maul2D's linear slop.
constexpr float kSkin = 0.005F;
/// A pass that moves less than this ends the move.
constexpr float kStill = 1.0e-4F;
/// A plane this near after the move is one the character is on.
constexpr float kTouching = 4 * kSkin;
/// A surface whose normal is less upward than this is a wall or a ceiling,
/// not a slope to slide down.
constexpr float kSlope = 0.05F;

[[nodiscard]] float lengthOf(m2Vec2 vector) noexcept {
    return std::sqrt((vector.x * vector.x) + (vector.y * vector.y));
}

/// The sweep of a thinner capsule from `at` along `translation`: the part
/// of it that is clear, from 0 to 1. A sweep that starts inside something
/// is left to the planes, which push out of it.
[[nodiscard]] float clearPart(
    m2WorldId world, const m2Capsule& thinner, m2Transform at, m2Vec2 translation, m2QueryFilter filter) noexcept {
    const float kLength = lengthOf(translation);
    if (!(kLength > 0)) {
        return 0;
    }
    const m2RayCastResult kHit = m2World_CastCapsuleClosest(world, &thinner, at, translation, filter);
    if (!kHit.hit || (kHit.normal.x == 0 && kHit.normal.y == 0)) {
        return 1;
    }
    return std::max(0.0F, kHit.fraction - (kSkin / kLength));
}

} // namespace

CharacterMove moveCharacter(m2WorldId world,
                            float radius,
                            float halfHeight,
                            m2Transform from,
                            m2Vec2 wish,
                            const Character2D& character,
                            bool wasGrounded,
                            m2QueryFilter filter) noexcept {
    const m2Capsule kShape{.point1 = {0, -halfHeight}, .point2 = {0, halfHeight}, .radius = radius};
    const m2Capsule kThinner{
        .point1 = {0, -halfHeight}, .point2 = {0, halfHeight}, .radius = std::max(radius - kSkin, radius / 2)};
    std::array<m2MoverPlane, M2_MOVER_PLANES> planes{};
    const auto kCollide = [&](m2Transform at) {
        return std::min(m2World_CollideMover(world, &kShape, at, planes.data(), M2_MOVER_PLANES, filter),
                        std::int32_t{M2_MOVER_PLANES});
    };
    const auto kShift = [](m2Transform& at, m2Vec2 by) {
        at.p.x += static_cast<double>(by.x);
        at.p.y += static_cast<double>(by.y);
    };

    CharacterMove move;
    m2Transform at = from;
    for (int pass = 0; pass < kPasses; ++pass) {
        const std::int32_t kCount = kCollide(at);
        const m2Vec2 kLeft{wish.x - move.translation.x, wish.y - move.translation.y};
        const m2MoverMove kSolved = m2SolveMover(kLeft, planes.data(), kCount);
        const float kPart = clearPart(world, kThinner, at, kSolved.translation, filter);
        const m2Vec2 kStep{kSolved.translation.x * kPart, kSolved.translation.y * kPart};
        kShift(at, kStep);
        move.translation.x += kStep.x;
        move.translation.y += kStep.y;
        if ((kPart == 1 && kSolved.pressed == 0) || lengthOf(kStep) < kStill) {
            break;
        }
    }

    // What it is on: of the planes it touches, the most upward.
    const auto kClassify = [&](m2Transform where) {
        const std::int32_t kCount = kCollide(where);
        CharacterMove found;
        for (std::int32_t index = 0; index < kCount; ++index) {
            const m2MoverPlane& plane = planes[static_cast<std::size_t>(index)];
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
        const m2Vec2 kDown{0, -(character.snap + kSkin)};
        const m2RayCastResult kHit = m2World_CastCapsuleClosest(world, &kThinner, at, kDown, filter);
        if (kHit.hit && kHit.normal.y >= character.groundNormal) {
            const float kDrop = std::max(0.0F, (kHit.fraction * -kDown.y) - kSkin);
            kShift(at, m2Vec2{0, -kDrop});
            move.translation.y -= kDrop;
            on = kClassify(at);
        }
    }
    move.ground = on.ground;
    move.normal = on.normal;
    return move;
}

} // namespace rawframe::physics2d
