#pragma once

// The World's components field by field, by name, from whoever defines them
// (a game, through its program's layouts), so a reader that knows none of
// them, the tooling endpoint (D409), can show a value as its fields rather
// than its bytes. Read on the Host thread between ticks.

#include "rawframe/composition/participant.h"
#include "rawframe/schema/component.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace rawframe::world_runtime {

enum class ComponentFieldKind : std::uint8_t {
    I8,
    I16,
    I32,
    I64,
    U8,
    U16,
    U32,
    U64,
    F32,
    F64,
    /// One byte, nought or one.
    Bool,
    /// A `world::EntityHandle`.
    Entity,
    /// An enum's case number, a U32; its names are the field's cases.
    Case,
};

struct ComponentFieldEntry {
    std::string name;
    std::size_t offset = 0;
    ComponentFieldKind kind = ComponentFieldKind::U8;
    /// A `Case` field's case names, by number.
    std::vector<std::string> cases;
};

struct ComponentFieldSet {
    schema::ComponentTypeId id;
    std::size_t size = 0;
    /// Every field a reader may show, in the type's order; a field of a kind
    /// none of the above is left out.
    std::vector<ComponentFieldEntry> fields;
};

class ComponentFields {
public:
    ComponentFields() = default;
    ComponentFields(const ComponentFields&) = delete;
    ComponentFields& operator=(const ComponentFields&) = delete;
    virtual ~ComponentFields() = default;

    /// Every component the provider knows the fields of; a component of the
    /// World missing here is shown by its name alone.
    [[nodiscard]] virtual std::span<const ComponentFieldSet> fieldSets() const noexcept = 0;
};

inline constexpr composition::Capability<ComponentFields> kComponentFields{"rawframe.world.component_fields"};

} // namespace rawframe::world_runtime
