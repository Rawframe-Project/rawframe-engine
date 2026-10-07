#include "casting.h"

#include "tables.h"

#include <algorithm>
#include <array>
#include <maul-rhi/encoder.h>
#include <optional>

namespace rawframe::render_scene_gpu {

result::Status cast(const Casting& with, mrhiPassId pass, std::span<const Square> squares) {
    mrhiDevice* native = with.native;
    const Pipelines& pipelines = *with.pipelines;
    if (mrhiBeginPass(native, pass) != mrhi_success) {
        return failed("a shadow pass could not begin", mrhi_errorState);
    }
    const auto kDraw = [&with, native, pass](const Run& run) -> result::Status {
        RAWFRAME_TRY(with.held->bind(pass, *run.mesh));
        if (mrhiDrawIndexed(native, pass, run.indexCount, run.count, run.firstIndex, 0, run.first) != mrhi_success) {
            return failed("a caster could not be drawn", mrhi_errorState);
        }
        return {};
    };
    for (const Square& kSquare : squares) {
        if (kSquare.casters->empty()) {
            continue;
        }
        // The cascade's square, then the material's bindings, as the
        // material module puts them (D488).
        const mrhiResourceId kWhite = resourceOf(with.textures->resource(0));
        std::array<mrhiBinding, 10> binding = {bufferAt(0, kSquare.view, sizeof(Matrix4)),
                                               bufferAt(9, with.materials, with.materialsBytes),
                                               textureAt(10, kWhite),
                                               samplerAt(11, pipelines.materialSamplers[0]),
                                               textureAt(12, kWhite),
                                               samplerAt(13, pipelines.materialSamplers[0]),
                                               textureAt(14, kWhite),
                                               samplerAt(15, pipelines.materialSamplers[0]),
                                               textureAt(16, kWhite),
                                               samplerAt(17, pipelines.materialSamplers[0])};
        if (mrhiSetViewport(native, pass, &kSquare.viewport) != mrhi_success) {
            return failed("a shadow square could not be set up", mrhi_errorState);
        }
        if (!kSquare.casters->solid.empty()) {
            if (mrhiSetGraphicsPipeline(native, pass, pipelines.casting.pipeline) != mrhi_success ||
                mrhiSetVertexBuffer(native, pass, 1, with.instances, 0, MRHI_WHOLE_SIZE) != mrhi_success ||
                mrhiSetBindings(native, pass, 0, binding.data(), binding.size()) != mrhi_success) {
                return failed("the casters could not be set up", mrhi_errorState);
            }
            for (const Run& run : kSquare.casters->solid) {
                RAWFRAME_TRY(kDraw(run));
            }
        }
        if (kSquare.casters->masked.empty()) {
            continue;
        }
        mrhiGraphicsPipelineId set = pipelines.cutCasting.pipeline;
        if (mrhiSetGraphicsPipeline(native, pass, set) != mrhi_success ||
            mrhiSetVertexBuffer(native, pass, 1, with.instances, 0, MRHI_WHOLE_SIZE) != mrhi_success) {
            return failed("the masked casters could not be set up", mrhi_errorState);
        }
        // A caster whose material has its own program is cut by it, once
        // its pipeline is made (D488); the table set again after a switch.
        std::optional<render_scene::SceneTextures> bound;
        for (const Run& run : kSquare.casters->masked) {
            mrhiGraphicsPipelineId wanted = pipelines.cutCasting.pipeline;
            if (const Asked* kOwn = run.program != nullptr
                                        ? pipelines.programPipeline(run.program, Shade::CutCasting, false, false)
                                        : nullptr;
                kOwn != nullptr) {
                wanted = kOwn->pipeline;
            }
            if (wanted.index1 != set.index1 || wanted.generation != set.generation) {
                if (mrhiSetGraphicsPipeline(native, pass, wanted) != mrhi_success) {
                    return failed("a material's program could not be set", mrhi_errorState);
                }
                set = wanted;
                bound.reset();
            }
            if (bound != run.texture) {
                bindTexture(*with.textures, pipelines, binding[2], binding[3], run.texture.base);
                bindTexture(*with.textures, pipelines, binding[4], binding[5], run.texture.packed);
                bindTexture(*with.textures, pipelines, binding[6], binding[7], run.texture.emission);
                bindTexture(*with.textures, pipelines, binding[8], binding[9], run.texture.normal);
                if (mrhiSetBindings(native, pass, 0, binding.data(), binding.size()) != mrhi_success) {
                    return failed("a masked caster's texture could not be bound", mrhi_errorState);
                }
                bound = run.texture;
            }
            RAWFRAME_TRY(kDraw(run));
        }
    }
    if (mrhiEndPass(native, pass) != mrhi_success) {
        return failed("a shadow pass could not end", mrhi_errorState);
    }
    return {};
}

namespace {

mrhiAccess wholeOf(mrhiResourceId resource, mrhiAccessKind kind) noexcept {
    return mrhiAccess{
        .resource = resource,
        .kind = kind,
        .range = {
            .baseMip = 0, .mipCount = MRHI_REMAINING, .baseLayer = 0, .layerCount = MRHI_REMAINING, .aspect = {}}};
}

mrhiAccess depthOf(mrhiResourceId resource) noexcept {
    return mrhiAccess{
        .resource = resource,
        .kind = mrhi_accessSampled,
        .range = {
            .baseMip = 0, .mipCount = MRHI_REMAINING, .baseLayer = 0, .layerCount = 1, .aspect = mrhi_aspectDepthOnly}};
}

} // namespace

ShadowPasses::ShadowPasses(mrhiDevice* native) noexcept : native_(native) {
}

result::Status
ShadowPasses::declare(const render_scene::SceneFrame& frame, const FrameBlock& block, std::vector<mrhiAccess>& writes) {
    // The shadow map: the cascades' squares two by two, or a texel
    // nothing reads the depth of when there are none.
    cascadeCount_ = frame.shadows.count;
    side_ = cascadeCount_ > 0 ? frame.shadows.side : 1;
    cascadeMatrices_ = block.cascades;
    mrhiTextureDef sunDef = mrhiDefaultTextureDef();
    sunDef.format = kShadowFormat;
    sunDef.width = cascadeCount_ > 0 ? 2 * side_ : 1;
    sunDef.height = sunDef.width;
    if (const mrhiResult kDeclared = mrhiDeclareTexture(native_, &sunDef, &sunMap_); kDeclared != mrhi_success) {
        return failed("the shadow map could not be declared", kDeclared);
    }
    for (std::size_t at = 0; at < cascadeCount_; ++at) {
        mrhiBufferDef def = mrhiDefaultBufferDef();
        def.size = sizeof(Matrix4);
        if (mrhiDeclareBuffer(native_, &def, &cascades_[at]) != mrhi_success) {
            return failed("a cascade's view could not be declared", mrhi_errorCapacity);
        }
        writes.push_back(wholeOf(cascades_[at], mrhi_accessCopyDestination));
    }
    // The punctual lights' atlas: their squares, or a texel nothing reads
    // the depth of when there are none; each square's view.
    const render_scene::SceneLightShadows& kLightShadows = frame.lightShadows;
    slots_ = slotsOf(frame);
    slotMatrices_.clear();
    slotViews_.clear();
    slotViewports_.clear();
    mrhiTextureDef atlasDef = mrhiDefaultTextureDef();
    atlasDef.format = kShadowFormat;
    atlasDef.width = std::max<std::uint32_t>(kLightShadows.side, 1);
    atlasDef.height = atlasDef.width;
    if (const mrhiResult kDeclared = mrhiDeclareTexture(native_, &atlasDef, &lightMap_); kDeclared != mrhi_success) {
        return failed("the lights' shadow atlas could not be declared", kDeclared);
    }
    mrhiBufferDef slotsDef = mrhiDefaultBufferDef();
    slotsDef.size = squareBytes();
    if (mrhiDeclareBuffer(native_, &slotsDef, &slotsResource_) != mrhi_success) {
        return failed("the lights' shadow squares could not be declared", mrhi_errorCapacity);
    }
    writes.push_back(wholeOf(slotsResource_, mrhi_accessCopyDestination));
    for (const render_scene::ShadowSlot& slot : kLightShadows.slots) {
        mrhiBufferDef def = mrhiDefaultBufferDef();
        def.size = sizeof(Matrix4);
        if (mrhiDeclareBuffer(native_, &def, &slotViews_.emplace_back()) != mrhi_success) {
            return failed("a shadow square's view could not be declared", mrhi_errorCapacity);
        }
        writes.push_back(wholeOf(slotViews_.back(), mrhi_accessCopyDestination));
        slotMatrices_.push_back(slot.viewProjection);
        slotViewports_.push_back({.x = static_cast<float>(slot.x),
                                  .y = static_cast<float>(slot.y),
                                  .width = static_cast<float>(slot.side),
                                  .height = static_cast<float>(slot.side),
                                  .minDepth = 0,
                                  .maxDepth = 1});
    }
    return {};
}

result::Status ShadowPasses::addPasses(const std::vector<mrhiAccess>& reads) {
    std::vector<mrhiAccess> sunReads = reads;
    for (std::size_t at = 0; at < cascadeCount_; ++at) {
        sunReads.push_back(wholeOf(cascades_[at], mrhi_accessUniform));
    }
    mrhiPassDef sunDef = mrhiDefaultPassDef();
    sunDef.accesses = sunReads.data();
    sunDef.accessCount = static_cast<std::uint32_t>(sunReads.size());
    sunDef.depthTarget = mrhiDepthTarget{.resource = sunMap_,
                                         .mip = 0,
                                         .layer = 0,
                                         .depthLoad = mrhi_loadClear,
                                         .depthStore = mrhi_storeKeep,
                                         .clearDepth = 0,
                                         .stencilLoad = mrhi_loadDiscard,
                                         .stencilStore = mrhi_storeDiscard,
                                         .clearStencil = 0,
                                         .readOnly = false};
    if (const mrhiResult kAdded = mrhiAddPass(native_, &sunDef, &sunPass_); kAdded != mrhi_success) {
        return failed("the shadow pass could not be added", kAdded);
    }
    std::vector<mrhiAccess> slotReads = reads;
    for (const mrhiResourceId kView : slotViews_) {
        slotReads.push_back(wholeOf(kView, mrhi_accessUniform));
    }
    mrhiPassDef lightDef = sunDef;
    lightDef.accesses = slotReads.data();
    lightDef.accessCount = static_cast<std::uint32_t>(slotReads.size());
    lightDef.depthTarget.resource = lightMap_;
    if (const mrhiResult kAdded = mrhiAddPass(native_, &lightDef, &lightPass_); kAdded != mrhi_success) {
        return failed("the lights' shadow pass could not be added", kAdded);
    }
    return {};
}

result::Status ShadowPasses::write(mrhiPassId upload) {
    for (std::size_t at = 0; at < cascadeCount_; ++at) {
        if (mrhiWriteBuffer(native_, upload, cascades_[at], 0, &cascadeMatrices_[at], sizeof(Matrix4)) !=
            mrhi_success) {
            return failed("a cascade's view could not be written", mrhi_errorCapacity);
        }
    }
    if (mrhiWriteBuffer(native_, upload, slotsResource_, 0, slots_.data(), squareBytes()) != mrhi_success) {
        return failed("the lights' shadow squares could not be written", mrhi_errorCapacity);
    }
    for (std::size_t at = 0; at < slotViews_.size(); ++at) {
        if (mrhiWriteBuffer(native_, upload, slotViews_[at], 0, &slotMatrices_[at], sizeof(Matrix4)) != mrhi_success) {
            return failed("a shadow square's view could not be written", mrhi_errorCapacity);
        }
    }
    return {};
}

result::Status ShadowPasses::record(const Casting& with, const Placed& placed) {
    // The sun's cascades, each its square of the map two by two; and the
    // punctual lights' squares of their atlas.
    std::vector<Square> squares;
    const auto kSide = static_cast<float>(side_);
    for (std::size_t at = 0; at < cascadeCount_; ++at) {
        squares.push_back({.view = cascades_[at],
                           .viewport = {.x = static_cast<float>(at % 2) * kSide,
                                        .y = static_cast<float>(at / 2) * kSide,
                                        .width = kSide,
                                        .height = kSide,
                                        .minDepth = 0,
                                        .maxDepth = 1},
                           .casters = &placed.cascadeRuns[at]});
    }
    RAWFRAME_TRY(cast(with, sunPass_, squares));
    squares.clear();
    for (std::size_t at = 0; at < slotViews_.size(); ++at) {
        squares.push_back({.view = slotViews_[at], .viewport = slotViewports_[at], .casters = &placed.slotRuns[at]});
    }
    return cast(with, lightPass_, squares);
}

void ShadowPasses::readBy(std::vector<mrhiAccess>& reads) const {
    reads.push_back(wholeOf(slotsResource_, mrhi_accessStorageRead));
    reads.push_back(depthOf(lightMap_));
    reads.push_back(depthOf(sunMap_));
}

mrhiResourceId ShadowPasses::sunMap() const noexcept {
    return sunMap_;
}

mrhiResourceId ShadowPasses::lightMap() const noexcept {
    return lightMap_;
}

mrhiResourceId ShadowPasses::squares() const noexcept {
    return slotsResource_;
}

std::uint64_t ShadowPasses::squareBytes() const noexcept {
    return slots_.size() * sizeof(SlotBlock);
}

} // namespace rawframe::render_scene_gpu
