#pragma once

#include "rawframe/composition/registrar.h"

#include <cstdint>

namespace rawframe::render_canvas_gpu {

/// Contributes the World-scoped participant
/// `rawframe.render_canvas_gpu.drawing` (D279, D285), which records the
/// frame the canvas queued (`rawframe.render_canvas.frames`) into each frame
/// `render` makes (`rawframe.render.frames`), after the scene's, and sizes
/// the canvas's view to the frame. It logs a summary when the World stops.
/// Without frames or a canvas it is idle; the frames' configuration
/// (offscreen or on a window, read back, captured) is `render`'s.
void registerParticipants(composition::ParticipantRegistrar& registrar) noexcept;

inline constexpr std::uint8_t kScopes = composition::scopeBit(composition::LifetimeScope::World);

} // namespace rawframe::render_canvas_gpu
