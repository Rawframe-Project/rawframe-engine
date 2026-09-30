#pragma once

#include "rawframe/composition/registrar.h"

#include <cstdint>

namespace rawframe::render_scene {

/// Contributes the World-scoped participant `rawframe.render_scene.scene`,
/// which draws one client's mirrored World in 3D as its player sees it: in
/// `presentation_extract` it copies the World's models out (SPEC-0024's
/// `extract` stage), and in `present` it queues them into a frame through
/// the player's camera (the `queue` stage). It logs a summary when the
/// World stops. A game without a model component, or a process without
/// clients, leaves it idle.
///
/// Configuration keys:
///
///   scene.client   which of the process's clients to draw (the one that
///                  plays, else 0)
///   scene.width    the view's width in pixels (1280), for its aspect
///   scene.height   the view's height in pixels (720)
///   scene.shadow_cascades  the sun's shadow cascades, 0 to 4 (4; 0 for no
///                          shadows)
///   scene.shadow_side      a cascade's side in texels, 64 to 4096 (1024)
///   scene.shadow_distance  how far shadows reach, in meters (100)
///
/// The game is the Runtime's game files (`rawframe.world_kest.game_files`),
/// whose `rawframe.model` components and meshes it reads.
void registerParticipants(composition::ParticipantRegistrar& registrar) noexcept;

inline constexpr std::uint8_t kScopes = composition::scopeBit(composition::LifetimeScope::World);

} // namespace rawframe::render_scene
