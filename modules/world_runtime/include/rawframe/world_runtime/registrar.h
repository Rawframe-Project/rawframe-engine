#pragma once

#include "rawframe/base/platform.h"
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

#if RAWFRAME_FILE_SYSTEM
/// Contributes `rawframe.world_runtime.saves` (ADR-0057), for a host whose
/// World keeps a game's save: it loads the slot when the World starts and
/// keeps it at the declared points, writing on the blocking-I/O executor,
/// which the composition must have. For a game that declares a save:
///
///   save.directory      where slots are kept; saves are off without it
///   save.slot           the slot, 1 to 64 of a-z, 0-9, _, - (world)
///   save.namespace      the persistence namespace, 32 hex digits
///   save.every_seconds  keep every this often; 0 keeps only at stop (0)
///
/// A kept save that does not read stops the start, so the next keep cannot
/// overwrite it. A World restored from a checkpoint (`checkpoint.restore`)
/// is not given its save: the checkpoint is the whole World, and the next
/// keep writes it.
void registerSaves(composition::ParticipantRegistrar& registrar) noexcept;
#endif

inline constexpr std::uint8_t kScopes = composition::scopeBit(composition::LifetimeScope::World);

} // namespace rawframe::world_runtime
