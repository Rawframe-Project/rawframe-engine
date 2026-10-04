#pragma once

// The door rawframe.replication's Kest module asks for: which player the
// machine running a system shows (D390).

#include "rawframe/kest/doors.h"
#include "rawframe/world/entity.h"

namespace rawframe::world_kest {

/// What the door answers, set by its owner as it changes: a client's own
/// player in the mirror its present systems run on; the null entity in
/// every simulation, on a server (which plays for everyone) and in a
/// client's prediction alike, so no simulation depends on which machine
/// runs it.
struct PlayerDoorContext {
    world::EntityHandle player;
};

/// Adds `Replication.player`, answered from `context` as it is when a
/// program calls one, or always the null entity for a null `context` (a
/// simulation's table). It only reads, so untrusted programs may call it.
/// `context` must outlive every machine started with the table.
[[nodiscard]] result::Status addPlayerDoor(kest::DoorTable& doors, const PlayerDoorContext* context);

} // namespace rawframe::world_kest
