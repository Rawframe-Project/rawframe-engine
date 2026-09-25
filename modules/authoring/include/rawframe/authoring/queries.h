#pragma once

// Read operations (SPEC-0040): what a scene holds, answered by identity and
// typed through the component catalog, so a tool or an agent computes its
// next request from the answer without parsing the document. A query
// stages nothing, enters no history, and names no generation: its answer
// is of the scene as it stands.
//
// What the catalog does not know is still answered, marked so, never
// dropped: a component with no type id, a field with no kind. Only an
// entity the scene does not hold is an error (`TargetNotFound`).

#include "rawframe/authoring/operations.h"
#include "rawframe/base/bits128.h"
#include "rawframe/result/result.h"
#include "rawframe/scene/scene.h"

#include <cstddef>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace rawframe::authoring {

/// An entity an instance brings: which instance, by its place among the
/// scene's, the source scene's resource identity, and the entity's id there.
struct Brought {
    std::size_t instance = 0;
    base::Bits128 scene{};
    base::Bits128 source{};

    friend bool operator==(const Brought&, const Brought&) = default;
};

struct EntityEntry {
    base::Bits128 id{};
    /// The scene's own entities' names; an instance's entities are named by
    /// their source.
    std::string name;
    /// The scene's own entities' places among them.
    std::optional<std::size_t> place;
    std::optional<Brought> brought;
    /// An instance's entity its patch removes.
    bool removed = false;

    friend bool operator==(const EntityEntry&, const EntityEntry&) = default;
};

/// The scene's own entities in their order, then each instance's in the
/// order of its mapping.
struct EntityList {
    std::vector<EntityEntry> entities;
};

struct FieldReading {
    std::string name;
    /// None when the catalog's layout has no such field.
    std::optional<FieldKind> kind;
    scene::FieldValue value;
};

struct ComponentReading {
    /// None when the catalog does not know the component.
    std::optional<schema::ComponentTypeId> component;
    std::string name;
    /// For an instance's entity, what its patch does to the component;
    /// none for the scene's own.
    std::optional<scene::Override::Kind> patch;
    /// The values the scene gives, in name order: a field at its default
    /// is not given, except in a `set` entry.
    std::vector<FieldReading> fields;
};

/// One entity whole: the scene's own components, or what an instance's
/// patch does to the entity it brings (its source's own components are
/// the source scene's to answer).
struct EntityReading {
    EntityEntry entity;
    std::vector<ComponentReading> components;
};

using Answer = std::variant<EntityList, EntityReading>;

[[nodiscard]] result::Result<Answer>
answer(const scene::Scene& scene, const Query& query, const ComponentCatalog& catalog);

} // namespace rawframe::authoring
