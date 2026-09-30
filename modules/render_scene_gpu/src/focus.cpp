#include "focus.h"

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

/// A full-frame sensor's height, in millimeters.
constexpr float kSensorHeight = 24;

} // namespace

DepthOfFieldPass::DepthOfFieldPass(mrhiDevice* native) noexcept : native_(native) {
}

result::Status DepthOfFieldPass::declare(const render_scene::SceneFrame& frame,
                                         std::uint32_t width,
                                         std::uint32_t height,
                                         std::vector<mrhiAccess>& writes) {
    enabled_ = frame.depthOfField.enabled;
    if (!enabled_) {
        return {};
    }
    // The lens whose field is the view's on the sensor: its focal length
    // from the projection's vertical scale, in millimeters. A point `d`
    // away blurs into a circle of diameter f² (d - s) / (N d (s - f)) on
    // the sensor, focused at `s` through f-number `N`; its radius in
    // pixels is that over twice the sensor's height, times the target's.
    const float kFocal = kSensorHeight * 0.5F * frame.projection[5];
    const float kFocus = frame.depthOfField.focus * 1000;
    const float kFar = kFocal * kFocal / (frame.depthOfField.aperture * std::max(kFocus - kFocal, 1.0F)) *
                       static_cast<float>(height) / (2 * kSensorHeight);
    // The longest blur reaches a fortieth of the target's height.
    const float kLongest = std::max(4.0F, static_cast<float>(height) / 40);
    // The near plane, where the projection keeps it (reversed-Z, D284).
    block_ = LensBlock{.settings = {kFar, frame.depthOfField.focus, frame.projection[14], kLongest}};
    for (const auto& [kWidth, kHeight, kMade] : {std::tuple{(width + 1) / 2, (height + 1) / 2, &halved_},
                                                 std::tuple{(width + 1) / 2, (height + 1) / 2, &bokeh_},
                                                 std::tuple{width, height, &focused_}}) {
        mrhiTextureDef def = mrhiDefaultTextureDef();
        def.format = kSceneFormat;
        def.width = kWidth;
        def.height = kHeight;
        if (const mrhiResult kDeclared = mrhiDeclareTexture(native_, &def, kMade); kDeclared != mrhi_success) {
            return failed("the depth of field's targets could not be declared", kDeclared);
        }
    }
    mrhiBufferDef def = mrhiDefaultBufferDef();
    def.size = sizeof(LensBlock);
    if (mrhiDeclareBuffer(native_, &def, &blockResource_) != mrhi_success) {
        return failed("the depth of field's lens could not be declared", mrhi_errorCapacity);
    }
    writes.push_back(wholeOf(blockResource_, mrhi_accessCopyDestination));
    return {};
}

bool DepthOfFieldPass::enabled() const noexcept {
    return enabled_;
}

mrhiResourceId DepthOfFieldPass::shown(mrhiResourceId shown) const noexcept {
    return enabled_ ? focused_ : shown;
}

result::Status DepthOfFieldPass::addPasses(mrhiResourceId shown, mrhiResourceId depth) {
    if (!enabled_) {
        return {};
    }
    light_ = shown;
    depth_ = depth;
    // Every pass's table holds all six slots, so each reads them all: the
    // prefilter reads the light in the halved light's slot.
    for (const auto& [kTarget, kHalved, kMade] : {std::tuple{halved_, mrhiResourceId{}, &prefilterPass_},
                                                  std::tuple{bokeh_, halved_, &bokehPass_},
                                                  std::tuple{focused_, bokeh_, &combinePass_}}) {
        std::vector<mrhiAccess> reads = {
            wholeOf(light_, mrhi_accessSampled), depthOf(depth_), wholeOf(blockResource_, mrhi_accessUniform)};
        if (kHalved.index1 != 0) {
            reads.push_back(wholeOf(kHalved, mrhi_accessSampled));
        }
        // The blend reads the halved light's circles too.
        if (kMade == &combinePass_) {
            reads.push_back(wholeOf(halved_, mrhi_accessSampled));
        }
        mrhiPassDef def = mrhiDefaultPassDef();
        def.colorTargets[0].resource = kTarget;
        def.colorTargets[0].load = mrhi_loadDiscard;
        def.colorTargets[0].store = mrhi_storeKeep;
        def.colorTargetCount = 1;
        def.accesses = reads.data();
        def.accessCount = static_cast<std::uint32_t>(reads.size());
        if (const mrhiResult kAdded = mrhiAddPass(native_, &def, kMade); kAdded != mrhi_success) {
            return failed("the depth of field's passes could not be added", kAdded);
        }
    }
    return {};
}

result::Status DepthOfFieldPass::write(mrhiPassId upload) {
    if (enabled_ && mrhiWriteBuffer(native_, upload, blockResource_, 0, &block_, sizeof(LensBlock)) != mrhi_success) {
        return failed("the depth of field's lens could not be written", mrhi_errorCapacity);
    }
    return {};
}

result::Status DepthOfFieldPass::record(const Pipelines& pipelines) {
    if (!enabled_) {
        return {};
    }
    // Slot 2 is what each pass halves or gathers from, slot 5 the halved
    // light's circles, which only the blend reads.
    for (const auto& [kPass, kPipeline, kHalved, kCircles] :
         {std::tuple{prefilterPass_, pipelines.focusPrefilter.pipeline, light_, light_},
          std::tuple{bokehPass_, pipelines.focusBokeh.pipeline, halved_, halved_},
          std::tuple{combinePass_, pipelines.focusCombine.pipeline, bokeh_, halved_}}) {
        const std::array<mrhiBinding, 6> kBindings = {textureAt(0, light_),
                                                      depthAt(1, depth_),
                                                      textureAt(2, kHalved),
                                                      samplerAt(3, pipelines.filteredSampler),
                                                      bufferAt(4, blockResource_, sizeof(LensBlock)),
                                                      textureAt(5, kCircles)};
        if (mrhiBeginPass(native_, kPass) != mrhi_success ||
            mrhiSetGraphicsPipeline(native_, kPass, kPipeline) != mrhi_success ||
            mrhiSetBindings(native_, kPass, 0, kBindings.data(), kBindings.size()) != mrhi_success ||
            mrhiDraw(native_, kPass, 3, 1, 0, 0) != mrhi_success || mrhiEndPass(native_, kPass) != mrhi_success) {
            return failed("the depth of field could not be drawn", mrhi_errorState);
        }
    }
    return {};
}

} // namespace rawframe::render_scene_gpu
