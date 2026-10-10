#pragma once

#include "rawframe/composition/registrar.h"

#include <cstdint>

namespace rawframe::xr {

/// Contributes the Runtime-scoped participant `rawframe.xr.headset` (D593):
/// with `xr.headset = true` it opens the OpenXR runtime's head-mounted
/// system and lends it as `rawframe.render.headset` (`render::Headset`), so
/// the runtime makes the render module's device and the frames are drawn
/// into the headset's views, through a session made once the device is
/// ready. Where no runtime answers, it says so and lends a headset that is
/// not present: the process plays flat, on its windows. Never in a
/// dedicated server (ADR-0081, scenario 3). It logs the session's states as
/// they change and a summary of its frames when it stops.
///
/// Configuration keys:
///
///   xr.headset   `true` to show the game on a headset when a runtime
///                answers (`false`)
void registerParticipants(composition::ParticipantRegistrar& registrar) noexcept;

inline constexpr std::uint8_t kScopes = composition::scopeBit(composition::LifetimeScope::Runtime);

} // namespace rawframe::xr
