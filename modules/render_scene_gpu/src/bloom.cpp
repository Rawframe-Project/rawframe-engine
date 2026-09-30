#include "bloom.h"

#include "tables.h"

#include <array>
#include <maul-rhi/encoder.h>

namespace rawframe::render_scene_gpu {

namespace {

/// The most levels the chain halves into.
constexpr std::size_t kMostLevels = 6;

mrhiAccess wholeOf(mrhiResourceId resource, mrhiAccessKind kind) noexcept {
    return mrhiAccess{
        .resource = resource,
        .kind = kind,
        .range = {.baseMip = 0, .mipCount = MRHI_REMAINING, .baseLayer = 0, .layerCount = 1, .aspect = {}}};
}

} // namespace

BloomPass::BloomPass(mrhiDevice* native) noexcept : native_(native) {
}

result::Status
BloomPass::declare(const render_scene::SceneFrame& frame, bool made, std::uint32_t width, std::uint32_t height) {
    levels_.clear();
    down_.clear();
    up_.clear();
    enabled_ = frame.bloom.enabled && made;
    if (!enabled_) {
        return {};
    }
    // The first level a quarter of the target's sides, then halved while
    // both sides stay at least two texels.
    std::uint32_t across = (width + 1) / 2;
    std::uint32_t down = (height + 1) / 2;
    while (levels_.size() < kMostLevels && across >= 4 && down >= 4) {
        across = (across + 1) / 2;
        down = (down + 1) / 2;
        mrhiTextureDef def = mrhiDefaultTextureDef();
        def.format = kSceneFormat;
        def.width = across;
        def.height = down;
        mrhiResourceId level{};
        if (const mrhiResult kDeclared = mrhiDeclareTexture(native_, &def, &level); kDeclared != mrhi_success) {
            return failed("the bloom's levels could not be declared", kDeclared);
        }
        levels_.push_back(level);
    }
    enabled_ = !levels_.empty();
    return {};
}

bool BloomPass::enabled() const noexcept {
    return enabled_;
}

mrhiResourceId BloomPass::spread() const noexcept {
    return levels_.empty() ? mrhiResourceId{} : levels_.front();
}

std::size_t BloomPass::levels() const noexcept {
    return levels_.size();
}

result::Status BloomPass::addPasses(mrhiResourceId scene) {
    if (!enabled_) {
        return {};
    }
    const auto kAdd = [this](mrhiResourceId from,
                             mrhiResourceId into,
                             bool adding,
                             std::vector<mrhiPassId>& passes) -> result::Status {
        const mrhiAccess kRead = wholeOf(from, mrhi_accessSampled);
        mrhiPassDef def = mrhiDefaultPassDef();
        def.colorTargets[0].resource = into;
        def.colorTargets[0].load = adding ? mrhi_loadKeep : mrhi_loadDiscard;
        def.colorTargets[0].store = mrhi_storeKeep;
        def.colorTargetCount = 1;
        def.accesses = &kRead;
        def.accessCount = 1;
        mrhiPassId made{};
        if (const mrhiResult kAdded = mrhiAddPass(native_, &def, &made); kAdded != mrhi_success) {
            return failed("the bloom's passes could not be added", kAdded);
        }
        passes.push_back(made);
        return {};
    };
    for (std::size_t at = 0; at < levels_.size(); ++at) {
        RAWFRAME_TRY(kAdd(at == 0 ? scene : levels_[at - 1], levels_[at], false, down_));
    }
    for (std::size_t at = levels_.size() - 1; at > 0; --at) {
        RAWFRAME_TRY(kAdd(levels_[at], levels_[at - 1], true, up_));
    }
    return {};
}

result::Status BloomPass::record(const Pipelines& pipelines, mrhiResourceId scene) {
    if (!enabled_) {
        return {};
    }
    const auto kDraw = [this,
                        &pipelines](mrhiPassId pass, const Asked& pipeline, mrhiResourceId from) -> result::Status {
        const std::array<mrhiBinding, 2> kBindings = {textureAt(0, from), samplerAt(1, pipelines.filteredSampler)};
        if (mrhiBeginPass(native_, pass) != mrhi_success ||
            mrhiSetGraphicsPipeline(native_, pass, pipeline.pipeline) != mrhi_success ||
            mrhiSetBindings(native_, pass, 0, kBindings.data(), kBindings.size()) != mrhi_success ||
            mrhiDraw(native_, pass, 3, 1, 0, 0) != mrhi_success || mrhiEndPass(native_, pass) != mrhi_success) {
            return failed("the bloom could not be drawn", mrhi_errorState);
        }
        return {};
    };
    for (std::size_t at = 0; at < down_.size(); ++at) {
        RAWFRAME_TRY(
            kDraw(down_[at], at == 0 ? pipelines.bloomFirst : pipelines.bloomDown, at == 0 ? scene : levels_[at - 1]));
    }
    for (std::size_t at = 0; at < up_.size(); ++at) {
        RAWFRAME_TRY(kDraw(up_[at], pipelines.bloomUp, levels_[levels_.size() - 1 - at]));
    }
    return {};
}

} // namespace rawframe::render_scene_gpu
