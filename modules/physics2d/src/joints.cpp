#include "joints.h"

#include "rawframe/physics/joint.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>

namespace rawframe::physics2d {

[[nodiscard]] bool same(const Joint2D& left, const Joint2D& right) noexcept {
    constexpr std::array kReals = {&Joint2D::anchorAX,
                                   &Joint2D::anchorAY,
                                   &Joint2D::anchorBX,
                                   &Joint2D::anchorBY,
                                   &Joint2D::axisX,
                                   &Joint2D::axisY,
                                   &Joint2D::linearLowerX,
                                   &Joint2D::linearLowerY,
                                   &Joint2D::linearUpperX,
                                   &Joint2D::linearUpperY,
                                   &Joint2D::angularLower,
                                   &Joint2D::angularUpper,
                                   &Joint2D::motorSpeed,
                                   &Joint2D::motorEffort,
                                   &Joint2D::breakForce,
                                   &Joint2D::breakTorque};
    constexpr std::array kBytes = {&Joint2D::linearX, &Joint2D::linearY, &Joint2D::angular, &Joint2D::motor};
    return left.a == right.a && left.b == right.b && left.collideConnected == right.collideConnected &&
           left.broken == right.broken &&
           std::ranges::all_of(kReals,
                               [&](float Joint2D::* field) {
                                   return std::bit_cast<std::uint32_t>(left.*field) ==
                                          std::bit_cast<std::uint32_t>(right.*field);
                               }) &&
           std::ranges::all_of(kBytes, [&](std::uint8_t Joint2D::* field) {
               return left.*field == right.*field;
           });
}

[[nodiscard]] m2JointId makeJoint(m2WorldId physics, const Joint2D& joint, m2BodyId a, m2BodyId b) noexcept {
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
    const bool kLinearLocked = joint.linearX == kLocked && joint.linearY == kLocked;
    if (kLinearLocked && joint.angular == kLocked && joint.motor == 0) {
        m2WeldJointDef definition = m2DefaultWeldJointDef();
        definition.bodyIdA = a;
        definition.bodyIdB = b;
        definition.localAnchorA = kAnchorA;
        definition.localAnchorB = kAnchorB;
        definition.collideConnected = joint.collideConnected;
        return m2CreateWeldJoint(physics, &definition);
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
        definition.enableMotor = joint.motor == 3;
        definition.motorSpeed = joint.motorSpeed;
        definition.maxMotorTorque = joint.motorEffort;
        definition.collideConnected = joint.collideConnected;
        return m2CreateRevoluteJoint(physics, &definition);
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
        definition.maxMotorForce = joint.motorEffort;
        definition.collideConnected = joint.collideConnected;
        return m2CreatePrismaticJoint(physics, &definition);
    }
    if (joint.linearX == kFree && joint.linearY == kFree && joint.angular == kFree && joint.motor == 0 &&
        !joint.collideConnected) {
        // Nothing held: the pair only stops colliding.
        m2FilterJointDef definition = m2DefaultFilterJointDef();
        definition.bodyIdA = a;
        definition.bodyIdB = b;
        return m2CreateFilterJoint(physics, &definition);
    }
    return m2JointId{};
}

} // namespace rawframe::physics2d
