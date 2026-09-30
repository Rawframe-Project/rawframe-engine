#pragma once

#include "pipelines.h"
#include "rawframe/render/textures.h"
#include "rawframe/render_scene/scene.h"

#include <cstdint>
#include <maul-rhi/encoder.h>
#include <maul-rhi/frame.h>
#include <maul-rhi/resources.h>

namespace rawframe::render_scene_gpu {

// A slot of a binding table as the scene's passes fill them: a buffer's
// first `bytes`, a colored texture's every level, a depth texture's depth,
// a cube, or a sampler.
[[nodiscard]] mrhiBinding bufferAt(std::uint32_t slot, mrhiResourceId resource, std::uint64_t bytes) noexcept;
[[nodiscard]] mrhiBinding textureAt(std::uint32_t slot, mrhiResourceId resource) noexcept;
[[nodiscard]] mrhiBinding depthAt(std::uint32_t slot, mrhiResourceId resource) noexcept;
/// A cube's six faces and every level, seen as a cube (D322).
[[nodiscard]] mrhiBinding cubeAt(std::uint32_t slot, mrhiResourceId resource) noexcept;
[[nodiscard]] mrhiBinding samplerAt(std::uint32_t slot, mrhiSamplerId sampler) noexcept;

/// A resource of the open frame from the key `render` names it by.
[[nodiscard]] mrhiResourceId resourceOf(std::uint64_t key) noexcept;

/// A material's texture and its sampler into two slots of a table (D309):
/// the texture held this frame, or white for none and for one not held.
void bindTexture(const render::DeviceTextures& textures,
                 const Pipelines& pipelines,
                 mrhiBinding& image,
                 mrhiBinding& filter,
                 const render_scene::SceneTexture& texture) noexcept;

} // namespace rawframe::render_scene_gpu
