#include "rawframe/physics2d/components.h"

#include <array>

namespace rawframe::physics2d {

namespace {

constexpr std::array<ComponentField, 11> kBodyFields = {{
    {"motion", offsetof(Body2D, motion), FieldType::U8},
    {"shape", offsetof(Body2D, shape), FieldType::U8},
    {"fixedRotation", offsetof(Body2D, fixedRotation), FieldType::Bool},
    {"bullet", offsetof(Body2D, bullet), FieldType::Bool},
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

template <typename T>
constexpr ComponentLayout layoutOf(std::string_view scriptType, std::span<const ComponentField> fields) noexcept {
    return ComponentLayout{.id = T::kComponentTypeId,
                           .name = T::kComponentName,
                           .scriptType = scriptType,
                           .size = sizeof(T),
                           .alignment = alignof(T),
                           .fields = fields};
}

const std::array<ComponentLayout, 4> kLayouts = {
    layoutOf<Body2D>("Body2D", kBodyFields),
    layoutOf<Pose2D>("Pose2D", kPoseFields),
    layoutOf<Velocity2D>("Velocity2D", kVelocityFields),
    layoutOf<Impulse2D>("Impulse2D", kImpulseFields),
};

} // namespace

std::span<const ComponentLayout> componentLayouts() noexcept {
    return kLayouts;
}

} // namespace rawframe::physics2d
