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
///
/// And `rawframe.world_runtime.checkpoints`, which restores the World from a
/// SPEC-0011 checkpoint before the first tick and captures checkpoints at
/// exact ticks, between two of them, for a World whose game provides a
/// CheckpointPlan:
///
///   checkpoint.restore         the checkpoint to start from
///   checkpoint.capture_ticks   increasing ticks to capture at, space-separated
///   checkpoint.capture_prefix  a capture at tick T is written to <prefix>T.rfsn
void registerParticipants(composition::ParticipantRegistrar& registrar) noexcept;

inline constexpr std::uint8_t kScopes = composition::scopeBit(composition::LifetimeScope::World);

} // namespace rawframe::world_runtime
