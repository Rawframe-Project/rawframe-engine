#pragma once

// The textures a frame's materials sample by identity, beyond the
// renderer's own and the sky's and probes' pictures: what its emitters and
// ribbons (D353, D354), post processes (D350), decals (D339, D342), and
// draws name, in that order, which is the order they are chosen in within
// the frame's upload budget.

#include "rawframe/render_scene/scene.h"

#include <cstdint>
#include <vector>

namespace rawframe::render_scene_gpu {

[[nodiscard]] std::vector<std::uint64_t> sampledTexturesOf(const render_scene::SceneFrame& scene);

} // namespace rawframe::render_scene_gpu
