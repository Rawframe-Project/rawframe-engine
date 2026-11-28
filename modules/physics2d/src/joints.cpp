#include "joints.h"

#include "rawframe/physics/joint.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>

namespace rawframe::physics2d {

[[nodiscard]] bool same(const Joint2D& left, const Joint2D& right) noexcept {
    constexpr std::array kReals = {
        &Joint2D::anchorAX,     &Joint2D::anchorAY,     &Joint2D::anchorBX,     &Joint2D::anchorBY,
        &Joint2D::axisX,        &Joint2D::axisY,        &Joint2D::linearLowerX, &Joint2D::linearLowerY,
        &Joint2D::linearUpperX, &Joint2D::linearUpperY, &Joint2D::angularLower, &Joint2D::angularUpper,
        &Joint2D::motorSpeed,   &Joint2D::motorEffort,  &Joint2D::breakForce,   &Joint2D::breakTorque,
        &Joint2D::minLength,    &Joint2D::maxLength,    &Joint2D::springHertz,  &Joint2D::springDampingRatio,
        &Joint2D::springLength, &Joint2D::ratio};
    constexpr std::array kWides = {&Joint2D::groundAX, &Joint2D::groundAY, &Joint2D::groundBX, &Joint2D::groundBY};
    constexpr std::array kBytes = {
        &Joint2D::linearX, &Joint2D::linearY, &Joint2D::angular, &Joint2D::motor, &Joint2D::kind};
    return left.a == right.a && left.b == right.b && left.collideConnected == right.collideConnected &&
           left.broken == right.broken &&
           std::ranges::all_of(kWides,
                               [&](double Joint2D::* field) {
                                   return std::bit_cast<std::uint64_t>(left.*field) ==
                                          std::bit_cast<std::uint64_t>(right.*field);
                               }) &&
           std::ranges::all_of(kReals,
                               [&](float Joint2D::* field) {
                                   return std::bit_cast<std::uint32_t>(left.*field) ==
                                          std::bit_cast<std::uint32_t>(right.*field);
                               }) &&
           std::ranges::all_of(kBytes, [&](std::uint8_t Joint2D::* field) {
               return left.*field == right.*field;
           });
}

[[nodiscard]] m2JointId makeJoint(m2World* physics, const Joint2D& joint, m2BodyId a, m2BodyId b) noexcept {
    constexpr auto kLocked = static_cast<std::uint8_t>(physics::JointAxis::Locked);
    constexpr auto kFree = static_cast<std::uint8_t>(physics::JointAxis::Free);
    constexpr auto kLimited = static_cast<std::uint8_t>(physics::JointAxis::Limited);
    const double kLength = std::sqrt((double{joint.axisX} * joint.axisX) + (double{joint.axisY} * joint.axisY));
    if (!std::isfinite(kLength) || joint.linearX > kLimited || joint.linearY > kLimited || joint.angular > kLimited ||
        joint.motor > 3 || !(joint.motorEffort >= 0) || !(joint.breakForce >= 0) || !std::isfinite(joint.breakForce) ||
        !(joint.breakTorque >= 0) || !std::isfinite(joint.breakTorque)) {
        return m2JointId{};
    }
    const m2Vec2 kAxis =
        kLength == 0 ? m2Vec2{1, 0}
                     : m2Vec2{static_cast<float>(joint.axisX / kLength), static_cast<float>(joint.axisY / kLength)};
    const m2Vec2 kAnchorA{joint.anchorAX, joint.anchorAY};
    const m2Vec2 kAnchorB{joint.anchorBX, joint.anchorBY};
    m2JointId made{};
    switch (static_cast<physics::JointKind>(joint.kind)) {
    case physics::JointKind::Generic:
        break;
    case physics::JointKind::Distance: {
        // A rod (no spring, the two lengths equal) is rigid at its length; a
        // rope or a spring keeps to the range and pulls with the spring.
        m2DistanceJointDef definition = m2DefaultDistanceJointDef();
        definition.bodyIdA = a;
        definition.bodyIdB = b;
        definition.localAnchorA = kAnchorA;
        definition.localAnchorB = kAnchorB;
        const bool kRod = !(joint.springHertz > 0) && joint.minLength == joint.maxLength;
        definition.length = joint.springLength > 0 && !kRod ? joint.springLength : joint.maxLength;
        definition.minLength = joint.minLength;
        definition.maxLength = joint.maxLength;
        definition.enableSpring = !kRod;
        definition.springHertz = joint.springHertz;
        definition.springDampingRatio = joint.springDampingRatio;
        definition.enableConnectedCollision = joint.collideConnected;
        return m2CreateDistanceJoint(physics, &definition, &made) == m2_success ? made : m2JointId{};
    }
    case physics::JointKind::Wheel: {
        m2WheelJointDef definition = m2DefaultWheelJointDef();
        definition.bodyIdA = a;
        definition.bodyIdB = b;
        definition.localAnchorA = kAnchorA;
        definition.localAnchorB = kAnchorB;
        definition.localAxisA = kAxis;
        definition.enableSpring = joint.springHertz > 0;
        definition.springHertz = joint.springHertz;
        definition.springDampingRatio = joint.springDampingRatio;
        definition.enableLimit = joint.minLength < joint.maxLength;
        definition.lowerTranslation = joint.minLength;
        definition.upperTranslation = joint.maxLength;
        definition.enableMotor = joint.motor != 0;
        definition.motorSpeed = joint.motorSpeed;
        definition.maxMotorEffort = joint.motorEffort;
        definition.enableConnectedCollision = joint.collideConnected;
        return m2CreateWheelJoint(physics, &definition, &made) == m2_success ? made : m2JointId{};
    }
    case physics::JointKind::Pulley: {
        m2PulleyJointDef definition = m2DefaultPulleyJointDef();
        definition.bodyIdA = a;
        definition.bodyIdB = b;
        definition.groundAnchorA = m2Pos2{joint.groundAX, joint.groundAY};
        definition.groundAnchorB = m2Pos2{joint.groundBX, joint.groundBY};
        definition.localAnchorA = kAnchorA;
        definition.localAnchorB = kAnchorB;
        definition.ratio = joint.ratio;
        definition.enableConnectedCollision = joint.collideConnected;
        return m2CreatePulleyJoint(physics, &definition, &made) == m2_success ? made : m2JointId{};
    }
    case physics::JointKind::Gear: {
        m2GearJointDef definition = m2DefaultGearJointDef();
        definition.bodyIdA = a;
        definition.bodyIdB = b;
        definition.ratio = joint.ratio;
        definition.enableConnectedCollision = joint.collideConnected;
        return m2CreateGearJoint(physics, &definition, &made) == m2_success ? made : m2JointId{};
    }
    default:
        return m2JointId{};
    }
    const bool kLinearLocked = joint.linearX == kLocked && joint.linearY == kLocked;
    if (kLinearLocked && joint.angular == kLocked && joint.motor == 0) {
        m2WeldJointDef definition = m2DefaultWeldJointDef();
        definition.bodyIdA = a;
        definition.bodyIdB = b;
        definition.localAnchorA = kAnchorA;
        definition.localAnchorB = kAnchorB;
        definition.enableConnectedCollision = joint.collideConnected;
        return m2CreateWeldJoint(physics, &definition, &made) == m2_success ? made : m2JointId{};
    }
    if (kLinearLocked && joint.angular != kLocked && (joint.motor == 0 || joint.motor == 3)) {
        m2RevoluteJointDef definition = m2DefaultRevoluteJointDef();
        definition.bodyIdA = a;
        definition.bodyIdB = b;
        definition.localAnchorA = kAnchorA;
        definition.localAnchorB = kAnchorB;
        definition.enableLimit = joint.angular == kLimited;
        definition.lowerAngle = joint.angularLower;
        definition.upperAngle = joint.angularUpper;
        definition.enableSpring = joint.springHertz > 0;
        definition.springHertz = joint.springHertz;
        definition.springDampingRatio = joint.springDampingRatio;
        definition.enableMotor = joint.motor == 3;
        definition.motorSpeed = joint.motorSpeed;
        definition.maxMotorEffort = joint.motorEffort;
        definition.enableConnectedCollision = joint.collideConnected;
        return m2CreateRevoluteJoint(physics, &definition, &made) == m2_success ? made : m2JointId{};
    }
    const bool kAlongX = joint.linearX != kLocked && joint.linearY == kLocked;
    const bool kAlongY = joint.linearY != kLocked && joint.linearX == kLocked;
    if ((kAlongX || kAlongY) && joint.angular == kLocked && (joint.motor == 0 || joint.motor == (kAlongX ? 1 : 2))) {
        m2PrismaticJointDef definition = m2DefaultPrismaticJointDef();
        definition.bodyIdA = a;
        definition.bodyIdB = b;
        definition.localAnchorA = kAnchorA;
        definition.localAnchorB = kAnchorB;
        definition.localAxisA = kAlongX ? kAxis : m2Vec2{-kAxis.y, kAxis.x};
        definition.enableLimit = (kAlongX ? joint.linearX : joint.linearY) == kLimited;
        definition.lowerTranslation = kAlongX ? joint.linearLowerX : joint.linearLowerY;
        definition.upperTranslation = kAlongX ? joint.linearUpperX : joint.linearUpperY;
        definition.enableMotor = joint.motor != 0;
        definition.motorSpeed = joint.motorSpeed;
        definition.maxMotorEffort = joint.motorEffort;
        definition.enableConnectedCollision = joint.collideConnected;
        return m2CreatePrismaticJoint(physics, &definition, &made) == m2_success ? made : m2JointId{};
    }
    if (joint.linearX == kFree && joint.linearY == kFree && joint.angular == kFree && joint.motor == 0 &&
        !joint.collideConnected) {
        // Nothing held: the pair only stops colliding.
        m2FilterJointDef definition = m2DefaultFilterJointDef();
        definition.bodyIdA = a;
        definition.bodyIdB = b;
        return m2CreateFilterJoint(physics, &definition, &made) == m2_success ? made : m2JointId{};
    }
    return m2JointId{};
}

} // namespace rawframe::physics2d
