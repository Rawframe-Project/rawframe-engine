#pragma once

// The doors rawframe.physics2d's Kest module asks for, for the server's game
// and for a predicting client alike.

#include "rawframe/kest/doors.h"
#include "rawframe/physics2d/physics.h"

namespace rawframe::world_kest {

/// Adds `Physics2D.castRay`, `castRayAt`, `castRayAmong`, and
/// `castRayAtAmong`, answered by whatever
/// `*queries` points at when a program calls one, and refused while that is
/// null. `queries` must outlive every machine started with the table.
[[nodiscard]] result::Status addPhysicsDoors(kest::DoorTable& doors, const physics2d::Physics2DQueries* const* queries);

} // namespace rawframe::world_kest
