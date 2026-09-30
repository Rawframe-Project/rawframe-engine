#include "reflection.h"

#include "environment.h"
#include "tables.h"

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

ReflectionPass::ReflectionPass(mrhiDevice* native) noexcept : native_(native) {
}

result::Status ReflectionPass::declare(const render_scene::SceneFrame& frame,
                                       bool made,
                                       const FrameBlock& block,
                                       mrhiResourceId before,
                                       std::uint32_t width,
                                       std::uint32_t height,
                                       std::vector<mrhiAccess>& writes) {
    enabled_ = frame.reflections.enabled && made && before.index1 != 0;
    if (!enabled_) {
        return {};
    }
    before_ = before;
    // The near plane, where the projection keeps it (reversed-Z, D284).
    block_ = ReflectionBlock{.toPoint = inverseOf(block.viewProjection),
                             .viewProjection = block.viewProjection,
                             .previous = block.previous,
                             .settings = {frame.reflections.distance, frame.projection[14], 0, 0}};
    mrhiTextureDef def = mrhiDefaultTextureDef();
    def.format = kReflectionFormat;
    def.width = width;
    def.height = height;
    if (const mrhiResult kDeclared = mrhiDeclareTexture(native_, &def, &reflected_); kDeclared != mrhi_success) {
        return failed("the reflections' target could not be declared", kDeclared);
    }
    mrhiBufferDef blockDef = mrhiDefaultBufferDef();
    blockDef.size = sizeof(ReflectionBlock);
    if (mrhiDeclareBuffer(native_, &blockDef, &blockResource_) != mrhi_success) {
        return failed("the reflections' view could not be declared", mrhi_errorCapacity);
    }
    writes.push_back(wholeOf(blockResource_, mrhi_accessCopyDestination));
    return {};
}

bool ReflectionPass::enabled() const noexcept {
    return enabled_;
}

mrhiResourceId ReflectionPass::reflected() const noexcept {
    return reflected_;
}

result::Status ReflectionPass::addPasses(mrhiResourceId depth, mrhiResourceId surfaces) {
    if (!enabled_) {
        return {};
    }
    depth_ = depth;
    surfaces_ = surfaces;
    const std::array<mrhiAccess, 4> kReads = {mrhiAccess{.resource = depth_,
                                                         .kind = mrhi_accessSampled,
                                                         .range = {.baseMip = 0,
                                                                   .mipCount = MRHI_REMAINING,
                                                                   .baseLayer = 0,
                                                                   .layerCount = 1,
                                                                   .aspect = mrhi_aspectDepthOnly}},
                                              wholeOf(surfaces_, mrhi_accessSampled),
                                              wholeOf(before_, mrhi_accessSampled),
                                              wholeOf(blockResource_, mrhi_accessUniform)};
    mrhiPassDef def = mrhiDefaultPassDef();
    def.colorTargets[0].resource = reflected_;
    def.colorTargets[0].load = mrhi_loadDiscard;
    def.colorTargets[0].store = mrhi_storeKeep;
    def.colorTargetCount = 1;
    def.accesses = kReads.data();
    def.accessCount = static_cast<std::uint32_t>(kReads.size());
    if (const mrhiResult kAdded = mrhiAddPass(native_, &def, &pass_); kAdded != mrhi_success) {
        return failed("the reflections' pass could not be added", kAdded);
    }
    return {};
}

result::Status ReflectionPass::write(mrhiPassId upload) {
    if (enabled_ &&
        mrhiWriteBuffer(native_, upload, blockResource_, 0, &block_, sizeof(ReflectionBlock)) != mrhi_success) {
        return failed("the reflections' view could not be written", mrhi_errorCapacity);
    }
    return {};
}

result::Status ReflectionPass::record(const Pipelines& pipelines) {
    if (!enabled_) {
        return {};
    }
    const std::array<mrhiBinding, 5> kBindings = {depthAt(0, depth_),
                                                  textureAt(1, surfaces_),
                                                  textureAt(2, before_),
                                                  samplerAt(3, pipelines.filteredSampler),
                                                  bufferAt(4, blockResource_, sizeof(ReflectionBlock))};
    if (mrhiBeginPass(native_, pass_) != mrhi_success ||
        mrhiSetGraphicsPipeline(native_, pass_, pipelines.march.pipeline) != mrhi_success ||
        mrhiSetBindings(native_, pass_, 0, kBindings.data(), kBindings.size()) != mrhi_success ||
        mrhiDraw(native_, pass_, 3, 1, 0, 0) != mrhi_success || mrhiEndPass(native_, pass_) != mrhi_success) {
        return failed("the reflections could not be drawn", mrhi_errorState);
    }
    return {};
}

} // namespace rawframe::render_scene_gpu
