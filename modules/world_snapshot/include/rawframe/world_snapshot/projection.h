#pragma once

// What a checkpoint holds of a World: the components that persist and, for
// each, its fields as the checkpoint writes them (SPEC-0011 projection). A
// value is written field by field, never as its bytes in memory, so padding
// and layout never reach an artifact, and a field holding an entity is
// written as the entity's place in the artifact, never as a handle.

#include "rawframe/base/sha256.h"
#include "rawframe/schema/component.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace rawframe::world_snapshot {

enum class FieldKind : std::uint8_t {
    I8 = 1,
    I16 = 2,
    I32 = 3,
    I64 = 4,
    U8 = 5,
    U16 = 6,
    U32 = 7,
    U64 = 8,
    F32 = 9,
    F64 = 10,
    /// One byte, nought or one.
    Bool = 11,
    /// A `world::EntityHandle` in memory; its `SnapshotEntityId` in the
    /// artifact, nought for the null handle.
    Entity = 12,
};

/// Bytes a field takes in memory, which is also what it takes written.
[[nodiscard]] std::size_t widthOf(FieldKind kind) noexcept;

struct SnapshotField {
    std::size_t offset = 0;
    FieldKind kind = FieldKind::U8;
};

struct SnapshotComponent {
    schema::ComponentTypeId id;
    std::size_t size = 0;
    /// In the order they are written.
    std::vector<SnapshotField> fields;
};

struct SnapshotProjection {
    std::vector<SnapshotComponent> components;
};

/// SHA-256 over the projection in stable component order: every component's
/// identity and size, and every field's offset and kind in order. Two
/// projections that write different bytes never share one.
[[nodiscard]] base::Sha256Digest projectionFingerprint(const SnapshotProjection& projection);

/// Whether every field lies inside its component and no two overlap.
[[nodiscard]] bool projectionValid(const SnapshotProjection& projection) noexcept;

} // namespace rawframe::world_snapshot
