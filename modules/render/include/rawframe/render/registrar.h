#pragma once

#include "rawframe/composition/registrar.h"

#include <cstdint>

namespace rawframe::render {

/// Contributes the Runtime-scoped participant `rawframe.render.device`
/// (D279), which asks for the process's one device when it starts, moves
/// its opening on in `platform_poll` until it is ready, and lends it as
/// `rawframe.render.device` (`DeviceHolder`). Never in a dedicated server.
/// Without `render.device` it asks for none, and lends none.
///
/// Configuration keys:
///
///   render.device   which adapters may serve: `hardware` (a GPU) or `any`
///                   (a software rasterizer too: CI and tests); absent for
///                   no device
void registerParticipants(composition::ParticipantRegistrar& registrar) noexcept;

inline constexpr std::uint8_t kScopes = composition::scopeBit(composition::LifetimeScope::Runtime);

} // namespace rawframe::render
