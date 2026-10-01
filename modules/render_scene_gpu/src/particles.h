#pragma once

// The scene's particles, trails, and beams as the shared device half draws
// them (D357): the view, and the scene's materials, as its shaders read
// them.

#include "blocks.h"
#include "pipelines.h"
#include "rawframe/particles_gpu/particles.h"
#include "rawframe/render/textures.h"
#include "rawframe/render_scene/scene.h"

#include <span>
#include <vector>

namespace rawframe::render_scene_gpu {

/// The view as `frame` sees it through `block`'s projection.
[[nodiscard]] particles_gpu::ViewBlock particleViewOf(const render_scene::SceneFrame& frame,
                                                      const FrameBlock& block) noexcept;

/// Each material's blob as the particles draw it, unlit (D353): its base
/// color, times its base texture's color where that multiplies it; its
/// opacity, times its texture's alpha where that multiplies it, which then
/// shapes it; and its emission, times its emission texture's color where
/// that multiplies it. Its textures (`textures`, by place) as `held` holds
/// them this frame, white for none and for one not held, sampled as
/// `pipelines` sample a material's.
[[nodiscard]] std::vector<particles_gpu::Material>
particleMaterialsOf(std::span<const render_scene::MaterialBlob> materials,
                    std::span<const render_scene::SceneTextures> textures,
                    const render::DeviceTextures& held,
                    const Pipelines& pipelines);

} // namespace rawframe::render_scene_gpu
