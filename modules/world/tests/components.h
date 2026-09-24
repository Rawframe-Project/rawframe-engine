#pragma once

// Components the World tests share, and a registry holding them.

#include "rawframe/schema/registry.h"
#include "rawframe/world/world.h"

#include <cstdint>
#include <memory>

namespace rawframe::world::testing {

using schema::ComponentKey;
using schema::ComponentTypeId;

struct Position {
    static constexpr ComponentTypeId kComponentTypeId =
        ComponentTypeId::fromText("c5628e1e-1851-428e-bd57-409bf5b6d486");
    static constexpr std::string_view kComponentName = "test.position";
    std::int32_t x = 0;
    std::int32_t y = 0;
};

struct Velocity {
    static constexpr ComponentTypeId kComponentTypeId =
        ComponentTypeId::fromText("39441e5d-080b-4cfc-8253-eb20add797fd");
    static constexpr std::string_view kComponentName = "test.velocity";
    std::int32_t dx = 0;
    std::int32_t dy = 0;
};

/// Counts live instances, to prove storage constructs and destroys each value
/// exactly once through every move.
struct Tracked {
    static constexpr ComponentTypeId kComponentTypeId =
        ComponentTypeId::fromText("bffde3bb-d4ea-4756-99e3-abd2b3b1059c");
    static constexpr std::string_view kComponentName = "test.tracked";

    static inline std::int64_t live = 0;

    std::int64_t value = 0;
    explicit Tracked(std::int64_t initial = 0) noexcept : value(initial) {
        ++live;
    }
    Tracked(Tracked&& other) noexcept : value(other.value) {
        ++live;
    }
    Tracked(const Tracked&) = delete;
    Tracked& operator=(const Tracked&) = delete;
    Tracked& operator=(Tracked&&) = delete;
    ~Tracked() {
        --live;
    }
};

struct Frozen {
    static constexpr ComponentTypeId kComponentTypeId =
        ComponentTypeId::fromText("3071bfd7-5cfd-4e3b-9c81-0e8f8d179ba3");
    static constexpr std::string_view kComponentName = "test.frozen";
};

struct Keys {
    ComponentKey<Position> position;
    ComponentKey<Velocity> velocity;
    ComponentKey<Tracked> tracked;
    ComponentKey<Frozen> frozen;
};

inline std::shared_ptr<const schema::SchemaRegistry> makeRegistry() {
    schema::RegistryBuilder builder;
    builder.add<Position>().add<Velocity>().add<Tracked>().add<Frozen>();
    auto registry = builder.freeze();
    return registry.has_value() ? *registry : nullptr;
}

inline Keys keysOf(const schema::SchemaRegistry& registry) {
    return Keys{.position = *registry.key<Position>(),
                .velocity = *registry.key<Velocity>(),
                .tracked = *registry.key<Tracked>(),
                .frozen = *registry.key<Frozen>()};
}

} // namespace rawframe::world::testing
