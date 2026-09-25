#pragma once

// Document deltas (ADR-0065, SPEC-0040): the one staged-change record of
// authoring. A delta names one slot of a scene document (rawframe/scene)
// and the slot's value before and after, both complete and explicit, so a
// delta applies forward or backward with nothing but itself, and a journal
// of them is persistable as it stands. The kinds are closed:
//
//   create_node       an entity, from none to its record (place, name,
//                     components with their layout marks)
//   destroy_node      an entity, from its record to none
//   reorder           an entity's place among the scene's entities
//   set_name          an entity's name for people (node metadata, D149)
//   add_component     a component of an entity, from none to its mark and
//                     fields
//   remove_component  the same, from its mark and fields to none
//   set_field         a number or truth field, from a value or its
//                     default to another
//   set_reference     an entity-valued field, likewise
//
// Applying a delta forward requires the slot to hold its `before` and
// leaves it holding its `after`; backward, the other way about. A slot
// holding anything else refuses (`DeltaMismatch`), so a delta never lands
// on a document it was not made against. The scene's schema follows its
// components: a component's mark is written when its first use arrives and
// dropped with its last. Whether the whole scene is still in form is the
// transaction's to check, once, after its journal.
//
// SPEC-0040's `reparent` waits for a document hierarchy: children are
// attachments, components like any other (D120). Instances and their
// patches are not yet authored by delta.

#include "rawframe/base/bits128.h"
#include "rawframe/result/result.h"
#include "rawframe/scene/scene.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace rawframe::authoring {

enum class DeltaKind : std::uint8_t {
    CreateNode,
    DestroyNode,
    Reorder,
    SetName,
    AddComponent,
    RemoveComponent,
    SetField,
    SetReference,
};

/// A component as a delta carries it: its layout mark and its fields.
struct ComponentRecord {
    std::string name;
    std::uint64_t mark = 0;
    std::vector<scene::SceneField> fields;

    friend bool operator==(const ComponentRecord&, const ComponentRecord&) = default;
};

/// An entity as a delta carries it: where it stands, its name, and its
/// components in name order.
struct NodeRecord {
    std::size_t place = 0;
    std::string name;
    std::vector<ComponentRecord> components;

    friend bool operator==(const NodeRecord&, const NodeRecord&) = default;
};

/// One slot's value. Which member holds it follows the kind: `node` for
/// create_node and destroy_node, `place` for reorder, `name` for set_name,
/// `component` for add_component and remove_component, `field` for
/// set_field and set_reference. None of them is the slot empty: no entity,
/// no component, or a field at its default.
struct SlotValue {
    std::optional<NodeRecord> node;
    std::optional<std::size_t> place;
    std::optional<std::string> name;
    std::optional<ComponentRecord> component;
    std::optional<scene::FieldValue> field;

    friend bool operator==(const SlotValue&, const SlotValue&) = default;
};

struct Delta {
    DeltaKind kind = DeltaKind::CreateNode;
    /// The entity, by its SourceEntityId.
    base::Bits128 entity{};
    /// For component and field kinds.
    std::string component;
    /// For field kinds.
    std::string field;
    SlotValue before;
    SlotValue after;

    friend bool operator==(const Delta&, const Delta&) = default;
};

using Journal = std::vector<Delta>;

/// Applies `delta` to `scene` forward, or backward when `forward` is
/// false. Refuses (`DeltaMismatch`) when the slot does not hold what the
/// delta leaves from, and (`DeltaInvalid`) a delta out of its kind's shape;
/// `scene` is unchanged on either.
[[nodiscard]] result::Status apply(scene::Scene& scene, const Delta& delta, bool forward);

/// A journal forward in order, or backward in reverse order; all or none.
[[nodiscard]] result::Status apply(scene::Scene& scene, const Journal& journal, bool forward);

/// SPEC-0028's canonical record of a journal, and read back; refused
/// (`DeltaInvalid`) in any other form.
[[nodiscard]] result::Result<std::string> writeJournal(const Journal& journal);
[[nodiscard]] result::Result<Journal> readJournal(std::string_view bytes);

} // namespace rawframe::authoring
