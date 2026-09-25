#pragma once

#include "rawframe/composition/registrar.h"

namespace rawframe::world_kest {

/// Contributes `rawframe.world_kest.game_files`, the Runtime's game files
/// (game_files.h), read when the Runtime is composed.
void registerGameFiles(composition::ParticipantRegistrar& registrar) noexcept;

} // namespace rawframe::world_kest
