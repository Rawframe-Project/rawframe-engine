#include "rawframe/world_animation/components.h"

#include <array>
#include <cstddef>

namespace rawframe::world_animation {

namespace {

using schema::ComponentField;
using schema::ComponentLayout;
using schema::FieldType;

constexpr std::array<ComponentField, 3> kAnimatorFields = {{
    {"graph", offsetof(Animator, graph), FieldType::U64},
    {"relevance", offsetof(Animator, relevance), FieldType::U8},
    {"events", offsetof(Animator, events), FieldType::U32},
}};

const std::array<ComponentLayout, 1> kLayouts = {ComponentLayout{.id = Animator::kComponentTypeId,
                                                                 .name = Animator::kComponentName,
                                                                 .scriptType = "Animator",
                                                                 .size = sizeof(Animator),
                                                                 .alignment = alignof(Animator),
                                                                 .fields = kAnimatorFields}};

} // namespace

std::span<const ComponentLayout> componentLayouts() noexcept {
    return kLayouts;
}

} // namespace rawframe::world_animation
