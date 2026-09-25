#include "rawframe/physics3d/components.h"

#include <array>

namespace rawframe::physics3d {

namespace {

using physics::ComponentField;
using physics::ComponentLayout;
using physics::FieldType;

constexpr std::array<ComponentField, 14> kBodyFields = {{
    {"motion", offsetof(Body3D, motion), FieldType::U8},
    {"shape", offsetof(Body3D, shape), FieldType::U8},
    {"fixedRotation", offsetof(Body3D, fixedRotation), FieldType::Bool},
    {"bullet", offsetof(Body3D, bullet), FieldType::Bool},
    {"sensor", offsetof(Body3D, sensor), FieldType::Bool},
    {"collisionClass", offsetof(Body3D, collisionClass), FieldType::U64},
    {"width", offsetof(Body3D, width), FieldType::F32},
    {"height", offsetof(Body3D, height), FieldType::F32},
    {"depth", offsetof(Body3D, depth), FieldType::F32},
    {"density", offsetof(Body3D, density), FieldType::F32},
    {"friction", offsetof(Body3D, friction), FieldType::F32},
    {"restitution", offsetof(Body3D, restitution), FieldType::F32},
    {"linearDamping", offsetof(Body3D, linearDamping), FieldType::F32},
    {"angularDamping", offsetof(Body3D, angularDamping), FieldType::F32},
}};

constexpr std::array<ComponentField, 7> kPoseFields = {{
    {"x", offsetof(Pose3D, x), FieldType::F64},
    {"y", offsetof(Pose3D, y), FieldType::F64},
    {"z", offsetof(Pose3D, z), FieldType::F64},
    {"qx", offsetof(Pose3D, qx), FieldType::F32},
    {"qy", offsetof(Pose3D, qy), FieldType::F32},
    {"qz", offsetof(Pose3D, qz), FieldType::F32},
    {"qw", offsetof(Pose3D, qw), FieldType::F32},
}};

template <typename T>
constexpr std::array<ComponentField, 6> kMotionFields = {{
    {"x", offsetof(T, x), FieldType::F32},
    {"y", offsetof(T, y), FieldType::F32},
    {"z", offsetof(T, z), FieldType::F32},
    {"angularX", offsetof(T, angularX), FieldType::F32},
    {"angularY", offsetof(T, angularY), FieldType::F32},
    {"angularZ", offsetof(T, angularZ), FieldType::F32},
}};

constexpr std::array<ComponentField, 14> kContactFields = {{
    {"touching", offsetof(Contact3D, touching), FieldType::U32},
    {"began", offsetof(Contact3D, began), FieldType::U32},
    {"ended", offsetof(Contact3D, ended), FieldType::U32},
    {"overlapping", offsetof(Contact3D, overlapping), FieldType::U32},
    {"entered", offsetof(Contact3D, entered), FieldType::U32},
    {"exited", offsetof(Contact3D, exited), FieldType::U32},
    {"hit.slot", offsetof(Contact3D, hit) + offsetof(world::EntityHandle, slot), FieldType::U32},
    {"hit.generation", offsetof(Contact3D, hit) + offsetof(world::EntityHandle, generation), FieldType::U32},
    {"hitSpeed", offsetof(Contact3D, hitSpeed), FieldType::F32},
    {"hitNormalX", offsetof(Contact3D, hitNormalX), FieldType::F32},
    {"hitNormalY", offsetof(Contact3D, hitNormalY), FieldType::F32},
    {"hitNormalZ", offsetof(Contact3D, hitNormalZ), FieldType::F32},
    {"visitor.slot", offsetof(Contact3D, visitor) + offsetof(world::EntityHandle, slot), FieldType::U32},
    {"visitor.generation", offsetof(Contact3D, visitor) + offsetof(world::EntityHandle, generation), FieldType::U32},
}};

constexpr std::array<ComponentField, 11> kRayHitFields = {{
    {"hit", offsetof(RayHit3D, hit), FieldType::Bool},
    {"discontinuous", offsetof(RayHit3D, discontinuous), FieldType::Bool},
    {"entity.slot", offsetof(RayHit3D, entity) + offsetof(world::EntityHandle, slot), FieldType::U32},
    {"entity.generation", offsetof(RayHit3D, entity) + offsetof(world::EntityHandle, generation), FieldType::U32},
    {"x", offsetof(RayHit3D, x), FieldType::F64},
    {"y", offsetof(RayHit3D, y), FieldType::F64},
    {"z", offsetof(RayHit3D, z), FieldType::F64},
    {"normalX", offsetof(RayHit3D, normalX), FieldType::F32},
    {"normalY", offsetof(RayHit3D, normalY), FieldType::F32},
    {"normalZ", offsetof(RayHit3D, normalZ), FieldType::F32},
    {"fraction", offsetof(RayHit3D, fraction), FieldType::F32},
}};

const ComponentLayout kRayHitLayout{.id = {},
                                    .name = {},
                                    .scriptType = "RayHit3D",
                                    .size = sizeof(RayHit3D),
                                    .alignment = alignof(RayHit3D),
                                    .fields = kRayHitFields};

template <typename T>
constexpr ComponentLayout layoutOf(std::string_view scriptType, std::span<const ComponentField> fields) noexcept {
    return ComponentLayout{.id = T::kComponentTypeId,
                           .name = T::kComponentName,
                           .scriptType = scriptType,
                           .size = sizeof(T),
                           .alignment = alignof(T),
                           .fields = fields};
}

const std::array<ComponentLayout, 5> kLayouts = {
    layoutOf<Body3D>("Body3D", kBodyFields),
    layoutOf<Pose3D>("Pose3D", kPoseFields),
    layoutOf<Velocity3D>("Velocity3D", kMotionFields<Velocity3D>),
    layoutOf<Impulse3D>("Impulse3D", kMotionFields<Impulse3D>),
    layoutOf<Contact3D>("Contact3D", kContactFields),
};

} // namespace

std::span<const ComponentLayout> componentLayouts() noexcept {
    return kLayouts;
}

const ComponentLayout& rayHitLayout() noexcept {
    return kRayHitLayout;
}

} // namespace rawframe::physics3d
