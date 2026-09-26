#pragma once

// The handlers of a game's taken mods (SPEC-0042 event points, D181).

#include "rawframe/kest/machine.h"
#include "rawframe/kest/program.h"
#include "rawframe/result/result.h"
#include "rawframe/world_kest/game.h"
#include "rawframe/world_kest/game_files.h"
#include "rawframe/world_kest/kest_systems.h"

#include <memory>
#include <span>
#include <vector>

namespace rawframe::world_kest {

/// Each taken mod's program on a machine of its own, under Kest's untrusted
/// profile with `limits`, offered the standard math alone; each handler a
/// system of it that runs right after the point's game system, over the
/// entities holding the event's component, which it reads, and the
/// components the point lets it write, in that order; each provider a
/// function of it taking `values: [T]` (D199). Refused (`ModRefused`, naming
/// the mod) for a program that does not compile, a handler that is not a
/// function of it taking those columns, a provider not shaped so, or a Kest
/// type of a component shaped otherwise than the game's own (`layouts`, in
/// the order of `game.components`). Their runs are timed in `timing`, if
/// any, with the game's own (D210).
[[nodiscard]] result::Result<std::vector<std::unique_ptr<KestSystems>>>
modHandlers(const GameDescription& game,
            std::span<const kest::TypeLayout> layouts,
            const GameFiles& files,
            const kest::MachineLimits& limits,
            KestTiming* timing = nullptr);

} // namespace rawframe::world_kest
