#pragma once

#include "rawframe/composition/registrar.h"

#include <cstdint>

namespace rawframe::world_animation {

/// Contributes `rawframe.animation.world`, a World-scoped participant that
/// adds animation (animation.h) to the World runtime's Simulation when the
/// loaded game's animation plan asks for it, and does nothing otherwise. It
/// logs its totals when it stops. A dedicated server's composition has
/// `rawframe.animation.server_world` in its place, which plays only
/// animators of `Simulation` relevance whatever the plan says.
///
/// Beside it, where anything is drawn, `rawframe.animation.presented` plays
/// every animator of the World a client mirrors from its server (D258), the
/// one of the client that plays or else the first, once a frame in
/// `run_worlds`, the game's ticks (`world.tick_rate`, 60) the frame's time
/// holds, at most four; it logs its totals when it stops.
void registerParticipants(composition::ParticipantRegistrar& registrar) noexcept;

inline constexpr std::uint8_t kScopes = composition::scopeBit(composition::LifetimeScope::World);

} // namespace rawframe::world_animation
