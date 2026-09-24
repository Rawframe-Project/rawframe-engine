#pragma once

#include "rawframe/composition/registrar.h"

#include <cstdint>

namespace rawframe::world_kest {

/// Contributes `rawframe.world_kest.game`, a World-scoped participant that
/// loads a Kest-scripted game (game.h) into the World runtime's Simulation:
/// it compiles the program, adds the Kest-declared components and the Kest
/// systems while being constructed, spawns the starting entities when it
/// starts, and may reload the program in the Host's `maintenance` phase,
/// between ticks. Configuration keys:
///
///   kest.game             the game description's path; without it nothing loads
///   kest.library          where Kest's `std` lives (Kest's default: lib/ beside the program)
///   kest.heap_bytes       the game machine's heap ceiling (67108864)
///   kest.fuel_per_system  steps one system may take per tick (10000000)
///   kest.reload_every     every this many Host iterations, reload the program
///                         if a `.kest` file beside it changed; 0 never (0)
void registerParticipants(composition::ParticipantRegistrar& registrar) noexcept;

inline constexpr std::uint8_t kScopes = composition::scopeBit(composition::LifetimeScope::World);

} // namespace rawframe::world_kest
