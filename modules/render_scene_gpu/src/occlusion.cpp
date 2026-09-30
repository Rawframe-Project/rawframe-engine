#include "occlusion.h"

#include "environment.h"
#include "tables.h"

#include <maul-rhi/encoder.h>
#include <tuple>
#include <utility>

namespace rawframe::render_scene_gpu {

namespace {

mrhiAccess wholeOf(mrhiResourceId resource, mrhiAccessKind kind) noexcept {
    return mrhiAccess{
        .resource = resource,
        .kind = kind,
        .range = {.baseMip = 0, .mipCount = MRHI_REMAINING, .baseLayer = 0, .layerCount = 1, .aspect = {}}};
}

mrhiAccess depthOf(mrhiResourceId resource) noexcept {
    return mrhiAccess{
        .resource = resource,
        .kind = mrhi_accessSampled,
        .range = {
            .baseMip = 0, .mipCount = MRHI_REMAINING, .baseLayer = 0, .layerCount = 1, .aspect = mrhi_aspectDepthOnly}};
}

} // namespace

OcclusionPass::OcclusionPass(mrhiDevice* native) noexcept : native_(native) {
}

result::Status OcclusionPass::declare(const render_scene::SceneFrame& frame,
                                      const FrameBlock& block,
                                      std::uint32_t width,
                                      std::uint32_t height,
                                      std::vector<mrhiAccess>& writes) {
    enabled_ = frame.occlusion.enabled;
    if (!enabled_) {
        return {};
    }
    // The near plane, where the projection keeps it (reversed-Z, D284).
    block_ = OcclusionBlock{.toPoint = inverseOf(block.viewProjection),
                            .viewProjection = block.viewProjection,
                            .settings = {frame.occlusion.radius, frame.occlusion.intensity, frame.projection[14], 0}};
    // The surfaces at the target's size; the occlusion at half of it,
    // rounded up.
    for (const auto& [kFormat, kHalved, kMade] : {std::tuple{kSurfaceFormat, false, &surfaces_},
                                                  std::tuple{kAmbientFormat, true, &raw_},
                                                  std::tuple{kAmbientFormat, true, &blurred_}}) {
        mrhiTextureDef def = mrhiDefaultTextureDef();
        def.format = kFormat;
        def.width = kHalved ? (width + 1) / 2 : width;
        def.height = kHalved ? (height + 1) / 2 : height;
        if (const mrhiResult kDeclared = mrhiDeclareTexture(native_, &def, kMade); kDeclared != mrhi_success) {
            return failed("the ambient occlusion's targets could not be declared", kDeclared);
        }
    }
    mrhiBufferDef def = mrhiDefaultBufferDef();
    def.size = sizeof(OcclusionBlock);
    if (mrhiDeclareBuffer(native_, &def, &blockResource_) != mrhi_success) {
        return failed("the ambient occlusion's view could not be declared", mrhi_errorCapacity);
    }
    writes.push_back(wholeOf(blockResource_, mrhi_accessCopyDestination));
    return {};
}

bool OcclusionPass::enabled() const noexcept {
    return enabled_;
}

mrhiResourceId OcclusionPass::surfaces() const noexcept {
    return surfaces_;
}

mrhiResourceId OcclusionPass::reaching() const noexcept {
    return blurred_;
}

result::Status OcclusionPass::addPasses(mrhiResourceId depth) {
    if (!enabled_) {
        return {};
    }
    for (const auto& [kTarget, kReads, kMade] :
         {std::tuple{raw_,
                     std::array<mrhiAccess, 3>{depthOf(depth),
                                               wholeOf(surfaces_, mrhi_accessSampled),
                                               wholeOf(blockResource_, mrhi_accessUniform)},
                     &occludePass_},
          std::tuple{blurred_,
                     std::array<mrhiAccess, 3>{depthOf(depth),
                                               wholeOf(raw_, mrhi_accessSampled),
                                               wholeOf(blockResource_, mrhi_accessUniform)},
                     &blurPass_}}) {
        // The blur's table holds the surfaces too, as every slot is bound.
        std::vector<mrhiAccess> reads{kReads.begin(), kReads.end()};
        if (kMade == &blurPass_) {
            reads.push_back(wholeOf(surfaces_, mrhi_accessSampled));
        }
        mrhiPassDef def = mrhiDefaultPassDef();
        def.colorTargets[0].resource = kTarget;
        def.colorTargets[0].load = mrhi_loadDiscard;
        def.colorTargets[0].store = mrhi_storeKeep;
        def.colorTargetCount = 1;
        def.accesses = reads.data();
        def.accessCount = static_cast<std::uint32_t>(reads.size());
        if (const mrhiResult kAdded = mrhiAddPass(native_, &def, kMade); kAdded != mrhi_success) {
            return failed("the ambient occlusion's passes could not be added", kAdded);
        }
    }
    return {};
}

result::Status OcclusionPass::write(mrhiPassId upload) {
    if (enabled_ &&
        mrhiWriteBuffer(native_, upload, blockResource_, 0, &block_, sizeof(OcclusionBlock)) != mrhi_success) {
        return failed("the ambient occlusion's view could not be written", mrhi_errorCapacity);
    }
    return {};
}

result::Status OcclusionPass::record(const Pipelines& pipelines, mrhiResourceId depth) {
    if (!enabled_) {
        return {};
    }
    // Every slot bound in both: the occlusion reads the depth and the
    // surfaces, the blur the depth and the occlusion found.
    for (const auto& [kPass, kPipeline, kFound] : {std::tuple{occludePass_, pipelines.occlude.pipeline, surfaces_},
                                                   std::tuple{blurPass_, pipelines.blurOcclusion.pipeline, raw_}}) {
        const std::array<mrhiBinding, 4> kBindings = {depthAt(0, depth),
                                                      textureAt(1, surfaces_),
                                                      bufferAt(2, blockResource_, sizeof(OcclusionBlock)),
                                                      textureAt(3, kFound)};
        if (mrhiBeginPass(native_, kPass) != mrhi_success ||
            mrhiSetGraphicsPipeline(native_, kPass, kPipeline) != mrhi_success ||
            mrhiSetBindings(native_, kPass, 0, kBindings.data(), kBindings.size()) != mrhi_success ||
            mrhiDraw(native_, kPass, 3, 1, 0, 0) != mrhi_success || mrhiEndPass(native_, kPass) != mrhi_success) {
            return failed("the ambient occlusion could not be drawn", mrhi_errorState);
        }
    }
    return {};
}

} // namespace rawframe::render_scene_gpu
