#include "temporal.h"

#include <maul-rhi/encoder.h>

namespace rawframe::render_scene_gpu {

namespace {

mrhiAccess wholeOf(mrhiResourceId resource, mrhiAccessKind kind) noexcept {
    return mrhiAccess{
        .resource = resource,
        .kind = kind,
        .range = {.baseMip = 0, .mipCount = MRHI_REMAINING, .baseLayer = 0, .layerCount = 1, .aspect = {}}};
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

} // namespace

TemporalPass::TemporalPass(mrhiDevice* native) noexcept : native_(native) {
}

TemporalPass::~TemporalPass() {
    // Maul RHI retires what a frame still uses once the frame is done.
    drop();
}

void TemporalPass::drop() noexcept {
    for (mrhiTextureId& texture : pictures_) {
        if (native_ != nullptr && texture.index1 != 0) {
            static_cast<void>(mrhiDestroyTexture(native_, texture));
        }
        texture = {};
    }
    width_ = 0;
    height_ = 0;
    ready_ = false;
}

result::Status TemporalPass::sized(std::uint32_t width, std::uint32_t height) {
    if (width_ == width && height_ == height) {
        return {};
    }
    drop();
    for (mrhiTextureId& texture : pictures_) {
        mrhiTextureDef def = mrhiDefaultTextureDef();
        def.format = kSceneFormat;
        def.width = width;
        def.height = height;
        def.usage = mrhi_textureSampled | mrhi_textureRenderTarget;
        if (const mrhiResult kMade = mrhiCreateTexture(native_, &def, &texture); kMade != mrhi_success) {
            drop();
            return failed("the temporal pictures could not be made", kMade);
        }
    }
    width_ = width;
    height_ = height;
    return {};
}

result::Status TemporalPass::declare(const render_scene::SceneFrame& frame,
                                     std::uint32_t width,
                                     std::uint32_t height,
                                     std::vector<mrhiAccess>& writes) {
    enabled_ = frame.temporal.enabled;
    if (!enabled_) {
        return {};
    }
    RAWFRAME_TRY(sized(width, height));
    writing_ = ready_ ? 1 - last_ : 0;
    block_.state = {frame.temporal.history && ready_ ? 1.0F : 0.0F, 0, 0, 0};
    if (const mrhiResult kImported = mrhiImportTexture(native_, pictures_[writing_], &resolved_);
        kImported != mrhi_success) {
        return failed("a temporal picture could not join the frame", kImported);
    }
    if (const mrhiResult kImported = mrhiImportTexture(native_, pictures_[1 - writing_], &before_);
        kImported != mrhi_success) {
        return failed("a temporal picture could not join the frame", kImported);
    }
    mrhiBufferDef def = mrhiDefaultBufferDef();
    def.size = sizeof(TemporalBlock);
    if (mrhiDeclareBuffer(native_, &def, &state_) != mrhi_success) {
        return failed("the temporal pass's state could not be declared", mrhi_errorCapacity);
    }
    writes.push_back(wholeOf(state_, mrhi_accessCopyDestination));
    return {};
}

bool TemporalPass::enabled() const noexcept {
    return enabled_;
}

bool TemporalPass::reused() const noexcept {
    return enabled_ && block_.state[0] > 0;
}

mrhiResourceId TemporalPass::shown(mrhiResourceId scene) const noexcept {
    return enabled_ ? resolved_ : scene;
}

result::Status TemporalPass::addPass(mrhiResourceId scene, mrhiResourceId motion) {
    if (!enabled_) {
        return {};
    }
    const std::array<mrhiAccess, 4> kReads = {wholeOf(scene, mrhi_accessSampled),
                                              wholeOf(before_, mrhi_accessSampled),
                                              wholeOf(motion, mrhi_accessSampled),
                                              wholeOf(state_, mrhi_accessUniform)};
    mrhiPassDef def = mrhiDefaultPassDef();
    def.colorTargets[0].resource = resolved_;
    def.colorTargets[0].load = mrhi_loadDiscard;
    def.colorTargets[0].store = mrhi_storeKeep;
    def.colorTargetCount = 1;
    def.accesses = kReads.data();
    def.accessCount = static_cast<std::uint32_t>(kReads.size());
    if (const mrhiResult kAdded = mrhiAddPass(native_, &def, &pass_); kAdded != mrhi_success) {
        return failed("the temporal pass could not be added", kAdded);
    }
    return {};
}

result::Status TemporalPass::write(mrhiPassId upload) {
    if (enabled_ && mrhiWriteBuffer(native_, upload, state_, 0, &block_, sizeof(TemporalBlock)) != mrhi_success) {
        return failed("the temporal pass's state could not be written", mrhi_errorCapacity);
    }
    return {};
}

result::Status TemporalPass::record(const Pipelines& pipelines, mrhiResourceId scene, mrhiResourceId motion) {
    if (!enabled_) {
        return {};
    }
    const std::array<mrhiBinding, 5> kBindings = {textureAt(0, scene),
                                                  textureAt(1, before_),
                                                  textureAt(2, motion),
                                                  mrhiBinding{.slot = 3,
                                                              .resource = {},
                                                              .offset = 0,
                                                              .size = 0,
                                                              .viewKind = mrhi_texture2d,
                                                              .viewFormat = mrhi_formatNone,
                                                              .range = {},
                                                              .sampler = pipelines.historySampler},
                                                  mrhiBinding{.slot = 4,
                                                              .resource = state_,
                                                              .offset = 0,
                                                              .size = sizeof(TemporalBlock),
                                                              .viewKind = mrhi_texture2d,
                                                              .viewFormat = mrhi_formatNone,
                                                              .range = {},
                                                              .sampler = {}}};
    if (mrhiBeginPass(native_, pass_) != mrhi_success ||
        mrhiSetGraphicsPipeline(native_, pass_, pipelines.temporal.pipeline) != mrhi_success ||
        mrhiSetBindings(native_, pass_, 0, kBindings.data(), kBindings.size()) != mrhi_success ||
        mrhiDraw(native_, pass_, 3, 1, 0, 0) != mrhi_success || mrhiEndPass(native_, pass_) != mrhi_success) {
        return failed("the temporal pass could not be drawn", mrhi_errorState);
    }
    return {};
}

void TemporalPass::ended(bool submitted) noexcept {
    ready_ = submitted && enabled_;
    if (ready_) {
        last_ = writing_;
    }
}

} // namespace rawframe::render_scene_gpu
