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

using Planes = std::array<m2MoverPlane, M2_MAX_MOVER_PLANES>;

[[nodiscard]] float lengthOf(m2Vec2 vector) noexcept {
    return std::sqrt((vector.x * vector.x) + (vector.y * vector.y));
}

void shift(m2Transform& at, m2Vec2 by) noexcept {
    at.p.x += static_cast<double>(by.x);
    at.p.y += static_cast<double>(by.y);
}

[[nodiscard]] m2Capsule capsuleOf(const CharacterShape& shape, float radius) noexcept {
    return m2Capsule{.point1 = {0, -shape.halfHeight}, .point2 = {0, shape.halfHeight}, .radius = radius};
}

/// The thinner capsule the sweeps are made with.
[[nodiscard]] m2Capsule thinnerOf(const CharacterShape& shape) noexcept {
    return capsuleOf(shape, std::max(shape.radius - kSkin, shape.radius / 2));
}

/// The planes around the capsule at `at` that its filter sees, in Maul2D's
/// order; at most M2_MAX_MOVER_PLANES.
std::int32_t collide(const m2World* world, const CharacterShape& shape, m2Transform at, Planes& planes) noexcept {
    const m2Capsule kShape = capsuleOf(shape, shape.radius);
    return std::min(m2World_CollideMover(world, &kShape, at, shape.filter, planes.data(), M2_MAX_MOVER_PLANES),
                    std::int32_t{M2_MAX_MOVER_PLANES});
}

/// The closest hit of the thinner capsule at `at` swept along
/// `translation`, if any.
[[nodiscard]] bool
cast(const m2World* world, const CharacterShape& shape, m2Transform at, m2Vec2 translation, m2CastHit& hit) noexcept {
    const m2Capsule kThinner = thinnerOf(shape);
    return m2World_CastCapsuleClosest(world, &kThinner, at, translation, shape.filter, &hit);
}

/// The sweep of the thinner capsule from `at` along `translation`: the part
/// of it that is clear, from 0 to 1. A sweep that starts inside something
/// is left to the planes, which push out of it.
[[nodiscard]] float
clearPart(const m2World* world, const CharacterShape& shape, m2Transform at, m2Vec2 translation) noexcept {
    const float kLength = lengthOf(translation);
    if (!(kLength > 0)) {
        return 0;
    }
    m2CastHit hit{};
    if (!cast(world, shape, at, translation, hit) || (hit.normal.x == 0 && hit.normal.y == 0)) {
        return 1;
    }
    return std::max(0.0F, hit.fraction - (kSkin / kLength));
}

/// Trace and slide from `at` along `wish`: moves `at`, and says how far. A
/// surface too steep to stand on is a wall: sliding along it never turns a
/// wish that does not rise into one that does, or a character would walk
/// up a cliff.
[[nodiscard]] m2Vec2
slide(const m2World* world, const CharacterShape& shape, float groundNormal, m2Transform& at, m2Vec2 wish) noexcept {
    Planes planes{};
    m2Vec2 moved{0, 0};
    for (int pass = 0; pass < kPasses; ++pass) {
        const std::int32_t kCount = collide(world, shape, at, planes);
        const m2Vec2 kLeft{wish.x - moved.x, wish.y - moved.y};
        m2MoverMove solved = m2SolveMover(kLeft, planes.data(), kCount);
        if (wish.y <= 0 && solved.translation.y > 0) {
            for (std::int32_t index = 0; index < kCount; ++index) {
                if ((solved.pressed & (std::uint32_t{1} << static_cast<std::uint32_t>(index))) != 0 &&
                    planes[static_cast<std::size_t>(index)].normal.y < groundNormal) {
                    solved.translation.y = 0;
                    break;
                }
            }
        }
        const float kPart = clearPart(world, shape, at, solved.translation);
        const m2Vec2 kStep{solved.translation.x * kPart, solved.translation.y * kPart};
        shift(at, kStep);
        moved.x += kStep.x;
        moved.y += kStep.y;
        if ((kPart == 1 && solved.pressed == 0) || lengthOf(kStep) < kStill) {
            break;
        }
    }
    return moved;
}

/// Walked into something with its `stepHeight` to climb: up, along the
/// wish across, and back down onto ground. Where it lands, if it lands on
/// ground no higher than the step and gets further than `progress` did.
/// The ground is told by a ray down just past the capsule's front, which
/// must meet walkable ground no lower than its feet and no higher above
/// where they started than the step: the tread of a step it stands on the
/// edge of, where a capsule's own contact is the edge's slanted normal. A
/// steep ramp's face is what that ray meets there, so a ramp is never
/// climbed a step at a time, and the top of a wall is too high.
[[nodiscard]] bool climb(const m2World* world,
                         const CharacterShape& shape,
                         const Character2D& character,
                         m2Transform from,
                         float across,
                         float progress,
                         m2Transform& landed,
                         m2CastHit& tread) noexcept {
    const float kToward = across > 0 ? 1.0F : -1.0F;
    // No higher than the character is tall.
    const float kStep = std::min(character.stepHeight, 2 * (shape.halfHeight + shape.radius));
    m2Transform at = from;
    const float kLift = kStep * clearPart(world, shape, at, m2Vec2{0, kStep});
    shift(at, m2Vec2{0, kLift});
    const m2Vec2 kAhead = slide(world, shape, character.groundNormal, at, m2Vec2{across, 0});
    if (!((kAhead.x * kToward) > progress + kStill)) {
        return false;
    }
    const float kReach = kLift + kSkin;
    m2CastHit land{};
    if (!cast(world, shape, at, m2Vec2{0, -kReach}, land) || (land.normal.x == 0 && land.normal.y == 0)) {
        return false;
    }
    shift(at, m2Vec2{0, -std::max(0.0F, (land.fraction * kReach) - kSkin)});
    const m2Pos2 kOrigin{at.p.x + static_cast<double>(kToward * (shape.radius + kTouching)), at.p.y};
    const float kDepth = shape.halfHeight + shape.radius + kTouching;
    m2CastHit floor{};
    const double kBelow = static_cast<double>(shape.halfHeight + shape.radius);
    if (!m2World_CastRayClosest(world, kOrigin, m2Vec2{0, -kDepth}, shape.filter, &floor) ||
        floor.normal.y < character.groundNormal ||
        floor.point.y < at.p.y - kBelow - static_cast<double>(2 * kTouching) ||
        floor.point.y > from.p.y - kBelow + static_cast<double>(kStep + kTouching)) {
        return false;
    }
    landed = at;
    tread = floor;
    return true;
}

/// What a character at `at` stands on: of the planes it touches, the most
/// upward.
[[nodiscard]] CharacterMove classify(const m2World* world,
                                     const CharacterShape& shape,
                                     const Character2D& character,
                                     m2Transform at,
                                     Planes& planes,
                                     std::int32_t& count) noexcept {
    count = collide(world, shape, at, planes);
    CharacterMove found;
    for (std::int32_t index = 0; index < count; ++index) {
        const m2MoverPlane& plane = planes[static_cast<std::size_t>(index)];
        if (plane.separation <= kTouching && plane.normal.y > kSlope &&
            (found.ground == physics::Ground::Airborne || plane.normal.y > found.normal.y)) {
            found.normal = plane.normal;
            found.groundShape = plane.shapeId;
            found.ground =
                plane.normal.y >= character.groundNormal ? physics::Ground::Grounded : physics::Ground::Sliding;
        }
    }
    if (found.ground != physics::Ground::Grounded) {
        found.groundShape = {};
    }
    return found;
}

/// The dynamic bodies among `planes` that stopped the move short of
/// `wish` and that the character may push, each once, by the most it was
/// stopped short against any of its planes.
void push(const m2World* world,
          const Character2D& character,
          m2Vec2 shortBy,
          float seconds,
          const Planes& planes,
          std::int32_t count,
          CharacterMove& move) noexcept {
    if (!(character.mass > 0) || !(character.pushRatio > 0) || !(seconds > 0)) {
        return;
    }
    std::array<float, M2_MAX_MOVER_PLANES> stopped{};
    for (std::int32_t index = 0; index < count; ++index) {
        const m2MoverPlane& plane = planes[static_cast<std::size_t>(index)];
        // Ground is stood on, not pushed.
        const float kInto = -((shortBy.x * plane.normal.x) + (shortBy.y * plane.normal.y));
        if (plane.separation > kTouching || plane.normal.y >= character.groundNormal || !(kInto > 0)) {
            continue;
        }
        const m2BodyId kBody = m2Shape_GetBody(world, plane.shapeId);
        if (m2Body_GetType(world, kBody) != m2_dynamicBody) {
            continue;
        }
        const float kWeight = m2Body_GetMass(world, kBody);
        if (!(kWeight > 0) || kWeight > character.pushRatio * character.mass) {
            continue;
        }
        std::int32_t slot = 0;
        while (slot < move.pushCount &&
               !(move.pushes[static_cast<std::size_t>(slot)].body.index1 == kBody.index1 &&
                 move.pushes[static_cast<std::size_t>(slot)].body.generation == kBody.generation)) {
            ++slot;
        }
        if (slot < move.pushCount && !(kInto > stopped[static_cast<std::size_t>(slot)])) {
            continue;
        }
        // A body no heavier than the character takes the speed it was
        // stopped short by; a heavier one, as much less as it is heavier.
        const float kImpulse = std::min(character.mass, kWeight) * (kInto / seconds);
        if (slot == move.pushCount) {
            ++move.pushCount;
        }
        stopped[static_cast<std::size_t>(slot)] = kInto;
        move.pushes[static_cast<std::size_t>(slot)] =
            CharacterPush{.body = kBody,
                          .impulse = m2Vec2{-plane.normal.x * kImpulse, -plane.normal.y * kImpulse},
                          .point = plane.point};
    }
}

} // namespace

CharacterMove moveCharacter(const m2World* world,
                            const CharacterShape& shape,
                            m2Transform from,
                            m2Vec2 wish,
                            const Character2D& character,
                            bool wasGrounded,
                            float seconds) noexcept {
    m2Transform at = from;
    m2Vec2 moved = slide(world, shape, character.groundNormal, at, wish);

    // Grounded, not rising, and stopped short across: a step it may climb.
    bool climbed = false;
    m2CastHit tread{};
    const float kAcross = std::abs(wish.x);
    if (wasGrounded && character.stepHeight > 0 && wish.y <= 0 && kAcross > kStill) {
        const float kProgress = wish.x > 0 ? moved.x : -moved.x;
        m2Transform landed{};
        if (kProgress < kAcross - kStill && climb(world, shape, character, from, wish.x, kProgress, landed, tread)) {
            at = landed;
            moved = m2Vec2{static_cast<float>(at.p.x - from.p.x), static_cast<float>(at.p.y - from.p.y)};
            climbed = true;
        }
    }
    const float kLift = climbed ? moved.y : 0;

    Planes planes{};
    std::int32_t count = 0;
    CharacterMove on = classify(world, shape, character, at, planes, count);
    // Up a step it may rest on the step's edge, whose slanted normal is no
    // ground: what it stands on is the tread the climb found.
    if (climbed && on.ground != physics::Ground::Grounded) {
        on.ground = physics::Ground::Grounded;
        on.normal = tread.normal;
        on.groundShape = tread.shapeId;
    }

    // Walked off ground it stood on, and not rising: down to ground within
    // the snap below, if there is any.
    if (on.ground != physics::Ground::Grounded && wasGrounded && character.snap > 0 && wish.y <= 0) {
        const m2Vec2 kDown{0, -(character.snap + kSkin)};
        m2CastHit hit{};
        if (cast(world, shape, at, kDown, hit) && hit.normal.y >= character.groundNormal) {
            const float kDrop = std::max(0.0F, (hit.fraction * -kDown.y) - kSkin);
            shift(at, m2Vec2{0, -kDrop});
            moved.y -= kDrop;
            on = classify(world, shape, character, at, planes, count);
        }
    }
    on.translation = moved;
    on.lift = kLift;
    on.climbed = climbed;
    if (!climbed) {
        push(world, character, m2Vec2{wish.x - moved.x, wish.y - moved.y}, seconds, planes, count, on);
    }
    return on;
}

bool characterFits(const m2World* world, const CharacterShape& shape, m2Transform at) noexcept {
    // The thinner capsule, so ground it stands a skin above is no overlap.
    const m2Capsule kThinner = thinnerOf(shape);
    std::array<m2ShapeId, 1> overlapped{};
    return m2World_OverlapCapsule(
               world, &kThinner, at, shape.filter, overlapped.data(), static_cast<std::int32_t>(overlapped.size())) ==
           0;
}

m2Vec2
sweepCharacter(const m2World* world, const CharacterShape& shape, m2Transform from, m2Vec2 translation) noexcept {
    const float kPart = clearPart(world, shape, from, translation);
    return m2Vec2{translation.x * kPart, translation.y * kPart};
}

} // namespace rawframe::physics2d
