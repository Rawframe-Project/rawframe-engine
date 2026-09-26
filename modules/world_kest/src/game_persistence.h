#pragma once

// What a game keeps: its save documents (ADR-0057, D105) and its checkpoint
// projection (SPEC-0011, D29), from its description and its components' Kest
// layouts.

#include "rawframe/kest/program.h"
#include "rawframe/result/result.h"
#include "rawframe/world_kest/game.h"
#include "rawframe/world_save/save.h"
#include "rawframe/world_snapshot/projection.h"

#include <optional>
#include <span>

namespace rawframe::world_kest {

struct GamePersistence {
    /// The `save` and `save player` lines as documents: each component with
    /// the program's mark for its layout and its fields as a save names
    /// them, which a later layout is migrated by.
    world_save::SaveDeclaration save;
    world_save::SaveDeclaration playerSave;
    /// What a checkpoint holds: every component, field by field from its
    /// Kest layout, with the `entity` lines' fields as references.
    world_snapshot::SnapshotProjection projection;
    /// A field no checkpoint can write (text, a tagged union). The game
    /// loads and runs; only checkpoints of it are refused, with the reason.
    std::optional<GameEntityField> unwritable;
};

/// `layouts` in the order of `game.components`. Refused (`UnknownName`) for
/// an `entity` line naming a field that is not a rawframe.world Entity.
[[nodiscard]] result::Result<GamePersistence> planPersistence(const GameDescription& game,
                                                              std::span<const kest::TypeLayout> layouts);

} // namespace rawframe::world_kest
