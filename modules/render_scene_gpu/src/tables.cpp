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

mrhiBinding arrayAt(std::uint32_t slot, mrhiResourceId resource) noexcept {
    mrhiBinding made = textureAt(slot, resource);
    made.viewKind = mrhi_texture2dArray;
    made.range.layerCount = MRHI_REMAINING;
    return made;
}

mrhiBinding cubeAt(std::uint32_t slot, mrhiResourceId resource) noexcept {
    mrhiBinding made = textureAt(slot, resource);
    made.viewKind = mrhi_textureCube;
    made.range.layerCount = 6;
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

mrhiResourceId resourceOf(std::uint64_t key) noexcept {
    return mrhiResourceId{.index1 = static_cast<std::uint32_t>(key >> 32U),
                          .generation = static_cast<std::uint32_t>(key)};
}

void bindTexture(const render::DeviceTextures& textures,
                 const Pipelines& pipelines,
                 mrhiBinding& image,
                 mrhiBinding& filter,
                 const render_scene::SceneTexture& texture) noexcept {
    const std::uint64_t kHeld = textures.resource(texture.id);
    image.resource = resourceOf(kHeld != 0 ? kHeld : textures.resource(0));
    filter.sampler = pipelines.materialSamplers[samplerOf(texture.filter, texture.address)];
}

} // namespace rawframe::render_scene_gpu
