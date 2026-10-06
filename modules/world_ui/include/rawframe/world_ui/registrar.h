#pragma once

#include "rawframe/composition/registrar.h"

#include <cstdint>

namespace rawframe::world_ui {

/// Contributes the World-scoped participant `rawframe.world_ui.ui`, never on
/// a dedicated server: in each `presentation_extract` it mirrors every
/// `rawframe.ui.Node` component of each local player's World into the UI
/// tree, lays each player's out in the player's view (its region of the
/// game's layout, constrained to the game's aspect), and draws it, lent as
/// `rawframe.ui.frames`. It logs `ui_summary` when the World stops. A
/// game without node components, or a process without clients, leaves it
/// idle.
void registerParticipants(composition::ParticipantRegistrar& registrar) noexcept;

inline constexpr std::uint8_t kScopes = composition::scopeBit(composition::LifetimeScope::World);

} // namespace rawframe::world_ui
