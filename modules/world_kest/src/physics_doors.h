#pragma once

// The doors rawframe.physics2d's and rawframe.physics3d's Kest modules ask
// for, for the server's game and for a predicting client alike.

#include "rawframe/kest/doors.h"
#include "rawframe/physics2d/physics.h"
#include "rawframe/physics3d/physics.h"
#include "rawframe/world_replication/perception.h"

#include <cstdint>
#include <vector>

namespace rawframe::world_kest {

/// What the doors answer from, set by their owner as it changes: the physics
/// asked, of the game's dimensions (the doors refuse while it is null), and
/// on a server, whom each connection was sent, which gates rays cast back to
/// what a player saw.
struct PhysicsDoorContext {
    const physics2d::Physics2DQueries* queries = nullptr;
    const physics3d::Physics3DQueries* queries3d = nullptr;
    const world_replication::InterestHistory* interest = nullptr;
    /// What the last overlap query found.
    std::vector<world::EntityHandle> found;
};

/// Adds `castRay`, `castRayAt`, `castRayAmong`, and `castRayAtAmong` of
/// `Physics2D`, with `castCircle` and `overlapCircle`, or, for 3
/// `dimensions`, those of `Physics3D`, with `castSphere` and
/// `overlapSphere`, answered from `context` as it is when a program calls
/// one. `context` must outlive every machine started with the table.
[[nodiscard]] result::Status
addPhysicsDoors(kest::DoorTable& doors, std::uint8_t dimensions, const PhysicsDoorContext* context);

} // namespace rawframe::world_kest
