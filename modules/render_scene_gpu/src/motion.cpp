#include "motion.h"

#include "tables.h"

#include <algorithm>
#include <maul-rhi/encoder.h>
#include <tuple>

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

MotionBlurPass::MotionBlurPass(mrhiDevice* native) noexcept : native_(native) {
}

result::Status MotionBlurPass::declare(const render_scene::SceneFrame& frame,
                                       bool made,
                                       std::uint32_t width,
                                       std::uint32_t height,
                                       std::vector<mrhiAccess>& writes) {
    enabled_ = frame.motionBlur.enabled && made;
    if (!enabled_) {
        return {};
    }
    // The longest blur reaches a thirty-second of the target's height
    // either way, whatever its size; a tile is as wide.
    const std::uint32_t kSide = std::max<std::uint32_t>(4, (height + 16) / 32);
    // The near plane, where the projection keeps it (reversed-Z, D284).
    block_ =
        MotionBlock{.settings = {frame.motionBlur.shutter * 0.5F, static_cast<float>(kSide), frame.projection[14], 0}};
    for (const auto& [kFormat, kWidth, kHeight, kMade] :
         {std::tuple{kMotionFormat, (width + kSide - 1) / kSide, (height + kSide - 1) / kSide, &tiles_},
          std::tuple{kMotionFormat, (width + kSide - 1) / kSide, (height + kSide - 1) / kSide, &neighbors_},
          std::tuple{kSceneFormat, width, height, &blurred_}}) {
        mrhiTextureDef def = mrhiDefaultTextureDef();
        def.format = kFormat;
        def.width = kWidth;
        def.height = kHeight;
        if (const mrhiResult kDeclared = mrhiDeclareTexture(native_, &def, kMade); kDeclared != mrhi_success) {
            return failed("the motion blur's targets could not be declared", kDeclared);
        }
    }
    mrhiBufferDef def = mrhiDefaultBufferDef();
    def.size = sizeof(MotionBlock);
    if (mrhiDeclareBuffer(native_, &def, &blockResource_) != mrhi_success) {
        return failed("the motion blur's settings could not be declared", mrhi_errorCapacity);
    }
    writes.push_back(wholeOf(blockResource_, mrhi_accessCopyDestination));
    return {};
}

bool MotionBlurPass::enabled() const noexcept {
    return enabled_;
}

mrhiResourceId MotionBlurPass::shown(mrhiResourceId shown) const noexcept {
    return enabled_ ? blurred_ : shown;
}

result::Status MotionBlurPass::addPasses(mrhiResourceId shown, mrhiResourceId motion, mrhiResourceId depth) {
    if (!enabled_) {
        return {};
    }
    light_ = shown;
    motion_ = motion;
    depth_ = depth;
    // Every pass's table holds all five slots, so each reads them all: the
    // tiles' pass reads the motion in the tiles' slot.
    for (const auto& [kTarget, kFound, kMade] : {std::tuple{tiles_, mrhiResourceId{}, &tilePass_},
                                                 std::tuple{neighbors_, tiles_, &neighborPass_},
                                                 std::tuple{blurred_, neighbors_, &gatherPass_}}) {
        std::vector<mrhiAccess> reads = {wholeOf(light_, mrhi_accessSampled),
                                         wholeOf(motion_, mrhi_accessSampled),
                                         depthOf(depth_),
                                         wholeOf(blockResource_, mrhi_accessUniform)};
        if (kFound.index1 != 0) {
            reads.push_back(wholeOf(kFound, mrhi_accessSampled));
        }
        mrhiPassDef def = mrhiDefaultPassDef();
        def.colorTargets[0].resource = kTarget;
        def.colorTargets[0].load = mrhi_loadDiscard;
        def.colorTargets[0].store = mrhi_storeKeep;
        def.colorTargetCount = 1;
        def.accesses = reads.data();
        def.accessCount = static_cast<std::uint32_t>(reads.size());
        if (const mrhiResult kAdded = mrhiAddPass(native_, &def, kMade); kAdded != mrhi_success) {
            return failed("the motion blur's passes could not be added", kAdded);
        }
    }
    return {};
}

result::Status MotionBlurPass::write(mrhiPassId upload) {
    if (enabled_ && mrhiWriteBuffer(native_, upload, blockResource_, 0, &block_, sizeof(MotionBlock)) != mrhi_success) {
        return failed("the motion blur's settings could not be written", mrhi_errorCapacity);
    }
    return {};
}

result::Status MotionBlurPass::record(const Pipelines& pipelines) {
    if (!enabled_) {
        return {};
    }
    for (const auto& [kPass, kPipeline, kFound] :
         {std::tuple{tilePass_, pipelines.motionTiles.pipeline, motion_},
          std::tuple{neighborPass_, pipelines.motionNeighbors.pipeline, tiles_},
          std::tuple{gatherPass_, pipelines.motionGather.pipeline, neighbors_}}) {
        const std::array<mrhiBinding, 5> kBindings = {textureAt(0, light_),
                                                      textureAt(1, motion_),
                                                      depthAt(2, depth_),
                                                      textureAt(3, kFound),
                                                      bufferAt(4, blockResource_, sizeof(MotionBlock))};
        if (mrhiBeginPass(native_, kPass) != mrhi_success ||
            mrhiSetGraphicsPipeline(native_, kPass, kPipeline) != mrhi_success ||
            mrhiSetBindings(native_, kPass, 0, kBindings.data(), kBindings.size()) != mrhi_success ||
            mrhiDraw(native_, kPass, 3, 1, 0, 0) != mrhi_success || mrhiEndPass(native_, kPass) != mrhi_success) {
            return failed("the motion blur could not be drawn", mrhi_errorState);
        }
    }
    return {};
}

} // namespace rawframe::render_scene_gpu
