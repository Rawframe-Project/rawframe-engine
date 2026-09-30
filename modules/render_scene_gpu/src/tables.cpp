#include "tables.h"

namespace rawframe::render_scene_gpu {

mrhiBinding bufferAt(std::uint32_t slot, mrhiResourceId resource, std::uint64_t bytes) noexcept {
    return mrhiBinding{.slot = slot,
                       .resource = resource,
                       .offset = 0,
                       .size = bytes,
                       .viewKind = mrhi_texture2d,
                       .viewFormat = mrhi_formatNone,
                       .range = {},
                       .sampler = {}};
}

mrhiBinding textureAt(std::uint32_t slot, mrhiResourceId resource) noexcept {
    return mrhiBinding{
        .slot = slot,
        .resource = resource,
        .offset = 0,
        .size = 0,
        .viewKind = mrhi_texture2d,
        .viewFormat = mrhi_formatNone,
        .range = {.baseMip = 0, .mipCount = MRHI_REMAINING, .baseLayer = 0, .layerCount = 1, .aspect = {}},
        .sampler = {}};
}

mrhiBinding depthAt(std::uint32_t slot, mrhiResourceId resource) noexcept {
    mrhiBinding made = textureAt(slot, resource);
    made.range.aspect = mrhi_aspectDepthOnly;
    return made;
}

mrhiBinding samplerAt(std::uint32_t slot, mrhiSamplerId sampler) noexcept {
    return mrhiBinding{.slot = slot,
                       .resource = {},
                       .offset = 0,
                       .size = 0,
                       .viewKind = mrhi_texture2d,
                       .viewFormat = mrhi_formatNone,
                       .range = {},
                       .sampler = sampler};
}

} // namespace rawframe::render_scene_gpu
