#pragma once

#include "rawframe/composition/registrar.h"

#include <cstdint>

namespace rawframe::world_animation {

/// Contributes `rawframe.animation.world`, a World-scoped participant that
/// adds animation (animation.h) to the World runtime's Simulation when the
/// loaded game's animation plan asks for it, and does nothing otherwise. It
/// logs its totals when it stops.
void registerParticipants(composition::ParticipantRegistrar& registrar) noexcept;

inline constexpr std::uint8_t kScopes = composition::scopeBit(composition::LifetimeScope::World);

} // namespace rawframe::world_animation
