#include "drives.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>
#include <limits>

namespace rawframe::physics3d {

namespace {

/// Maul3D's bound on a force, a torque, and a drive's budget.
constexpr float kForceLimit = 1e20F;
/// Maul3D's bounds on a spring's frequency and damping ratio.
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
[[nodiscard]] bool sameSpring(const Align3D& left, const Align3D& right) noexcept {
    return same(left.hertz, right.hertz) && same(left.dampingRatio, right.dampingRatio) &&
           same(left.maxForce, right.maxForce) && same(left.maxTorque, right.maxTorque);
}

[[nodiscard]] bool sameAim(const Align3D& left, const Align3D& right) noexcept {
    return same(left.x, right.x) && same(left.y, right.y) && same(left.z, right.z) && same(left.qx, right.qx) &&
           same(left.qy, right.qy) && same(left.qz, right.qz) && same(left.qw, right.qw);
}

/// Whether a drive can be made of these values.
[[nodiscard]] bool makeable(const Align3D& align) noexcept {
    return std::isfinite(align.x) && std::isfinite(align.y) && std::isfinite(align.z) && finite(align.qx) &&
           finite(align.qy) && finite(align.qz) && finite(align.qw) && align.hertz > 0 && align.hertz <= kHertzLimit &&
           align.dampingRatio >= 0 && align.dampingRatio <= kRatioLimit && align.maxForce >= 0 &&
           align.maxForce <= kForceLimit && align.maxTorque >= 0 && align.maxTorque <= kForceLimit &&
           (align.maxForce > 0 || align.maxTorque > 0);
}

/// Where a drive aims, as a pose.
[[nodiscard]] Pose3D aimOf(const Align3D& align) noexcept {
    return Pose3D{
        .x = align.x, .y = align.y, .z = align.z, .qx = align.qx, .qy = align.qy, .qz = align.qz, .qw = align.qw};
}

/// A budget as Maul3D takes it, where nought is none at all: the least
/// there is, for a way the drive does not pull.
[[nodiscard]] float budgetOf(float budget) noexcept {
    return budget > 0 ? budget : std::numeric_limits<float>::min();
}

/// The body a drive or a force acts on: the entity's made dynamic body.
[[nodiscard]] m3BodyId dynamicBodyOf(const std::map<world::EntityHandle, Mapped>& mapped,
                                     world::EntityHandle entity) noexcept {
    const auto kFound = mapped.find(entity);
    if (kFound == mapped.end() || kFound->second.refused ||
        kFound->second.made.motion != static_cast<std::uint8_t>(physics::Motion::Dynamic)) {
        return m3BodyId{};
    }
    return kFound->second.body;
}

} // namespace

result::Result<Drives> Drives::resolve(const schema::SchemaRegistry& registry) {
    Drives drives;
    RAWFRAME_TRY_ASSIGN(drives.forces_, (world::Query<world::Read<Force3D>>::resolve(registry)));
    RAWFRAME_TRY_ASSIGN(drives.aligns_, (world::Query<world::Read<Align3D>>::resolve(registry)));
    return drives;
}

std::vector<schema::ComponentRuntimeId> Drives::reads() const {
    std::vector<schema::ComponentRuntimeId> reads = forces_->reads();
    const std::vector<schema::ComponentRuntimeId> kAligns = aligns_->reads();
    reads.insert(reads.end(), kAligns.begin(), kAligns.end());
    return reads;
}

void Drives::apply(world::World& world,
                   m3World* physics,
                   const std::map<world::EntityHandle, Mapped>& mapped,
                   Physics3DStatistics& statistics) {
    push(world, physics, mapped, statistics);
    align(world, physics, mapped, statistics);
}

void Drives::push(world::World& world,
                  m3World* physics,
                  const std::map<world::EntityHandle, Mapped>& mapped,
                  Physics3DStatistics& statistics) {
    forceRows_.clear();
    forces_->forEach(world, [this](world::EntityHandle entity, const Force3D& force) {
        forceRows_.emplace_back(entity, &force);
    });
    std::ranges::sort(forceRows_, {}, &std::pair<world::EntityHandle, const Force3D*>::first);
    for (const auto& [kEntity, kForce] : forceRows_) {
        const m3BodyId kBody = dynamicBodyOf(mapped, kEntity);
        const Force3D& force = *kForce;
        if (kBody.index1 == 0 || !within(force.x, kForceLimit) || !within(force.y, kForceLimit) ||
            !within(force.z, kForceLimit) || !within(force.torqueX, kForceLimit) ||
            !within(force.torqueY, kForceLimit) || !within(force.torqueZ, kForceLimit) ||
            !within(force.pointX, kForceLimit) || !within(force.pointY, kForceLimit) ||
            !within(force.pointZ, kForceLimit)) {
            continue;
        }
        m3Vec3 pushed{force.x, force.y, force.z};
        m3Vec3 turned{force.torqueX, force.torqueY, force.torqueZ};
        if (force.local) {
            pushed = m3Body_GetWorldVector(physics, kBody, pushed);
            turned = m3Body_GetWorldVector(physics, kBody, turned);
        }
        if (pushed.x != 0 || pushed.y != 0 || pushed.z != 0) {
            if (force.atPoint) {
                m3Body_ApplyForceAtPoint(
                    physics,
                    kBody,
                    pushed,
                    m3Body_GetWorldPoint(physics, kBody, m3Vec3{force.pointX, force.pointY, force.pointZ}));
            } else {
                m3Body_ApplyForce(physics, kBody, pushed);
            }
        }
        if (turned.x != 0 || turned.y != 0 || turned.z != 0) {
            m3Body_ApplyTorque(physics, kBody, turned);
        }
        ++statistics.forcesApplied;
    }
}

void Drives::align(world::World& world,
                   m3World* physics,
                   const std::map<world::EntityHandle, Mapped>& mapped,
                   Physics3DStatistics& statistics) {
    alignRows_.clear();
    aligns_->forEach(world, [this](world::EntityHandle entity, const Align3D& align) {
        alignRows_.emplace_back(entity, &align);
    });
    std::ranges::sort(alignRows_, {}, &std::pair<world::EntityHandle, const Align3D*>::first);
    for (auto entry = aligned_.begin(); entry != aligned_.end();) {
        if (std::ranges::binary_search(
                alignRows_, entry->first, {}, &std::pair<world::EntityHandle, const Align3D*>::first)) {
            ++entry;
            continue;
        }
        remove(physics, entry->second);
        entry = aligned_.erase(entry);
    }
    for (const auto& [kEntity, kAlign] : alignRows_) {
        const Align3D& wanted = *kAlign;
        const m3BodyId kBody = dynamicBodyOf(mapped, kEntity);
        const auto [kEntry, kNew] = aligned_.try_emplace(kEntity);
        Aligned& entry = kEntry->second;
        // A drive whose body was made again went with the body; one that
        // only aims elsewhere moves its anchor.
        if (!kNew && sameSpring(entry.made, wanted) && sameBody(entry.body, kBody)) {
            if (entry.refused && sameAim(entry.made, wanted)) {
                continue;
            }
            if (!entry.refused && m3Joint_IsValid(physics, entry.joint) &&
                (sameAim(entry.made, wanted) || makeable(wanted))) {
                if (!sameAim(entry.made, wanted)) {
                    const Pose3D kAim = aimOf(wanted);
                    m3Body_SetTransform(physics, entry.anchor, m3Pos3{kAim.x, kAim.y, kAim.z}, rotationOf(kAim));
                    m3Body_SetAwake(physics, kBody, true);
                    entry.made = wanted;
                }
                continue;
            }
        }
        remove(physics, entry);
        entry = Aligned{.body = kBody, .made = wanted, .refused = true};
        if (kBody.index1 != 0 && makeable(wanted)) {
            const Pose3D kAim = aimOf(wanted);
            m3BodyDef anchor = m3DefaultBodyDef();
            anchor.type = m3_kinematicBody;
            anchor.position = m3Pos3{kAim.x, kAim.y, kAim.z};
            // The joint's frames meet at create: made turned as the body
            // is, then turned to the aim, the anchor's rotation is the
            // body's goal.
            anchor.rotation = m3Body_GetRotation(physics, kBody);
            m3BodyId made{};
            if (m3CreateBody(physics, &anchor, &made) == m3_success) {
                m3JointDef definition = m3DefaultJointDef();
                definition.type = m3_motorJoint;
                definition.bodyIdA = made;
                definition.bodyIdB = kBody;
                m3JointId joint{};
                if (m3CreateJoint(physics, &definition, &joint) == m3_success) {
                    m3Joint_SetMotorPose(physics, joint, m3Vec3{0, 0, 0}, m3Quat{0, 0, 0, 1});
                    m3Joint_SetSpring(physics, joint, true, wanted.hertz, wanted.dampingRatio);
                    m3Joint_SetLimits(physics, joint, true, budgetOf(wanted.maxForce), budgetOf(wanted.maxTorque));
                    m3Body_SetTransform(physics, made, anchor.position, rotationOf(kAim));
                    entry.anchor = made;
                    entry.joint = joint;
                    entry.refused = false;
                } else {
                    // A body made a moment ago, with nothing joined to it.
                    static_cast<void>(m3DestroyBody(physics, made));
                }
            }
        }
        ++(entry.refused ? statistics.alignsRefused : statistics.alignsMade);
    }
}

void Drives::remove(m3World* physics, Aligned& aligned) noexcept {
    if (aligned.refused) {
        return;
    }
    if (m3Joint_IsValid(physics, aligned.joint)) {
        static_cast<void>(m3DestroyJoint(physics, aligned.joint));
    }
    if (m3Body_IsValid(physics, aligned.anchor)) {
        static_cast<void>(m3DestroyBody(physics, aligned.anchor));
    }
    aligned.joint = {};
    aligned.anchor = {};
    aligned.refused = true;
}

} // namespace rawframe::physics3d
