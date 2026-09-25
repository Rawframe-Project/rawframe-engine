#pragma once

#include "rawframe/composition/registrar.h"

#include <cstdint>

namespace rawframe::physics3d {

/// Contributes `rawframe.physics3d.world`, a World-scoped participant that
/// adds 3D physics (physics.h) to the World runtime's Simulation when the
/// loaded game's physics plan asks for it, and does nothing otherwise. It logs
/// its totals when it stops.
void registerParticipants(composition::ParticipantRegistrar& registrar) noexcept;

inline constexpr std::uint8_t kScopes = composition::scopeBit(composition::LifetimeScope::World);

} // namespace rawframe::physics3d
