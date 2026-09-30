#pragma once

#include "rawframe/composition/registrar.h"

#include <cstdint>

namespace rawframe::render_scene_gpu {

/// Contributes the World-scoped participant
/// `rawframe.render_scene_gpu.drawing` (D284, D285), which records the
/// frame the scene queued (`rawframe.render_scene.frames`) into each frame
/// `render` makes (`rawframe.render.frames`), first, and sizes the scene's
/// view to the frame. It logs a summary when the World stops. Without
/// frames or a scene it is idle; the frames' configuration (offscreen or on
/// a window, read back, captured) is `render`'s.
void registerParticipants(composition::ParticipantRegistrar& registrar) noexcept;

inline constexpr std::uint8_t kScopes = composition::scopeBit(composition::LifetimeScope::World);

} // namespace rawframe::render_scene_gpu
