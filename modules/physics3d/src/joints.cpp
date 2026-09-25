#include "joints.h"

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
        &Joint3D::motorSpeed,    &Joint3D::motorEffort,   &Joint3D::breakForce,    &Joint3D::breakTorque};
    constexpr std::array kBytes = {&Joint3D::linearX,
                                   &Joint3D::linearY,
                                   &Joint3D::linearZ,
                                   &Joint3D::angularX,
                                   &Joint3D::angularY,
                                   &Joint3D::angularZ,
                                   &Joint3D::motor};
    return left.a == right.a && left.b == right.b && left.collideConnected == right.collideConnected &&
           left.broken == right.broken &&
           std::ranges::all_of(kReals,
                               [&](float Joint3D::* field) {
                                   return std::bit_cast<std::uint32_t>(left.*field) ==
                                          std::bit_cast<std::uint32_t>(right.*field);
                               }) &&
           std::ranges::all_of(kBytes, [&](std::uint8_t Joint3D::* field) {
               return left.*field == right.*field;
           });
}

/// The Maul3D generic joint a Joint3D is, between two bodies; none for
/// values out of range. Maul3D refuses the rest (a limit's order, the
/// angular contract, a motor on a locked axis) when the joint is made.
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
    definition.type = m3_genericJoint;
    definition.bodyIdA = a;
    definition.bodyIdB = b;
    definition.localAnchorA = m3Vec3{joint.anchorAX, joint.anchorAY, joint.anchorAZ};
    definition.localAnchorB = m3Vec3{joint.anchorBX, joint.anchorBY, joint.anchorBZ};
    definition.localAxisA = *kAxisA;
    definition.localAxisB = *kAxisB;
    definition.collideConnected = joint.collideConnected;
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

} // namespace rawframe::physics3d
