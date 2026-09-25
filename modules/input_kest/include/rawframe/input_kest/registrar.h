#pragma once

#include "rawframe/composition/registrar.h"

#include <cstdint>

namespace rawframe::input_kest {

/// Contributes `rawframe.input_kest.sources`, a World-scoped participant that
/// provides `rawframe.replication.input_sources` for a Kest-scripted game
/// with controls: bots then play it through its action set and sample
/// function, pressing controls as a player would. For a game without
/// controls it provides sources that refuse, and bots steer at random. The
/// game is the Runtime's game files (`rawframe.world_kest.game_files`).
/// Configuration keys:
///
///   kest.sample_heap_bytes   a sample machine's heap ceiling (1048576)
///   kest.sample_fuel         steps one sample may take (1000000)
void registerParticipants(composition::ParticipantRegistrar& registrar) noexcept;

inline constexpr std::uint8_t kScopes = composition::scopeBit(composition::LifetimeScope::World);

} // namespace rawframe::input_kest
