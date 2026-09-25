#pragma once

#include "rawframe/composition/registrar.h"

#include <cstdint>

namespace rawframe::physics2d {

/// Contributes `rawframe.physics2d.world`, a World-scoped participant that
/// adds 2D physics (physics.h) to the World runtime's Simulation when the
/// loaded game's physics plan asks for it, and does nothing otherwise. It logs
/// its totals when it stops.
void registerParticipants(composition::ParticipantRegistrar& registrar) noexcept;

inline constexpr std::uint8_t kScopes = composition::scopeBit(composition::LifetimeScope::World);

} // namespace rawframe::physics2d
