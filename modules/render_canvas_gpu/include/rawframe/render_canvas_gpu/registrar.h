#pragma once

#include "rawframe/composition/registrar.h"

#include <cstdint>

namespace rawframe::render_canvas_gpu {

/// Contributes the World-scoped participant
/// `rawframe.render_canvas_gpu.drawing` (D279, D280), which records each
/// frame the canvas queued (`rawframe.render_canvas.frames`) on the one
/// device (`rawframe.render.device`), with one frame on the GPU at a time:
/// a frame queued while the last is still running is not drawn. Where the
/// host lends its windows (`rawframe.window.surfaces`), the picture is drawn
/// at the first window's size and shown on it; elsewhere it is drawn
/// offscreen at the view's size when `canvas.offscreen` asks. Every so often
/// it reads the picture back and counts the pixels the sprites covered. It
/// logs a summary when the World stops. Without a device or a canvas, and
/// without a window or `canvas.offscreen`, it is idle.
///
/// Configuration keys:
///
///   canvas.offscreen             `true` to draw offscreen (`false`)
///   canvas.read_every            one frame drawn in this many is read back
///                                (60; 0 for none)
///   canvas.capture               a TGA file the last frame read back is
///                                written to when the World stops
void registerParticipants(composition::ParticipantRegistrar& registrar) noexcept;

inline constexpr std::uint8_t kScopes = composition::scopeBit(composition::LifetimeScope::World);

} // namespace rawframe::render_canvas_gpu
