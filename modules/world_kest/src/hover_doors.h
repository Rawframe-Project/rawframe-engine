#pragma once

// The door rawframe.ui's Kest module asks for: what the client's UI shows
// under the mouse, for a present system to light (D422).

#include "rawframe/kest/doors.h"

#include <cstdint>

namespace rawframe::world_kest {

/// What the door answers, set by its owner each frame: the press code of
/// the UI node under the process's own player's mouse, nought for none. In
/// every simulation, and for any other player's presentation, it stays
/// nought, so no simulation depends on a mouse.
struct HoverDoorContext {
    std::int64_t hovered = 0;
};

/// Adds `UI.hovered`, answered from `context` as it is when a program calls
/// it, or always nought for a null `context` (a simulation's table). It only
/// reads, so untrusted programs may call it. `context` must outlive every
/// machine started with the table.
[[nodiscard]] result::Status addHoverDoor(kest::DoorTable& doors, const HoverDoorContext* context);

} // namespace rawframe::world_kest
