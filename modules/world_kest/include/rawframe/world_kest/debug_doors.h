#pragma once

// The doors rawframe.debug's Kest module asks for: lines, boxes, spheres
// and arrows a client's present systems draw over the World, and their
// channels cleared. Every table a program importing rawframe.debug may run
// against offers them; the simulations', the sample's and a shipping
// build's draw nothing.

#include "rawframe/kest/doors.h"
#include "rawframe/world/time.h"
#include "rawframe/world_kest/drawings.h"

namespace rawframe::world_kest {

/// Where the doors draw: the presentation's drawings, none for a table
/// that only simulates; and the World's tick rate, which a shape's seconds
/// are counted in.
struct DebugDoorContext {
    PresentedDrawings* drawings = nullptr;
    world::TickRate rate;
};

/// Adds `Debug.line`, `Debug.box`, `Debug.sphere`, `Debug.arrow` and
/// `Debug.clear`, which draw into `context`'s drawings, or do nothing for a
/// null `context` or drawings, and in a shipping build. They write outside
/// the World, so untrusted programs may not call them. `context` outlives
/// every machine started with the table.
[[nodiscard]] result::Status addDebugDoors(kest::DoorTable& doors, const DebugDoorContext* context);

} // namespace rawframe::world_kest
