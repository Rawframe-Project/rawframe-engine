#pragma once

#include <cstdint>
#include <maul-rhi/encoder.h>
#include <maul-rhi/frame.h>
#include <maul-rhi/resources.h>

namespace rawframe::render_scene_gpu {

// A slot of a binding table as the scene's passes fill them: a buffer's
// first `bytes`, a colored texture's every level, a depth texture's depth,
// or a sampler.
[[nodiscard]] mrhiBinding bufferAt(std::uint32_t slot, mrhiResourceId resource, std::uint64_t bytes) noexcept;
[[nodiscard]] mrhiBinding textureAt(std::uint32_t slot, mrhiResourceId resource) noexcept;
[[nodiscard]] mrhiBinding depthAt(std::uint32_t slot, mrhiResourceId resource) noexcept;
[[nodiscard]] mrhiBinding samplerAt(std::uint32_t slot, mrhiSamplerId sampler) noexcept;

} // namespace rawframe::render_scene_gpu
