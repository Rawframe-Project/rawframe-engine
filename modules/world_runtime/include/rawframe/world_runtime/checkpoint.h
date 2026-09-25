#pragma once

#include "rawframe/composition/participant.h"
#include "rawframe/result/result.h"
#include "rawframe/world_snapshot/checkpoint.h"
#include "rawframe/world_snapshot/projection.h"

namespace rawframe::world_runtime {

/// What a World's checkpoints hold and what they are of, from whoever
/// defines the World's components: a game.
class CheckpointPlan {
public:
    CheckpointPlan() = default;
    CheckpointPlan(const CheckpointPlan&) = delete;
    CheckpointPlan& operator=(const CheckpointPlan&) = delete;
    virtual ~CheckpointPlan() = default;

    /// The persisted projection, or why the World cannot be checkpointed.
    [[nodiscard]] virtual result::Result<const world_snapshot::SnapshotProjection*> projection() const = 0;
    [[nodiscard]] virtual world_snapshot::CheckpointIdentity identity() const noexcept = 0;
};

inline constexpr composition::Capability<CheckpointPlan> kCheckpointPlan{"rawframe.world.checkpoint_plan"};

} // namespace rawframe::world_runtime
