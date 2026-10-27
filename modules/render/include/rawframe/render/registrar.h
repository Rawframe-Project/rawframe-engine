#pragma once

#include "rawframe/composition/registrar.h"

#include <cstdint>

namespace rawframe::render {

/// Contributes the Runtime-scoped participant `rawframe.render.device`,
/// which asks for the process's one device when it starts, moves
/// its opening on in `platform_poll` until it is ready, and lends it as
/// `rawframe.render.device` (`DeviceHolder`). Never in a dedicated server.
/// Without `render.device` it asks for none, and lends none. Where a
/// headset that answered is lent (`rawframe.render.headset`), its
/// runtime makes the device. Where the host
/// lends its windows (`rawframe.window.surfaces`), it waits for the
/// first window's surface before asking, makes each window's surface
/// generation a surface of the device, prepares a window's surface for a
/// frame that draws to it, and logs a summary of them when it stops.
///
/// Configuration keys:
///
///   render.device    which adapters may serve: `hardware` (a GPU) or `any`
///                    (a software rasterizer too: CI and tests); absent for
///                    no device
///   render.present   the present policy: `vsync` (the default),
///                    `adaptive_vsync`, `low_latency_vsync`, or `immediate`
///   render.unavailable_says  `true` to say in the platform's message box,
///                    once, that no device could be had, why, and what to
///                    try (`false`): for a program that is nothing without
///                    its view, as an editor is
///
/// And the World-scoped participant `rawframe.render.frame`, which
/// owns the frames (`rawframe.render.frames`, frame.h): at the start of each
/// `present` it plans one, at the first window's size where the host lends
/// its windows, else offscreen when `render.offscreen` asks; the bridges
/// that joined (the scene, the canvas) prepare their parts and say they are
/// ready, and the last makes the frame, one on the GPU at a time. Where a
/// headset is lent and present, each planned frame begins the headset's,
/// paced by its runtime, and the picture is placed into its views' images
/// as well, at their size where no window shows it. Every so
/// often it reads the picture back and counts the pixels drawn over and
/// its colors. It logs a summary when the World stops. Without a device,
/// or without a window or `render.offscreen`, or with no bridge joined, it
/// is idle.
///
///   render.offscreen   `true` to draw offscreen (`false`)
///   render.width       the offscreen picture's width in pixels (1280)
///   render.height      its height (720)
///   render.read_every  one frame in this many is read back (60; 0 for
///                      none)
///   render.capture     a TGA file the last picture read back that holds
///                      more than one color is written to when the World
///                      stops
///   render.stream      a file each picture read back is put in, whole
///                      (capture.h's stream file), for an editor showing
///                      them; with render.offscreen, drawn
///                      offscreen though a window is lent
///   render.frame_rate  the most frames made a second, as a game's frame
///                      limit (0, as many as the Host's iterations)
void registerParticipants(composition::ParticipantRegistrar& registrar) noexcept;

inline constexpr std::uint8_t kScopes = composition::scopeBit(composition::LifetimeScope::Runtime) |
                                        composition::scopeBit(composition::LifetimeScope::World);

} // namespace rawframe::render
