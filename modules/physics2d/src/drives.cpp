#include "drives.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>

namespace rawframe::physics2d {

namespace {

/// Maul2D's bound on a force, a torque, and a drive's budget.
constexpr float kForceLimit = 1e20F;
/// The bounds on a drive's spring: Maul3D's, the same in both dimensions.
constexpr float kHertzLimit = 1e5F;
constexpr float kRatioLimit = 1e4F;

[[nodiscard]] bool within(float value, float limit) noexcept {
    return std::isfinite(value) && std::abs(value) <= limit;
}

/// Bit for bit, the padding a component has left out.
[[nodiscard]] bool same(float left, float right) noexcept {
    return std::bit_cast<std::uint32_t>(left) == std::bit_cast<std::uint32_t>(right);
}
[[nodiscard]] bool same(double left, double right) noexcept {
    return std::bit_cast<std::uint64_t>(left) == std::bit_cast<std::uint64_t>(right);
}

/// Whether two drives pull alike, wherever they aim.
[[nodiscard]] bool sameSpring(const Align2D& left, const Align2D& right) noexcept {
    return same(left.hertz, right.hertz) && same(left.dampingRatio, right.dampingRatio) &&
           same(left.maxForce, right.maxForce) && same(left.maxTorque, right.maxTorque);
}

[[nodiscard]] bool sameAim(const Align2D& left, const Align2D& right) noexcept {
    return same(left.x, right.x) && same(left.y, right.y) && same(left.c, right.c) && same(left.s, right.s);
}

/// Whether a drive can be made of these values.
[[nodiscard]] bool makeable(const Align2D& align) noexcept {
    return std::isfinite(align.x) && std::isfinite(align.y) && std::isfinite(align.c) && std::isfinite(align.s) &&
           align.hertz > 0 && align.hertz <= kHertzLimit && align.dampingRatio >= 0 &&
           align.dampingRatio <= kRatioLimit && align.maxForce >= 0 && align.maxForce <= kForceLimit &&
           align.maxTorque >= 0 && align.maxTorque <= kForceLimit && (align.maxForce > 0 || align.maxTorque > 0);
}

/// The angle a drive aims at, made unit; all noughts as none.
[[nodiscard]] m2Rot angleOf(const Align2D& align) noexcept {
    const double kLength = std::sqrt((double{align.c} * align.c) + (double{align.s} * align.s));
    if (!(kLength > 0)) {
        return m2Rot{1, 0};
    }
    return m2Rot{static_cast<float>(align.c / kLength), static_cast<float>(align.s / kLength)};
}

/// The made dynamic body of an entity, or the null body.
[[nodiscard]] m2BodyId bodyOf(const DynamicBodies& bodies, world::EntityHandle entity) noexcept {
    const auto kFound = std::ranges::lower_bound(bodies, entity, {}, &std::pair<world::EntityHandle, m2BodyId>::first);
    return kFound != bodies.end() && kFound->first == entity ? kFound->second : m2BodyId{};
}

[[nodiscard]] bool sameBody(m2BodyId left, m2BodyId right) noexcept {
    return left.index1 == right.index1 && left.generation == right.generation;
}

} // namespace

result::Result<Drives> Drives::resolve(const schema::SchemaRegistry& registry) {
    Drives drives;
    RAWFRAME_TRY_ASSIGN(drives.forces_, (world::Query<world::Read<Force2D>>::resolve(registry)));
    RAWFRAME_TRY_ASSIGN(drives.aligns_, (world::Query<world::Read<Align2D>>::resolve(registry)));
    return drives;
}

std::vector<schema::ComponentRuntimeId> Drives::reads() const {
    std::vector<schema::ComponentRuntimeId> reads = forces_->reads();
    const std::vector<schema::ComponentRuntimeId> kAligns = aligns_->reads();
    reads.insert(reads.end(), kAligns.begin(), kAligns.end());
    return reads;
}

void Drives::apply(world::World& world,
                   m2World* physics,
                   const DynamicBodies& bodies,
                   Physics2DStatistics& statistics) {
    push(world, physics, bodies, statistics);
    align(world, physics, bodies, statistics);
}

void Drives::push(world::World& world, m2World* physics, const DynamicBodies& bodies, Physics2DStatistics& statistics) {
    forceRows_.clear();
    forces_->forEach(world, [this](world::EntityHandle entity, const Force2D& force) {
        forceRows_.emplace_back(entity, &force);
    });
    std::ranges::sort(forceRows_, {}, &std::pair<world::EntityHandle, const Force2D*>::first);
    for (const auto& [kEntity, kForce] : forceRows_) {
        const m2BodyId kBody = bodyOf(bodies, kEntity);
        const Force2D& force = *kForce;
        if (kBody.index1 == 0 || !within(force.x, kForceLimit) || !within(force.y, kForceLimit) ||
            !within(force.torque, kForceLimit) || !within(force.pointX, kForceLimit) ||
            !within(force.pointY, kForceLimit)) {
            continue;
        }
        m2Vec2 pushed{force.x, force.y};
        if (force.local) {
            pushed = m2Body_GetWorldVector(physics, kBody, pushed);
        }
        if (pushed.x != 0 || pushed.y != 0) {
            if (force.atPoint) {
                m2Body_ApplyForceAtPoint(
                    physics, kBody, pushed, m2Body_GetWorldPoint(physics, kBody, m2Vec2{force.pointX, force.pointY}));
            } else {
                m2Body_ApplyForce(physics, kBody, pushed);
            }
        }
        if (force.torque != 0) {
            m2Body_ApplyTorque(physics, kBody, force.torque);
        }
        ++statistics.forcesApplied;
    }
}

void Drives::align(world::World& world,
                   m2World* physics,
                   const DynamicBodies& bodies,
                   Physics2DStatistics& statistics) {
    alignRows_.clear();
    aligns_->forEach(world, [this](world::EntityHandle entity, const Align2D& align) {
        alignRows_.emplace_back(entity, &align);
    });
    std::ranges::sort(alignRows_, {}, &std::pair<world::EntityHandle, const Align2D*>::first);
    for (auto entry = aligned_.begin(); entry != aligned_.end();) {
        if (std::ranges::binary_search(
                alignRows_, entry->first, {}, &std::pair<world::EntityHandle, const Align2D*>::first)) {
            ++entry;
            continue;
        }
        remove(physics, entry->second);
        entry = aligned_.erase(entry);
    }
    for (const auto& [kEntity, kAlign] : alignRows_) {
        const Align2D& wanted = *kAlign;
        const m2BodyId kBody = bodyOf(bodies, kEntity);
        const auto [kEntry, kNew] = aligned_.try_emplace(kEntity);
        Aligned& entry = kEntry->second;
        // A drive whose body was made again went with the body; one that
        // only aims elsewhere moves its anchor.
        if (!kNew && sameSpring(entry.made, wanted) && sameBody(entry.body, kBody)) {
            if (entry.refused && sameAim(entry.made, wanted)) {
                continue;
            }
            if (!entry.refused && m2Joint_IsValid(physics, entry.joint) &&
                (sameAim(entry.made, wanted) || makeable(wanted))) {
                if (!sameAim(entry.made, wanted)) {
                    m2Body_SetTransform(physics, entry.anchor, m2Pos2{wanted.x, wanted.y}, angleOf(wanted));
                    m2Body_SetAwake(physics, kBody, true);
                    entry.made = wanted;
                }
                continue;
            }
        }
        remove(physics, entry);
        entry = Aligned{.body = kBody, .made = wanted, .refused = true};
        if (kBody.index1 != 0 && makeable(wanted)) {
            m2BodyDef anchor = m2DefaultBodyDef();
            anchor.type = m2_kinematicBody;
            anchor.position = m2Pos2{wanted.x, wanted.y};
            anchor.rotation = angleOf(wanted);
            m2BodyId made{};
            if (m2CreateBody(physics, &anchor, &made) == m2_success) {
                m2MotorJointDef definition = m2DefaultMotorJointDef();
                definition.bodyIdA = made;
                definition.bodyIdB = kBody;
                definition.linearOffset = m2Vec2{0, 0};
                definition.angularOffset = 0;
                definition.maxForce = wanted.maxForce;
                definition.maxTorque = wanted.maxTorque;
                definition.springHertz = wanted.hertz;
                definition.springDampingRatio = wanted.dampingRatio;
                m2JointId joint{};
                if (m2CreateMotorJoint(physics, &definition, &joint) == m2_success) {
                    entry.anchor = made;
                    entry.joint = joint;
                    entry.refused = false;
                } else {
                    // A body made a moment ago, with nothing joined to it.
                    static_cast<void>(m2DestroyBody(physics, made));
                }
            }
        }
        ++(entry.refused ? statistics.alignsRefused : statistics.alignsMade);
    }
}

void Drives::remove(m2World* physics, Aligned& aligned) noexcept {
    if (aligned.refused) {
        return;
    }
    if (m2Joint_IsValid(physics, aligned.joint)) {
        static_cast<void>(m2DestroyJoint(physics, aligned.joint));
    }
    if (m2Body_IsValid(physics, aligned.anchor)) {
        static_cast<void>(m2DestroyBody(physics, aligned.anchor));
    }
    aligned.joint = {};
    aligned.anchor = {};
    aligned.refused = true;
}

} // namespace rawframe::physics2d
