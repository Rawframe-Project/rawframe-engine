#pragma once

#include "rawframe/composition/registrar.h"

#include <cstdint>

namespace rawframe::studio {

/// Contributes `rawframe.studio.shell` (ADR-0032, D435), a World-scoped
/// participant of Studio's own composition: it holds an authoring session
/// on a game in its process, speaking only the session's records, and
/// shows it as a UI of its own, laid out and drawn in each
/// `presentation_extract` and lent as `rawframe.ui.frames` for
/// render_canvas_gpu to draw: a header, and a column for the scenes beside
/// the game, the chosen scene's entities, and the chosen entity's
/// components. It logs `studio_summary` when it stops.
///
///   studio.game   the game description the session reads (required)
///   studio.root   the scenes' root (the description's directory)
void registerParticipants(composition::ParticipantRegistrar& registrar) noexcept;

inline constexpr std::uint8_t kScopes = composition::scopeBit(composition::LifetimeScope::World);

} // namespace rawframe::studio
