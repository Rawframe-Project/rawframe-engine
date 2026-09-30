#include "picture.h"

#include "tables.h"

#include <array>
#include <maul-rhi/encoder.h>

namespace rawframe::render_scene_gpu {

namespace {

mrhiAccess wholeOf(mrhiResourceId resource, mrhiAccessKind kind) noexcept {
    return mrhiAccess{
        .resource = resource,
        .kind = kind,
        .range = {.baseMip = 0, .mipCount = MRHI_REMAINING, .baseLayer = 0, .layerCount = 1, .aspect = {}}};
}

} // namespace

PicturePass::PicturePass(mrhiDevice* native) noexcept : native_(native) {
}

result::Status PicturePass::declare(const render_scene::SceneFrame& frame,
                                    bool made,
                                    std::uint32_t width,
                                    std::uint32_t height,
                                    std::size_t bloomLevels,
                                    std::vector<mrhiAccess>& writes) {
    block_ = pictureOf(frame);
    if (bloomLevels > 0) {
        block_.bloom = {frame.bloom.intensity, 1.0F / static_cast<float>(bloomLevels), 0, 0};
    }
    mrhiBufferDef blockDef = mrhiDefaultBufferDef();
    blockDef.size = sizeof(PictureBlock);
    if (mrhiDeclareBuffer(native_, &blockDef, &blockResource_) != mrhi_success) {
        return failed("the picture's grade could not be declared", mrhi_errorCapacity);
    }
    writes.push_back(wholeOf(blockResource_, mrhi_accessCopyDestination));
    smoothed_ = frame.fxaa && made;
    if (smoothed_) {
        mrhiTextureDef def = mrhiDefaultTextureDef();
        def.format = kPictureFormat;
        def.width = width;
        def.height = height;
        if (const mrhiResult kDeclared = mrhiDeclareTexture(native_, &def, &display_); kDeclared != mrhi_success) {
            return failed("a target could not be declared", kDeclared);
        }
    }
    return {};
}

bool PicturePass::smoothed() const noexcept {
    return smoothed_;
}

result::Status
PicturePass::addPasses(mrhiResourceId shown, mrhiResourceId spread, mrhiResourceId picture, bool clears) {
    shown_ = shown;
    spread_ = spread;
    // Every pixel of the picture written, over whatever was there; with
    // FXAA, the tonemapped picture first, then FXAA over it.
    mrhiPassDef pictureDef = mrhiDefaultPassDef();
    pictureDef.colorTargets[0].resource = picture;
    pictureDef.colorTargets[0].load = clears ? mrhi_loadClear : mrhi_loadKeep;
    pictureDef.colorTargets[0].store = mrhi_storeKeep;
    pictureDef.colorTargets[0].clear = mrhiClearColor{.red = 0, .green = 0, .blue = 0, .alpha = 1};
    pictureDef.colorTargetCount = 1;
    pictureDef.neverCull = true;
    mrhiPassDef tonemapDef = pictureDef;
    if (smoothed_) {
        tonemapDef.colorTargets[0].resource = display_;
        tonemapDef.colorTargets[0].load = mrhi_loadDiscard;
    }
    std::vector<mrhiAccess> reads = {wholeOf(shown_, mrhi_accessSampled), wholeOf(blockResource_, mrhi_accessUniform)};
    if (spread_.index1 != 0) {
        reads.push_back(wholeOf(spread_, mrhi_accessSampled));
    }
    tonemapDef.accesses = reads.data();
    tonemapDef.accessCount = static_cast<std::uint32_t>(reads.size());
    if (const mrhiResult kAdded = mrhiAddPass(native_, &tonemapDef, &tonemapPass_); kAdded != mrhi_success) {
        return failed("the picture's pass could not be added", kAdded);
    }
    if (!smoothed_) {
        return {};
    }
    const mrhiAccess kDisplay = wholeOf(display_, mrhi_accessSampled);
    pictureDef.accesses = &kDisplay;
    pictureDef.accessCount = 1;
    if (const mrhiResult kAdded = mrhiAddPass(native_, &pictureDef, &fxaaPass_); kAdded != mrhi_success) {
        return failed("the FXAA pass could not be added", kAdded);
    }
    return {};
}

result::Status PicturePass::write(mrhiPassId upload) {
    if (mrhiWriteBuffer(native_, upload, blockResource_, 0, &block_, sizeof(PictureBlock)) != mrhi_success) {
        return failed("the picture's grade could not be written", mrhi_errorCapacity);
    }
    return {};
}

result::Status PicturePass::record(const Pipelines& pipelines) {
    // The bloom's spread light, or the light shown where there is none,
    // which the picture's shader does not read then (D328).
    const std::array<mrhiBinding, 4> kSceneBinding = {textureAt(0, shown_),
                                                      bufferAt(1, blockResource_, sizeof(PictureBlock)),
                                                      textureAt(2, spread_.index1 != 0 ? spread_ : shown_),
                                                      samplerAt(3, pipelines.filteredSampler)};
    if (mrhiBeginPass(native_, tonemapPass_) != mrhi_success ||
        mrhiSetGraphicsPipeline(native_, tonemapPass_, pipelines.tonemap.pipeline) != mrhi_success ||
        mrhiSetBindings(native_, tonemapPass_, 0, kSceneBinding.data(), kSceneBinding.size()) != mrhi_success ||
        mrhiDraw(native_, tonemapPass_, 3, 1, 0, 0) != mrhi_success ||
        mrhiEndPass(native_, tonemapPass_) != mrhi_success) {
        return failed("the picture could not be drawn", mrhi_errorState);
    }
    if (!smoothed_) {
        return {};
    }
    const std::array<mrhiBinding, 2> kDisplayBinding = {textureAt(0, display_),
                                                        samplerAt(1, pipelines.filteredSampler)};
    if (mrhiBeginPass(native_, fxaaPass_) != mrhi_success ||
        mrhiSetGraphicsPipeline(native_, fxaaPass_, pipelines.fxaa.pipeline) != mrhi_success ||
        mrhiSetBindings(native_, fxaaPass_, 0, kDisplayBinding.data(), kDisplayBinding.size()) != mrhi_success ||
        mrhiDraw(native_, fxaaPass_, 3, 1, 0, 0) != mrhi_success || mrhiEndPass(native_, fxaaPass_) != mrhi_success) {
        return failed("the picture could not be antialiased", mrhi_errorState);
    }
    return {};
}

} // namespace rawframe::render_scene_gpu
