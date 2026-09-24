#pragma once

#include "rawframe/result/result.h"
#include "rawframe/schema/component.h"
#include "rawframe/schema/stable_id.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

namespace rawframe::schema {

/// The dense runtime index of a component type in one frozen registry. Assigned
/// in ascending stable-ID order, so identical inputs give identical indices.
/// Never durable: it means nothing outside its registry.
struct ComponentRuntimeId {
    std::uint32_t value = 0;
    friend constexpr bool operator==(const ComponentRuntimeId&, const ComponentRuntimeId&) noexcept = default;
    friend constexpr auto operator<=>(const ComponentRuntimeId&, const ComponentRuntimeId&) noexcept = default;
};

/// A component type resolved against a registry at setup, carrying its C++ type
/// so hot access needs no lookup and cannot confuse types.
template <Component T> struct ComponentKey {
    ComponentRuntimeId id;
};

/// An immutable, validated set of component types. Built once by
/// RegistryBuilder, then shared by reference: every World binds one explicitly,
/// and there is no global registry (ADR-0011).
class SchemaRegistry {
public:
    [[nodiscard]] std::span<const ComponentDescriptor> components() const noexcept {
        return components_;
    }
    [[nodiscard]] const ComponentDescriptor& descriptor(ComponentRuntimeId id) const noexcept {
        return components_[id.value];
    }

    /// The runtime index for a stable ID, or `not_found`.
    [[nodiscard]] result::Result<ComponentRuntimeId> find(ComponentTypeId id) const;

    /// The typed key for T, or `not_found` if T is not in this registry.
    template <Component T> [[nodiscard]] result::Result<ComponentKey<T>> key() const {
        RAWFRAME_TRY_ASSIGN(const ComponentRuntimeId kId, find(T::kComponentTypeId));
        return ComponentKey<T>{kId};
    }

    /// Whether `key` names T in this registry. Keys from another registry, or
    /// forged ones, fail this.
    template <Component T> [[nodiscard]] bool owns(ComponentKey<T> key) const noexcept {
        return key.id.value < components_.size() && components_[key.id.value].id == T::kComponentTypeId;
    }

private:
    friend class RegistryBuilder;

    std::vector<ComponentDescriptor> components_; // sorted by stable ID
};

/// Collects component types from an explicit list and validates them. No
/// discovery and no static registration: what is added is what exists.
class RegistryBuilder {
public:
    template <Component T> RegistryBuilder& add() {
        descriptors_.push_back(describeComponent<T>());
        return *this;
    }

    RegistryBuilder& add(const ComponentDescriptor& descriptor) {
        descriptors_.push_back(descriptor);
        return *this;
    }

    /// Validates and freezes: rejects a zero stable ID, an empty or overlong
    /// name, and duplicate stable IDs or names, all as `invalid_argument` or
    /// `already_exists`.
    [[nodiscard]] result::Result<std::shared_ptr<const SchemaRegistry>> freeze() const;

private:
    std::vector<ComponentDescriptor> descriptors_;
};

} // namespace rawframe::schema
