#pragma once

#include "rawframe/schema/stable_id.h"

#include <concepts>
#include <cstddef>
#include <new>
#include <string_view>
#include <type_traits>
#include <utility>

namespace rawframe::schema {

/// The longest canonical component name.
inline constexpr std::size_t kMaximumComponentNameBytes = 128;

/// A component is typed data: no virtual functions, nothing that can fail to
/// move or destroy, and a committed stable identity and canonical name.
/// Behaviour belongs to systems (ADR-0011). An empty type is a tag.
template <typename T>
concept Component = std::is_object_v<T> && !std::is_polymorphic_v<T> && std::is_nothrow_move_constructible_v<T> &&
                    std::is_nothrow_destructible_v<T> && requires {
                        { T::kComponentTypeId } -> std::convertible_to<ComponentTypeId>;
                        { T::kComponentName } -> std::convertible_to<std::string_view>;
                    };

/// How storage moves and destroys values of a component it knows only by
/// descriptor.
struct ComponentOperations {
    void (*moveConstruct)(void* destination, void* source) noexcept = nullptr;
    void (*destroy)(void* value) noexcept = nullptr;
};

/// What the registry knows about one component type.
struct ComponentDescriptor {
    ComponentTypeId id;
    std::string_view name;
    std::size_t size = 0; // zero for a tag
    std::size_t alignment = 1;
    ComponentOperations operations;
};

template <Component T> [[nodiscard]] constexpr ComponentDescriptor describeComponent() noexcept {
    constexpr bool kTag = std::is_empty_v<T>;
    return ComponentDescriptor{
        .id = T::kComponentTypeId,
        .name = T::kComponentName,
        .size = kTag ? 0 : sizeof(T),
        .alignment = alignof(T),
        .operations = ComponentOperations{
            .moveConstruct =
                [](void* destination, void* source) noexcept {
                    ::new (destination) T(std::move(*static_cast<T*>(source)));
                },
            .destroy =
                [](void* value) noexcept {
                    static_cast<T*>(value)->~T();
                },
        },
    };
}

} // namespace rawframe::schema
