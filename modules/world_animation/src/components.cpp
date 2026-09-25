#include "rawframe/world_animation/components.h"

#include <array>
#include <cstddef>

namespace rawframe::world_animation {

namespace {

using schema::ComponentField;
using schema::ComponentLayout;
using schema::FieldType;

constexpr std::array<ComponentField, 4> kAnimatorFields = {{
    {"graph", offsetof(Animator, graph), FieldType::U64},
    {"relevance", offsetof(Animator, relevance), FieldType::U8},
    {"events", offsetof(Animator, events), FieldType::U32},
    {"request", offsetof(Animator, request), FieldType::U64},
}};

constexpr std::array<ComponentField, 14> kRootMotionFields = {{
    {"moveX", offsetof(RootMotion, moveX), FieldType::F64},
    {"moveY", offsetof(RootMotion, moveY), FieldType::F64},
    {"moveZ", offsetof(RootMotion, moveZ), FieldType::F64},
    {"turnX", offsetof(RootMotion, turnX), FieldType::F64},
    {"turnY", offsetof(RootMotion, turnY), FieldType::F64},
    {"turnZ", offsetof(RootMotion, turnZ), FieldType::F64},
    {"turnW", offsetof(RootMotion, turnW), FieldType::F64},
    {"travelX", offsetof(RootMotion, travelX), FieldType::F64},
    {"travelY", offsetof(RootMotion, travelY), FieldType::F64},
    {"travelZ", offsetof(RootMotion, travelZ), FieldType::F64},
    {"facingX", offsetof(RootMotion, facingX), FieldType::F64},
    {"facingY", offsetof(RootMotion, facingY), FieldType::F64},
    {"facingZ", offsetof(RootMotion, facingZ), FieldType::F64},
    {"facingW", offsetof(RootMotion, facingW), FieldType::F64},
}};

const std::array<ComponentLayout, 2> kLayouts = {ComponentLayout{.id = Animator::kComponentTypeId,
                                                                 .name = Animator::kComponentName,
                                                                 .scriptType = "Animator",
                                                                 .size = sizeof(Animator),
                                                                 .alignment = alignof(Animator),
                                                                 .fields = kAnimatorFields},
                                                 ComponentLayout{.id = RootMotion::kComponentTypeId,
                                                                 .name = RootMotion::kComponentName,
                                                                 .scriptType = "RootMotion",
                                                                 .size = sizeof(RootMotion),
                                                                 .alignment = alignof(RootMotion),
                                                                 .fields = kRootMotionFields}};

} // namespace

std::span<const ComponentLayout> componentLayouts() noexcept {
    return kLayouts;
}

} // namespace rawframe::world_animation
