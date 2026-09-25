#include "rawframe/physics2d/components.h"

#include <array>

namespace rawframe::physics2d {

namespace {

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

const ComponentLayout kRayHitLayout{.id = {},
                                    .name = {},
                                    .scriptType = "RayHit2D",
                                    .size = sizeof(RayHit2D),
                                    .alignment = alignof(RayHit2D),
                                    .fields = kRayHitFields};

constexpr std::array<ComponentField, 7> kCharacterFields = {{
    {"groundNormal", offsetof(Character2D, groundNormal), FieldType::F32},
    {"snap", offsetof(Character2D, snap), FieldType::F32},
    {"ground", offsetof(Character2D, ground), FieldType::U8},
    {"groundEntity.slot", offsetof(Character2D, groundEntity) + offsetof(world::EntityHandle, slot), FieldType::U32},
    {"groundEntity.generation",
     offsetof(Character2D, groundEntity) + offsetof(world::EntityHandle, generation),
     FieldType::U32},
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

const std::array<ComponentLayout, 6> kLayouts = {
    layoutOf<Body2D>("Body2D", kBodyFields),
    layoutOf<Pose2D>("Pose2D", kPoseFields),
    layoutOf<Velocity2D>("Velocity2D", kVelocityFields),
    layoutOf<Impulse2D>("Impulse2D", kImpulseFields),
    layoutOf<Contact2D>("Contact2D", kContactFields),
    layoutOf<Character2D>("Character2D", kCharacterFields),
};

} // namespace

std::span<const ComponentLayout> componentLayouts() noexcept {
    return kLayouts;
}

const ComponentLayout& rayHitLayout() noexcept {
    return kRayHitLayout;
}

} // namespace rawframe::physics2d
