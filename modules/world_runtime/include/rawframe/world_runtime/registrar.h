#pragma once

#include "rawframe/composition/registrar.h"

namespace rawframe::world_runtime {

/// Contributes the World participant, `rawframe.world_runtime.world`: a
/// World-scoped participant providing kSimulation and ticking in the Host's
/// `run_worlds` phase. It reads these configuration keys:
///
///   world.tick_rate                   ticks per `world.tick_rate_seconds` (60)
///   world.tick_rate_seconds           (1)
///   world.maximum_ticks_per_iteration catch-up bound per Host iteration (4)
///   world.root_seed                   the random root seed (0)
///   world.maximum_entities            the entity ceiling (1048576)
void registerParticipants(composition::ParticipantRegistrar& registrar) noexcept;

inline constexpr std::uint8_t kScopes = composition::scopeBit(composition::LifetimeScope::World);

} // namespace rawframe::world_runtime
