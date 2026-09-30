#pragma once

#include "rawframe/composition/registrar.h"

#include <cstdint>

namespace rawframe::render_canvas_gpu {

/// Contributes the World-scoped participant
/// `rawframe.render_canvas_gpu.offscreen` (D279), which records each frame
/// the canvas queued (`rawframe.render_canvas.frames`) on the one device
/// (`rawframe.render.device`) into an offscreen target of the view's size,
/// with one frame on the GPU at a time: a frame queued while the last is
/// still running is not drawn. Every so often it reads the target back and
/// counts the pixels the sprites covered. It logs a summary when the World
/// stops. Without `canvas.offscreen`, a device, or a canvas, it is idle.
///
/// Configuration keys:
///
///   canvas.offscreen             `true` to draw offscreen (`false`)
///   canvas.offscreen_read_every  one frame drawn in this many is read back
///                                (60; 0 for none)
///   canvas.offscreen_capture     a TGA file the last frame read back is
///                                written to when the World stops
void registerParticipants(composition::ParticipantRegistrar& registrar) noexcept;

inline constexpr std::uint8_t kScopes = composition::scopeBit(composition::LifetimeScope::World);

} // namespace rawframe::render_canvas_gpu
