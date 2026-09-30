#pragma once

#include "rawframe/composition/registrar.h"

#include <cstdint>

namespace rawframe::render_scene_gpu {

/// Contributes the World-scoped participant
/// `rawframe.render_scene_gpu.drawing` (D284), which records each frame the
/// scene queued (`rawframe.render_scene.frames`) on the one device
/// (`rawframe.render.device`) into an offscreen picture at the view's size,
/// with one frame on the GPU at a time: a frame queued while the last is
/// still running is not drawn. Every so often it reads the picture back and
/// counts its colors. It logs a summary when the World stops. Without a
/// device or a scene, or without `scene.offscreen`, it is idle; shown on a
/// window, the scene waits for frame ownership in `render`.
///
/// Configuration keys:
///
///   scene.offscreen             `true` to draw offscreen (`false`)
///   scene.read_every            one frame drawn in this many is read back
///                               (60; 0 for none)
///   scene.capture               a TGA file the last frame read back is
///                               written to when the World stops
void registerParticipants(composition::ParticipantRegistrar& registrar) noexcept;

inline constexpr std::uint8_t kScopes = composition::scopeBit(composition::LifetimeScope::World);

} // namespace rawframe::render_scene_gpu
