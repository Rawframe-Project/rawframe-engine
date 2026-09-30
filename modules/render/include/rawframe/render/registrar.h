#pragma once

#include "rawframe/composition/registrar.h"

#include <cstdint>

namespace rawframe::render {

/// Contributes the Runtime-scoped participant `rawframe.render.device`
/// (D279), which asks for the process's one device when it starts, moves
/// its opening on in `platform_poll` until it is ready, and lends it as
/// `rawframe.render.device` (`DeviceHolder`). Never in a dedicated server.
/// Without `render.device` it asks for none, and lends none. Where the host
/// lends its windows (`rawframe.window.surfaces`, D280), it waits for the
/// first window's surface before asking, makes each window's surface
/// generation a surface of the device, prepares a window's surface for a
/// frame that draws to it, and logs a summary of them when it stops.
///
/// Configuration keys:
///
///   render.device    which adapters may serve: `hardware` (a GPU) or `any`
///                    (a software rasterizer too: CI and tests); absent for
///                    no device
///   render.present   SPEC-0024's present policy: `vsync` (the default),
///                    `adaptive_vsync`, `low_latency_vsync`, or `immediate`
void registerParticipants(composition::ParticipantRegistrar& registrar) noexcept;

inline constexpr std::uint8_t kScopes = composition::scopeBit(composition::LifetimeScope::Runtime);

} // namespace rawframe::render
