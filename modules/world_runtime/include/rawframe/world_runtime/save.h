#pragma once

#include "rawframe/composition/participant.h"
#include "rawframe/result/result.h"
#include "rawframe/world_save/save.h"

namespace rawframe::world_runtime {

/// What a World's save keeps (ADR-0057), from whoever defines the World's
/// components: a game.
class SavePlan {
public:
    SavePlan() = default;
    SavePlan(const SavePlan&) = delete;
    SavePlan& operator=(const SavePlan&) = delete;
    virtual ~SavePlan() = default;

    /// The World's save document, or why the game declares none.
    [[nodiscard]] virtual result::Result<const world_save::SaveDeclaration*> saveDeclaration() const = 0;
    /// The document kept for each player, of the player's entity; null for
    /// a game that keeps none.
    [[nodiscard]] virtual const world_save::SaveDeclaration* playerSaveDeclaration() const noexcept = 0;
};

inline constexpr composition::Capability<SavePlan> kSavePlan{"rawframe.world.save_plan"};

} // namespace rawframe::world_runtime
