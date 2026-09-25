#include "rawframe/physics2d/components.h"

#include <array>

namespace rawframe::physics2d {

namespace {

using schema::ComponentField;
using schema::ComponentLayout;
using schema::FieldType;

constexpr std::array<ComponentField, 13> kBodyFields = {{
    {"motion", offsetof(Body2D, motion), FieldType::U8},
    {"shape", offsetof(Body2D, shape), FieldType::U8},
    {"fixedRotation", offsetof(Body2D, fixedRotation), FieldType::Bool},
    {"bullet", offsetof(Body2D, bullet), FieldType::Bool},
    {"sensor", offsetof(Body2D, sensor), FieldType::Bool},
    {"collisionClass", offsetof(Body2D, collisionClass), FieldType::U64},
    {"width", offsetof(Body2D, width), FieldType::F32},
    {"height", offsetof(Body2D, height), FieldType::F32},
    {"density", offsetof(Body2D, density), FieldType::F32},
    {"friction", offsetof(Body2D, friction), FieldType::F32},
    {"restitution", offsetof(Body2D, restitution), FieldType::F32},
    {"linearDamping", offsetof(Body2D, linearDamping), FieldType::F32},
    {"angularDamping", offsetof(Body2D, angularDamping), FieldType::F32},
}};

constexpr std::array<ComponentField, 4> kPoseFields = {{
    {"x", offsetof(Pose2D, x), FieldType::F64},
    {"y", offsetof(Pose2D, y), FieldType::F64},
    {"c", offsetof(Pose2D, c), FieldType::F32},
    {"s", offsetof(Pose2D, s), FieldType::F32},
}};

constexpr std::array<ComponentField, 3> kVelocityFields = {{
    {"x", offsetof(Velocity2D, x), FieldType::F32},
    {"y", offsetof(Velocity2D, y), FieldType::F32},
    {"angular", offsetof(Velocity2D, angular), FieldType::F32},
}};

constexpr std::array<ComponentField, 3> kImpulseFields = {{
    {"x", offsetof(Impulse2D, x), FieldType::F32},
    {"y", offsetof(Impulse2D, y), FieldType::F32},
    {"angular", offsetof(Impulse2D, angular), FieldType::F32},
}};

constexpr std::array<ComponentField, 13> kContactFields = {{
    {"touching", offsetof(Contact2D, touching), FieldType::U32},
    {"began", offsetof(Contact2D, began), FieldType::U32},
    {"ended", offsetof(Contact2D, ended), FieldType::U32},
    {"overlapping", offsetof(Contact2D, overlapping), FieldType::U32},
    {"entered", offsetof(Contact2D, entered), FieldType::U32},
    {"exited", offsetof(Contact2D, exited), FieldType::U32},
    {"hit.slot", offsetof(Contact2D, hit) + offsetof(world::EntityHandle, slot), FieldType::U32},
    {"hit.generation", offsetof(Contact2D, hit) + offsetof(world::EntityHandle, generation), FieldType::U32},
    {"hitSpeed", offsetof(Contact2D, hitSpeed), FieldType::F32},
    {"hitNormalX", offsetof(Contact2D, hitNormalX), FieldType::F32},
    {"hitNormalY", offsetof(Contact2D, hitNormalY), FieldType::F32},
    {"visitor.slot", offsetof(Contact2D, visitor) + offsetof(world::EntityHandle, slot), FieldType::U32},
    {"visitor.generation", offsetof(Contact2D, visitor) + offsetof(world::EntityHandle, generation), FieldType::U32},
}};

constexpr std::array<ComponentField, 10> kRayHitFields = {{
    {"hit", offsetof(RayHit2D, hit), FieldType::Bool},
    {"inside", offsetof(RayHit2D, inside), FieldType::Bool},
    {"discontinuous", offsetof(RayHit2D, discontinuous), FieldType::Bool},
    {"entity.slot", offsetof(RayHit2D, entity) + offsetof(world::EntityHandle, slot), FieldType::U32},
    {"entity.generation", offsetof(RayHit2D, entity) + offsetof(world::EntityHandle, generation), FieldType::U32},
    {"x", offsetof(RayHit2D, x), FieldType::F64},
    {"y", offsetof(RayHit2D, y), FieldType::F64},
    {"normalX", offsetof(RayHit2D, normalX), FieldType::F32},
    {"normalY", offsetof(RayHit2D, normalY), FieldType::F32},
    {"fraction", offsetof(RayHit2D, fraction), FieldType::F32},
}};

constexpr std::array<ComponentField, 3> kOverlapFields = {{
    {"count", offsetof(Overlap2D, count), FieldType::U32},
    {"entity.slot", offsetof(Overlap2D, entity) + offsetof(world::EntityHandle, slot), FieldType::U32},
    {"entity.generation", offsetof(Overlap2D, entity) + offsetof(world::EntityHandle, generation), FieldType::U32},
}};

const std::array<ComponentLayout, 2> kAnswerLayouts = {
    ComponentLayout{.id = {},
                    .name = {},
                    .scriptType = "RayHit2D",
                    .size = sizeof(RayHit2D),
                    .alignment = alignof(RayHit2D),
                    .fields = kRayHitFields},
    ComponentLayout{.id = {},
                    .name = {},
                    .scriptType = "Overlap2D",
                    .size = sizeof(Overlap2D),
                    .alignment = alignof(Overlap2D),
                    .fields = kOverlapFields},
};

constexpr std::array<ComponentField, 5> kCharacterFields = {{
    {"groundNormal", offsetof(Character2D, groundNormal), FieldType::F32},
    {"snap", offsetof(Character2D, snap), FieldType::F32},
    {"ground", offsetof(Character2D, ground), FieldType::U8},
    {"groundNormalX", offsetof(Character2D, groundNormalX), FieldType::F32},
    {"groundNormalY", offsetof(Character2D, groundNormalY), FieldType::F32},
}};

template <typename T>
constexpr ComponentLayout layoutOf(std::string_view scriptType, std::span<const ComponentField> fields) noexcept {
    return ComponentLayout{.id = T::kComponentTypeId,
                           .name = T::kComponentName,
                           .scriptType = scriptType,
                           .size = sizeof(T),
                           .alignment = alignof(T),
                           .fields = fields};
}

constexpr std::array<ComponentField, 26> kJointFields = {{
    {"a.slot", offsetof(Joint2D, a) + offsetof(world::EntityHandle, slot), FieldType::U32},
    {"a.generation", offsetof(Joint2D, a) + offsetof(world::EntityHandle, generation), FieldType::U32},
    {"b.slot", offsetof(Joint2D, b) + offsetof(world::EntityHandle, slot), FieldType::U32},
    {"b.generation", offsetof(Joint2D, b) + offsetof(world::EntityHandle, generation), FieldType::U32},
    {"anchorAX", offsetof(Joint2D, anchorAX), FieldType::F32},
    {"anchorAY", offsetof(Joint2D, anchorAY), FieldType::F32},
    {"anchorBX", offsetof(Joint2D, anchorBX), FieldType::F32},
    {"anchorBY", offsetof(Joint2D, anchorBY), FieldType::F32},
    {"axisX", offsetof(Joint2D, axisX), FieldType::F32},
    {"axisY", offsetof(Joint2D, axisY), FieldType::F32},
    {"linearLowerX", offsetof(Joint2D, linearLowerX), FieldType::F32},
    {"linearLowerY", offsetof(Joint2D, linearLowerY), FieldType::F32},
    {"linearUpperX", offsetof(Joint2D, linearUpperX), FieldType::F32},
    {"linearUpperY", offsetof(Joint2D, linearUpperY), FieldType::F32},
    {"angularLower", offsetof(Joint2D, angularLower), FieldType::F32},
    {"angularUpper", offsetof(Joint2D, angularUpper), FieldType::F32},
    {"motorSpeed", offsetof(Joint2D, motorSpeed), FieldType::F32},
    {"motorEffort", offsetof(Joint2D, motorEffort), FieldType::F32},
    {"breakForce", offsetof(Joint2D, breakForce), FieldType::F32},
    {"breakTorque", offsetof(Joint2D, breakTorque), FieldType::F32},
    {"linearX", offsetof(Joint2D, linearX), FieldType::U8},
    {"linearY", offsetof(Joint2D, linearY), FieldType::U8},
    {"angular", offsetof(Joint2D, angular), FieldType::U8},
    {"motor", offsetof(Joint2D, motor), FieldType::U8},
    {"collideConnected", offsetof(Joint2D, collideConnected), FieldType::Bool},
    {"broken", offsetof(Joint2D, broken), FieldType::Bool},
}};

constexpr std::array<ComponentField, 6> kAttachFields = {{
    {"parent.slot", offsetof(Attach2D, parent) + offsetof(world::EntityHandle, slot), FieldType::U32},
    {"parent.generation", offsetof(Attach2D, parent) + offsetof(world::EntityHandle, generation), FieldType::U32},
    {"x", offsetof(Attach2D, x), FieldType::F32},
    {"y", offsetof(Attach2D, y), FieldType::F32},
    {"c", offsetof(Attach2D, c), FieldType::F32},
    {"s", offsetof(Attach2D, s), FieldType::F32},
}};

constexpr std::array<ComponentField, 5> kTargetFields = {{
    {"x", offsetof(Target2D, x), FieldType::F64},
    {"y", offsetof(Target2D, y), FieldType::F64},
    {"c", offsetof(Target2D, c), FieldType::F32},
    {"s", offsetof(Target2D, s), FieldType::F32},
    {"set", offsetof(Target2D, set), FieldType::Bool},
}};

const std::array<ComponentLayout, 9> kLayouts = {
    layoutOf<Body2D>("Body2D", kBodyFields),
    layoutOf<Pose2D>("Pose2D", kPoseFields),
    layoutOf<Velocity2D>("Velocity2D", kVelocityFields),
    layoutOf<Impulse2D>("Impulse2D", kImpulseFields),
    layoutOf<Target2D>("Target2D", kTargetFields),
    layoutOf<Contact2D>("Contact2D", kContactFields),
    layoutOf<Character2D>("Character2D", kCharacterFields),
    layoutOf<Joint2D>("Joint2D", kJointFields),
    layoutOf<Attach2D>("Attach2D", kAttachFields),
};

} // namespace

std::span<const ComponentLayout> componentLayouts() noexcept {
    return kLayouts;
}

std::span<const ComponentLayout> answerLayouts() noexcept {
    return kAnswerLayouts;
}

} // namespace rawframe::physics2d
