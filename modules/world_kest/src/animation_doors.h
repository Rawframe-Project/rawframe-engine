#pragma once

// The doors rawframe.animation's Kest module asks for: a bone of an
// entity's last pose, the events of its last step, and its state
// machines' states.

#include "rawframe/kest/doors.h"
#include "rawframe/world_animation/animation.h"

namespace rawframe::world_kest {

/// What the doors answer from, set by their owner as it changes: the
/// World's animation while it lives (the doors refuse while it is null).
struct AnimationDoorContext {
    const world_animation::AnimationQueries* queries = nullptr;
};

/// Adds `Animation.bone`, `Animation.event`, and `Animation.state`,
/// answered from `context` as it is when a program calls one. Each only
/// reads what the last step left, within bounds, so untrusted programs may
/// call them. `context` must
/// outlive every machine started with the table.
[[nodiscard]] result::Status addAnimationDoors(kest::DoorTable& doors, const AnimationDoorContext* context);

} // namespace rawframe::world_kest
