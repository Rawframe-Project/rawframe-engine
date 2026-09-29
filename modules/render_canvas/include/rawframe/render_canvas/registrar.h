#pragma once

#include "rawframe/composition/registrar.h"

#include <cstdint>

namespace rawframe::render_canvas {

/// Contributes the World-scoped participant `rawframe.render_canvas.canvas`,
/// which draws one client's mirrored World as its player sees it: in
/// `presentation_extract` it copies the World's sprites out (SPEC-0024's
/// `extract` stage), and in `present` it queues them into a frame through a
/// camera following the player (the `queue` stage). It logs a summary when
/// the World stops. A game without a sprite component, or a process without
/// clients, leaves it idle.
///
/// Configuration keys:
///
///   canvas.client   which of the process's clients to draw (the one that
///                   plays, else 0)
///   canvas.width    the view's width in pixels (1280), for its aspect
///   canvas.height   the view's height in pixels (720)
///
/// The game is the Runtime's game files (`rawframe.world_kest.game_files`),
/// whose `texture` and `camera` lines it reads.
void registerParticipants(composition::ParticipantRegistrar& registrar) noexcept;

inline constexpr std::uint8_t kScopes = composition::scopeBit(composition::LifetimeScope::World);

} // namespace rawframe::render_canvas
