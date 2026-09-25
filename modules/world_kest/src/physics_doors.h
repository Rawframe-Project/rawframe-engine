#pragma once

// The doors rawframe.physics2d's Kest module asks for, for the server's game
// and for a predicting client alike.

#include "rawframe/kest/doors.h"
#include "rawframe/physics2d/physics.h"
#include "rawframe/world_replication/perception.h"

namespace rawframe::world_kest {

/// What the doors answer from, set by their owner as it changes: the physics
/// asked (the doors refuse while it is null), and on a server, whom each
/// connection was sent, which gates rays cast back to what a player saw.
struct PhysicsDoorContext {
    const physics2d::Physics2DQueries* queries = nullptr;
    const world_replication::InterestHistory* interest = nullptr;
};

/// Adds `Physics2D.castRay`, `castRayAt`, `castRayAmong`, and
/// `castRayAtAmong`, answered from `context` as it is when a program calls
/// one. `context` must outlive every machine started with the table.
[[nodiscard]] result::Status addPhysicsDoors(kest::DoorTable& doors, const PhysicsDoorContext* context);

} // namespace rawframe::world_kest
