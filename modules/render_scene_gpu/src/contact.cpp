#include "contact.h"

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

ContactPass::ContactPass(mrhiDevice* native) noexcept : native_(native) {
}

result::Status ContactPass::declare(const render_scene::SceneFrame& frame,
                                    bool made,
                                    const FrameBlock& block,
                                    std::uint32_t width,
                                    std::uint32_t height,
                                    std::vector<mrhiAccess>& writes) {
    enabled_ = frame.contactShadows.enabled && made;
    if (!enabled_) {
        return {};
    }
    // The near plane, where the projection keeps it (reversed-Z, D284).
    block_ = ContactBlock{.toPoint = inverseOf(block.viewProjection),
                          .viewProjection = block.viewProjection,
                          .toSun = block.toSun,
                          .settings = {frame.contactShadows.length, frame.projection[14], 0, 0}};
    mrhiTextureDef def = mrhiDefaultTextureDef();
    def.format = kAmbientFormat;
    def.width = width;
    def.height = height;
    if (const mrhiResult kDeclared = mrhiDeclareTexture(native_, &def, &lit_); kDeclared != mrhi_success) {
        return failed("the contact shadows' target could not be declared", kDeclared);
    }
    mrhiBufferDef blockDef = mrhiDefaultBufferDef();
    blockDef.size = sizeof(ContactBlock);
    if (mrhiDeclareBuffer(native_, &blockDef, &blockResource_) != mrhi_success) {
        return failed("the contact shadows' view could not be declared", mrhi_errorCapacity);
    }
    writes.push_back(wholeOf(blockResource_, mrhi_accessCopyDestination));
    return {};
}

bool ContactPass::enabled() const noexcept {
    return enabled_;
}

mrhiResourceId ContactPass::lit() const noexcept {
    return lit_;
}

result::Status ContactPass::addPasses(mrhiResourceId depth) {
    if (!enabled_) {
        return {};
    }
    depth_ = depth;
    const std::array<mrhiAccess, 2> kReads = {mrhiAccess{.resource = depth_,
                                                         .kind = mrhi_accessSampled,
                                                         .range = {.baseMip = 0,
                                                                   .mipCount = MRHI_REMAINING,
                                                                   .baseLayer = 0,
                                                                   .layerCount = 1,
                                                                   .aspect = mrhi_aspectDepthOnly}},
                                              wholeOf(blockResource_, mrhi_accessUniform)};
    mrhiPassDef def = mrhiDefaultPassDef();
    def.colorTargets[0].resource = lit_;
    def.colorTargets[0].load = mrhi_loadDiscard;
    def.colorTargets[0].store = mrhi_storeKeep;
    def.colorTargetCount = 1;
    def.accesses = kReads.data();
    def.accessCount = static_cast<std::uint32_t>(kReads.size());
    if (const mrhiResult kAdded = mrhiAddPass(native_, &def, &pass_); kAdded != mrhi_success) {
        return failed("the contact shadows' pass could not be added", kAdded);
    }
    return {};
}

result::Status ContactPass::write(mrhiPassId upload) {
    if (enabled_ &&
        mrhiWriteBuffer(native_, upload, blockResource_, 0, &block_, sizeof(ContactBlock)) != mrhi_success) {
        return failed("the contact shadows' view could not be written", mrhi_errorCapacity);
    }
    return {};
}

result::Status ContactPass::record(const Pipelines& pipelines) {
    if (!enabled_) {
        return {};
    }
    const std::array<mrhiBinding, 2> kBindings = {depthAt(0, depth_),
                                                  bufferAt(1, blockResource_, sizeof(ContactBlock))};
    if (mrhiBeginPass(native_, pass_) != mrhi_success ||
        mrhiSetGraphicsPipeline(native_, pass_, pipelines.contactShade.pipeline) != mrhi_success ||
        mrhiSetBindings(native_, pass_, 0, kBindings.data(), kBindings.size()) != mrhi_success ||
        mrhiDraw(native_, pass_, 3, 1, 0, 0) != mrhi_success || mrhiEndPass(native_, pass_) != mrhi_success) {
        return failed("the contact shadows could not be drawn", mrhi_errorState);
    }
    return {};
}

} // namespace rawframe::render_scene_gpu
