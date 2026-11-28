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

[[nodiscard]] float dot(m3Vec3 left, m3Vec3 right) noexcept {
    return (left.x * right.x) + (left.y * right.y) + (left.z * right.z);
}

[[nodiscard]] m3Vec3 scaled(m3Vec3 vector, float by) noexcept {
    return m3Vec3{vector.x * by, vector.y * by, vector.z * by};
}

void shift(m3Pos3& at, m3Vec3 by) noexcept {
    at.x += static_cast<double>(by.x);
    at.y += static_cast<double>(by.y);
    at.z += static_cast<double>(by.z);
}

/// Whether a query with `filter` sees a shape: Maul3D's own rule, applied
/// to the mover's planes, which it gathers from every shape.
[[nodiscard]] bool sees(const m3World* world, m3ShapeId shape, m3QueryFilter filter) noexcept {
    std::uint64_t category = 0;
    std::uint64_t mask = 0;
    std::int32_t group = 0;
    m3Shape_GetFilter(world, shape, &category, &mask, &group);
    return (filter.categoryBits & mask) != 0 && (category & filter.maskBits) != 0;
}

using Planes = std::array<m3MoverPlane, M3_MOVER_PLANES>;

/// The planes around the capsule at `at` that its filter sees, in Maul3D's
/// order; at most M3_MOVER_PLANES.
std::int32_t collide(const m3World* world, const CharacterShape& shape, m3Pos3 at, Planes& planes) noexcept {
    std::array<m3MoverPlane, kGathered> gathered{};
    const std::int32_t kCount =
        m3World_CollideMover(world, at, shape.halfHeight, shape.radius, kCollar, gathered.data(), kGathered);
    std::int32_t kept = 0;
    for (std::int32_t index = 0; index < kCount && kept < M3_MOVER_PLANES; ++index) {
        if (sees(world, gathered[static_cast<std::size_t>(index)].shapeId, shape.filter)) {
            planes[static_cast<std::size_t>(kept++)] = gathered[static_cast<std::size_t>(index)];
        }
    }
    return kept;
}

/// The radius of the thinner capsule the sweeps are made with.
[[nodiscard]] float thinnerOf(const CharacterShape& shape) noexcept {
    return std::max(shape.radius - kSkin, shape.radius / 2);
}

/// The closest hit of the thinner capsule at `at` swept along `translation`.
[[nodiscard]] m3RayCastResult
cast(const m3World* world, const CharacterShape& shape, m3Pos3 at, m3Vec3 translation) noexcept {
    return m3World_CastCapsuleClosest(world,
                                      at,
                                      m3Vec3{0, -shape.halfHeight, 0},
                                      m3Vec3{0, shape.halfHeight, 0},
                                      thinnerOf(shape),
                                      translation,
                                      shape.filter);
}

/// The sweep of the thinner capsule from `at` along `translation`: the part
/// of it that is clear, from 0 to 1. A sweep that starts inside something
/// is left to the planes, which push out of it.
[[nodiscard]] float
clearPart(const m3World* world, const CharacterShape& shape, m3Pos3 at, m3Vec3 translation) noexcept {
    const float kLength = lengthOf(translation);
    if (!(kLength > 0)) {
        return 0;
    }
    const m3RayCastResult kHit = cast(world, shape, at, translation);
    if (!kHit.hit || (kHit.normal.x == 0 && kHit.normal.y == 0 && kHit.normal.z == 0)) {
        return 1;
    }
    return std::max(0.0F, kHit.fraction - (kSkin / kLength));
}

/// Trace and slide from `at` along `wish`: moves `at`, and says how far. A
/// surface too steep to stand on is a wall: sliding along it never turns a
/// wish that does not rise into one that does, or a character would walk
/// up a cliff.
[[nodiscard]] m3Vec3
slide(const m3World* world, const CharacterShape& shape, float groundNormal, m3Pos3& at, m3Vec3 wish) noexcept {
    Planes planes{};
    m3Vec3 moved{0, 0, 0};
    for (int pass = 0; pass < kPasses; ++pass) {
        const std::int32_t kCount = collide(world, shape, at, planes);
        const m3Vec3 kLeft{wish.x - moved.x, wish.y - moved.y, wish.z - moved.z};
        m3MoverMove solved = m3SolveMover(kLeft, planes.data(), kCount);
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
        const m3Vec3 kStep = scaled(solved.translation, kPart);
        shift(at, kStep);
        moved.x += kStep.x;
        moved.y += kStep.y;
        moved.z += kStep.z;
        if ((kPart == 1 && solved.pressed == 0) || lengthOf(kStep) < kStill) {
            break;
        }
    }
    return moved;
}

/// How far a move went along the horizontal direction `toward` (a unit
/// vector).
[[nodiscard]] float progressOf(m3Vec3 moved, m3Vec3 toward) noexcept {
    return (moved.x * toward.x) + (moved.z * toward.z);
}

/// Walked into something with its `stepHeight` to climb: up, along the
/// horizontal wish, and back down onto ground. Where it lands, if it lands
/// on ground no higher than the step and gets further than `progress` did.
/// The ground is told by a ray down just past the capsule's front, which
/// must meet walkable ground no lower than its feet and no higher above
/// where they started than the step: the tread of a step it stands on the
/// edge of, where a capsule's own contact is the edge's slanted normal. A
/// steep ramp's face is what that ray meets there, so a ramp is never
/// climbed a step at a time, and the top of a wall is too high.
[[nodiscard]] bool climb(const m3World* world,
                         const CharacterShape& shape,
                         const Character3D& character,
                         m3Pos3 from,
                         m3Vec3 wish,
                         float progress,
                         m3Pos3& landed,
                         m3RayCastResult& tread) noexcept {
    const float kAcross = std::sqrt((wish.x * wish.x) + (wish.z * wish.z));
    const m3Vec3 kToward{wish.x / kAcross, 0, wish.z / kAcross};
    // No higher than the character is tall.
    const float kStep = std::min(character.stepHeight, 2 * (shape.halfHeight + shape.radius));
    m3Pos3 at = from;
    const m3Vec3 kUp{0, kStep, 0};
    const float kLift = kStep * clearPart(world, shape, at, kUp);
    shift(at, m3Vec3{0, kLift, 0});
    const m3Vec3 kAhead = slide(world, shape, character.groundNormal, at, m3Vec3{wish.x, 0, wish.z});
    const float kFurther = progressOf(kAhead, kToward);
    if (!(kFurther > progress + kStill)) {
        return false;
    }
    const float kReach = kLift + kSkin;
    const m3RayCastResult kLand = cast(world, shape, at, m3Vec3{0, -kReach, 0});
    if (!kLand.hit || (kLand.normal.x == 0 && kLand.normal.y == 0 && kLand.normal.z == 0)) {
        return false;
    }
    shift(at, m3Vec3{0, -std::max(0.0F, (kLand.fraction * kReach) - kSkin), 0});
    const float kProbe = shape.radius + kTouching;
    const m3Pos3 kOrigin{
        at.x + static_cast<double>(kToward.x * kProbe), at.y, at.z + static_cast<double>(kToward.z * kProbe)};
    const float kDepth = shape.halfHeight + shape.radius + kTouching;
    const m3RayCastResult kFloor = m3World_CastRayClosest(world, kOrigin, m3Vec3{0, -kDepth, 0}, shape.filter);
    const double kBelow = static_cast<double>(shape.halfHeight + shape.radius);
    if (!kFloor.hit || kFloor.normal.y < character.groundNormal ||
        kFloor.point.y < at.y - kBelow - static_cast<double>(2 * kTouching) ||
        kFloor.point.y > from.y - kBelow + static_cast<double>(kStep + kTouching)) {
        return false;
    }
    landed = at;
    tread = kFloor;
    return true;
}

/// What a character at `at` stands on: of the planes it touches, the most
/// upward.
[[nodiscard]] CharacterMove classify(const m3World* world,
                                     const CharacterShape& shape,
                                     const Character3D& character,
                                     m3Pos3 at,
                                     Planes& planes,
                                     std::int32_t& count) noexcept {
    count = collide(world, shape, at, planes);
    CharacterMove found;
    for (std::int32_t index = 0; index < count; ++index) {
        const m3MoverPlane& plane = planes[static_cast<std::size_t>(index)];
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
void push(const m3World* world,
          const CharacterShape& shape,
          const Character3D& character,
          m3Pos3 at,
          m3Vec3 shortBy,
          float seconds,
          const Planes& planes,
          std::int32_t count,
          CharacterMove& move) noexcept {
    if (!(character.mass > 0) || !(character.pushRatio > 0) || !(seconds > 0)) {
        return;
    }
    std::array<float, M3_MOVER_PLANES> stopped{};
    for (std::int32_t index = 0; index < count; ++index) {
        const m3MoverPlane& plane = planes[static_cast<std::size_t>(index)];
        // Ground is stood on, not pushed.
        const float kInto = -dot(shortBy, plane.normal);
        if (plane.separation > kTouching || plane.normal.y >= character.groundNormal || !(kInto > 0)) {
            continue;
        }
        const m3BodyId kBody = m3Shape_GetBody(world, plane.shapeId);
        if (m3Body_GetType(world, kBody) != m3_dynamicBody) {
            continue;
        }
        const float kWeight = m3Body_GetMass(world, kBody);
        if (!(kWeight > 0) || kWeight > character.pushRatio * character.mass) {
            continue;
        }
        // The point of the capsule's side nearest the plane, level with its
        // center.
        const float kOut = shape.radius + std::max(0.0F, plane.separation);
        const m3Pos3 kPoint{at.x - static_cast<double>(plane.normal.x * kOut),
                            at.y - static_cast<double>(plane.normal.y * kOut),
                            at.z - static_cast<double>(plane.normal.z * kOut)};
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
        const float kSpeed = kInto / seconds;
        const float kImpulse = std::min(character.mass, kWeight) * kSpeed;
        if (slot == move.pushCount) {
            ++move.pushCount;
        }
        stopped[static_cast<std::size_t>(slot)] = kInto;
        move.pushes[static_cast<std::size_t>(slot)] =
            CharacterPush{.body = kBody, .impulse = scaled(plane.normal, -kImpulse), .point = kPoint};
    }
}

} // namespace

CharacterMove moveCharacter(const m3World* world,
                            const CharacterShape& shape,
                            m3Pos3 from,
                            m3Vec3 wish,
                            const Character3D& character,
                            bool wasGrounded,
                            float seconds) noexcept {
    m3Pos3 at = from;
    m3Vec3 moved = slide(world, shape, character.groundNormal, at, wish);

    // Grounded, not rising, and stopped short across: a step it may climb.
    bool climbed = false;
    m3RayCastResult tread{};
    const float kAcross = std::sqrt((wish.x * wish.x) + (wish.z * wish.z));
    if (wasGrounded && character.stepHeight > 0 && wish.y <= 0 && kAcross > kStill) {
        const m3Vec3 kToward{wish.x / kAcross, 0, wish.z / kAcross};
        const float kProgress = progressOf(moved, kToward);
        m3Pos3 landed{};
        if (kProgress < kAcross - kStill && climb(world, shape, character, from, wish, kProgress, landed, tread)) {
            at = landed;
            moved = m3Vec3{static_cast<float>(at.x - from.x),
                           static_cast<float>(at.y - from.y),
                           static_cast<float>(at.z - from.z)};
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
        const float kReach = character.snap + kSkin;
        const m3RayCastResult kHit = cast(world, shape, at, m3Vec3{0, -kReach, 0});
        if (kHit.hit && kHit.normal.y >= character.groundNormal) {
            const float kDrop = std::max(0.0F, (kHit.fraction * kReach) - kSkin);
            shift(at, m3Vec3{0, -kDrop, 0});
            moved.y -= kDrop;
            on = classify(world, shape, character, at, planes, count);
        }
    }
    on.translation = moved;
    on.lift = kLift;
    on.climbed = climbed;
    if (!climbed) {
        push(world,
             shape,
             character,
             at,
             m3Vec3{wish.x - moved.x, wish.y - moved.y, wish.z - moved.z},
             seconds,
             planes,
             count,
             on);
    }
    return on;
}

bool characterFits(const m3World* world, const CharacterShape& shape, m3Pos3 at) noexcept {
    // The thinner capsule, so ground it stands a skin above is no overlap.
    std::array<m3ShapeId, 1> overlapped{};
    return m3World_OverlapCapsule(world,
                                  m3Pos3{at.x, at.y - static_cast<double>(shape.halfHeight), at.z},
                                  m3Pos3{at.x, at.y + static_cast<double>(shape.halfHeight), at.z},
                                  thinnerOf(shape),
                                  overlapped.data(),
                                  static_cast<std::int32_t>(overlapped.size()),
                                  shape.filter) == 0;
}

m3Vec3 sweepCharacter(const m3World* world, const CharacterShape& shape, m3Pos3 from, m3Vec3 translation) noexcept {
    return scaled(translation, clearPart(world, shape, from, translation));
}

} // namespace rawframe::physics3d
