#pragma once

#include "rawframe/composition/registrar.h"

namespace rawframe::world_runtime {

/// Contributes `rawframe.world_runtime.checkpoints` (registrar.h).
void registerCheckpoints(composition::ParticipantRegistrar& registrar) noexcept;

} // namespace rawframe::world_runtime
