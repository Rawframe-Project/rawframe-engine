#include "joints.h"

#include "rawframe/physics/joint.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>

namespace rawframe::physics3d {

namespace {

/// A joint's axis in a body's frame: unit, all noughts as +z; none for one
/// that is not finite.
[[nodiscard]] std::optional<m3Vec3> jointAxis(float x, float y, float z) noexcept {
    const double kLength = std::sqrt((double{x} * x) + (double{y} * y) + (double{z} * z));
    if (!std::isfinite(kLength)) {
        return std::nullopt;
    }
    if (kLength == 0) {
        return m3Vec3{0, 0, 1};
    }
    return m3Vec3{static_cast<float>(x / kLength), static_cast<float>(y / kLength), static_cast<float>(z / kLength)};
}

} // namespace

/// Field by field, for the padding a Joint3D may have.
bool same(const Joint3D& left, const Joint3D& right) noexcept {
    constexpr std::array kReals = {
        &Joint3D::anchorAX,      &Joint3D::anchorAY,      &Joint3D::anchorAZ,      &Joint3D::anchorBX,
        &Joint3D::anchorBY,      &Joint3D::anchorBZ,      &Joint3D::axisAX,        &Joint3D::axisAY,
        &Joint3D::axisAZ,        &Joint3D::axisBX,        &Joint3D::axisBY,        &Joint3D::axisBZ,
        &Joint3D::linearLowerX,  &Joint3D::linearLowerY,  &Joint3D::linearLowerZ,  &Joint3D::linearUpperX,
        &Joint3D::linearUpperY,  &Joint3D::linearUpperZ,  &Joint3D::angularLowerX, &Joint3D::angularLowerY,
        &Joint3D::angularLowerZ, &Joint3D::angularUpperX, &Joint3D::angularUpperY, &Joint3D::angularUpperZ,
        &Joint3D::motorSpeed,    &Joint3D::motorEffort,   &Joint3D::breakForce,    &Joint3D::breakTorque,
        &Joint3D::minLength,     &Joint3D::maxLength,     &Joint3D::springHertz,   &Joint3D::springDampingRatio,
        &Joint3D::springLength,  &Joint3D::ratio};
    constexpr std::array kWides = {&Joint3D::groundAX,
                                   &Joint3D::groundAY,
                                   &Joint3D::groundAZ,
                                   &Joint3D::groundBX,
                                   &Joint3D::groundBY,
                                   &Joint3D::groundBZ};
    constexpr std::array kBytes = {&Joint3D::linearX,
                                   &Joint3D::linearY,
                                   &Joint3D::linearZ,
                                   &Joint3D::angularX,
                                   &Joint3D::angularY,
                                   &Joint3D::angularZ,
                                   &Joint3D::motor,
                                   &Joint3D::kind};
    return left.a == right.a && left.b == right.b && left.collideConnected == right.collideConnected &&
           left.broken == right.broken &&
           std::ranges::all_of(kWides,
                               [&](double Joint3D::* field) {
                                   return std::bit_cast<std::uint64_t>(left.*field) ==
                                          std::bit_cast<std::uint64_t>(right.*field);
                               }) &&
           std::ranges::all_of(kReals,
                               [&](float Joint3D::* field) {
                                   return std::bit_cast<std::uint32_t>(left.*field) ==
                                          std::bit_cast<std::uint32_t>(right.*field);
                               }) &&
           std::ranges::all_of(kBytes, [&](std::uint8_t Joint3D::* field) {
               return left.*field == right.*field;
           });
}

/// The Maul3D joint a Joint3D is, between two bodies; none for values out
/// of range. Maul3D refuses the rest (a limit's order, the angular contract,
/// a motor on a locked axis, a ratio out of range) when the joint is made.
std::optional<m3JointDef> jointDef(const Joint3D& joint, m3BodyId a, m3BodyId b) noexcept {
    const auto kAxisA = jointAxis(joint.axisAX, joint.axisAY, joint.axisAZ);
    const auto kAxisB = jointAxis(joint.axisBX, joint.axisBY, joint.axisBZ);
    const std::array<std::uint8_t, 6> kModes = {
        joint.linearX, joint.linearY, joint.linearZ, joint.angularX, joint.angularY, joint.angularZ};
    if (!kAxisA || !kAxisB || joint.motor > 6 || !(joint.breakForce >= 0) || !std::isfinite(joint.breakForce) ||
        !(joint.breakTorque >= 0) || !std::isfinite(joint.breakTorque) ||
        std::ranges::any_of(kModes, [](std::uint8_t mode) {
            return mode > static_cast<std::uint8_t>(physics::JointAxis::Limited);
        })) {
        return std::nullopt;
    }
    m3JointDef definition = m3DefaultJointDef();
    definition.bodyIdA = a;
    definition.bodyIdB = b;
    definition.localAnchorA = m3Vec3{joint.anchorAX, joint.anchorAY, joint.anchorAZ};
    definition.localAnchorB = m3Vec3{joint.anchorBX, joint.anchorBY, joint.anchorBZ};
    definition.localAxisA = *kAxisA;
    definition.localAxisB = *kAxisB;
    definition.collideConnected = joint.collideConnected;
    switch (static_cast<physics::JointKind>(joint.kind)) {
    case physics::JointKind::Generic:
        break;
    case physics::JointKind::Distance:
        // The range always; the spring rides on the motor fields, and its
        // rest length on the cone angle, as Maul3D documents.
        definition.type = m3_distanceJoint;
        definition.enableLimit = true;
        definition.lowerLimit = joint.minLength;
        definition.upperLimit = joint.maxLength;
        definition.enableMotor = joint.springHertz > 0;
        definition.motorSpeed = joint.springHertz;
        definition.maxMotorEffort = joint.springDampingRatio;
        definition.coneAngle = joint.springLength;
        return definition;
    case physics::JointKind::Wheel:
        // The suspension's spring is set once the joint is made (spring).
        definition.type = m3_wheelJoint;
        definition.enableLimit = joint.minLength < joint.maxLength;
        definition.lowerLimit = joint.minLength;
        definition.upperLimit = joint.maxLength;
        definition.enableMotor = joint.motor != 0;
        definition.motorSpeed = joint.motorSpeed;
        definition.maxMotorEffort = joint.motorEffort;
        return definition;
    case physics::JointKind::Pulley:
        definition.type = m3_pulleyJoint;
        definition.ratio = joint.ratio;
        definition.groundAnchorA = m3Pos3{joint.groundAX, joint.groundAY, joint.groundAZ};
        definition.groundAnchorB = m3Pos3{joint.groundBX, joint.groundBY, joint.groundBZ};
        return definition;
    case physics::JointKind::Gear:
        definition.type = m3_gearJoint;
        definition.ratio = joint.ratio;
        return definition;
    default:
        return std::nullopt;
    }
    definition.type = m3_genericJoint;
    // The modes are Maul3D's own numbers: locked, free, limited.
    for (std::size_t axis = 0; axis < 3; ++axis) {
        definition.genericLinear[axis] = kModes[axis];
        definition.genericAngular[axis] = kModes[3 + axis];
    }
    definition.genericLinearLower[0] = joint.linearLowerX;
    definition.genericLinearLower[1] = joint.linearLowerY;
    definition.genericLinearLower[2] = joint.linearLowerZ;
    definition.genericLinearUpper[0] = joint.linearUpperX;
    definition.genericLinearUpper[1] = joint.linearUpperY;
    definition.genericLinearUpper[2] = joint.linearUpperZ;
    definition.genericAngularLower[0] = joint.angularLowerX;
    definition.genericAngularLower[1] = joint.angularLowerY;
    definition.genericAngularLower[2] = joint.angularLowerZ;
    definition.genericAngularUpper[0] = joint.angularUpperX;
    definition.genericAngularUpper[1] = joint.angularUpperY;
    definition.genericAngularUpper[2] = joint.angularUpperZ;
    definition.genericMotorAxis = joint.motor == 0 ? std::uint8_t{255} : static_cast<std::uint8_t>(joint.motor - 1);
    definition.motorSpeed = joint.motorSpeed;
    definition.maxMotorEffort = joint.motorEffort;
    return definition;
}

bool spring(m3World* physics, m3JointId made, const Joint3D& joint) noexcept {
    if (static_cast<physics::JointKind>(joint.kind) != physics::JointKind::Wheel || !(joint.springHertz > 0)) {
        return true;
    }
    if (!(joint.springHertz <= 1e5F) || !(joint.springDampingRatio >= 0) || !(joint.springDampingRatio <= 1e4F)) {
        return false;
    }
    // Toward where it was made, as Maul2D's wheel rests.
    m3Joint_SetSpring(physics, made, true, joint.springHertz, joint.springDampingRatio);
    m3Joint_SetTargetTranslation(physics, made, 0);
    return true;
}

} // namespace rawframe::physics3d
