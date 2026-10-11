#pragma once

// The door rawframe.sound's Kest module asks for: the player's volume for
// a bus role, which a client's present systems set (D625). Every table a
// program importing rawframe.sound may run against offers it, the
// simulations' and the sample's doing nothing.

#include "rawframe/kest/doors.h"
#include "rawframe/world_kest/volumes.h"

namespace rawframe::world_kest {

/// Where the door writes: the presentation's volumes, none for a table
/// that only simulates.
struct SoundDoorContext {
    PresentedVolumes* volumes = nullptr;
};

/// Adds `Sound.volume`, which writes into `context`'s volumes a role's
/// decibels (a role past `audio::Role`'s and a volume not finite change
/// nothing), or does nothing for a null `context` or volumes: no
/// simulation has a sound. It changes only the player's own hearing, so
/// untrusted programs may call it. `context` outlives every machine
/// started with the table.
[[nodiscard]] result::Status addSoundDoors(kest::DoorTable& doors, const SoundDoorContext* context);

} // namespace rawframe::world_kest
