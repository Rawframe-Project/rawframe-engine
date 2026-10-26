#pragma once

// The doors rawframe.ui's Kest module asks for: what the client's UI shows
// under the mouse, for a present system to light, and the value a
// range holds.

#include "rawframe/kest/doors.h"
#include "rawframe/world_kest/ui_queries.h"

#include <cstddef>
#include <cstdint>

namespace rawframe::world_kest {

/// What the doors answer, set by their owner: the press code of the UI node
/// under the process's own player's mouse, nought for none, set each frame;
/// and the UI that holds the ranges of local player `player`'s view, none
/// for nought. In every simulation, and for any other player's hover, they
/// answer nought, so no simulation depends on a mouse.
struct UiDoorContext {
    std::int64_t hovered = 0;
    const UiQueries* queries = nullptr;
    std::size_t player = 0;
};

/// Adds `UI.hovered` and `UI.value`, answered from `context` as it is when
/// a program calls them, or always nought for a null `context` (a
/// simulation's table). They only read, so untrusted programs may call
/// them. `context` must outlive every machine started with the table.
[[nodiscard]] result::Status addUiDoors(kest::DoorTable& doors, const UiDoorContext* context);

} // namespace rawframe::world_kest
