#pragma once

// The queue stage's reflection probes (ADR-0051, D325): which of the
// extracted probes a frame keeps, and which one each draw reflects.

#include "rawframe/render_scene/scene.h"

#include <array>
#include <span>

namespace rawframe::render_scene {

/// Keeps the usable probes, those with a positive, finite box and an
/// environment, the nearest to `eye` first, at most `maximum`, placed
/// relative to the eye; and gives each draw the one whose box holds its
/// middle: the highest priority, then the smallest box, then the first
/// kept. A draw in none reflects the sky's picture.
void resolveProbes(SceneFrame& frame,
                   std::span<const ProbeInstance> probes,
                   const std::array<double, 3>& eye,
                   std::size_t maximum);

} // namespace rawframe::render_scene
