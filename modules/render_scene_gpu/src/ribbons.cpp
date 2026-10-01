#include "ribbons.h"

#include "blocks.h"
#include "tables.h"

#include <cstring>
#include <maul-rhi/encoder.h>
#include <utility>

namespace rawframe::render_scene_gpu {

namespace {

mrhiAccess wholeOf(mrhiResourceId resource, mrhiAccessKind kind) noexcept {
    return mrhiAccess{
        .resource = resource,
        .kind = kind,
        .range = {.baseMip = 0, .mipCount = MRHI_REMAINING, .baseLayer = 0, .layerCount = 1, .aspect = {}}};
}

/// The bytes the shader's reflection asks a ribbon's binding to span: the
/// emitters' block it shares slot 2 with.
constexpr std::uint64_t kRibbonBinding = sizeof(EmitterBlock);

} // namespace

RibbonPass::RibbonPass(mrhiDevice* native) noexcept : native_(native) {
}

result::Status RibbonPass::declare(const render_scene::SceneFrame& frame, bool made, std::vector<mrhiAccess>& writes) {
    enabled_ = made && !frame.ribbons.empty();
    points_.clear();
    blocks_.clear();
    textures_.clear();
    counts_.clear();
    if (!enabled_) {
        return {};
    }
    points_.reserve(frame.ribbonPoints.size());
    for (const render_scene::SceneRibbonPoint& kPoint : frame.ribbonPoints) {
        points_.push_back(
            RibbonPointBlock{.placeWidth = {kPoint.place[0], kPoint.place[1], kPoint.place[2], kPoint.width},
                             .color = kPoint.color,
                             .along = {kPoint.along, 0, 0, 0}});
    }
    for (const render_scene::SceneRibbon& kRibbon : frame.ribbons) {
        const std::size_t kAt = blocks_.size();
        blocks_.resize(kAt + kBlockStride);
        const std::array<std::uint32_t, 4> kRange = {kRibbon.first, kRibbon.count, kRibbon.material, 0};
        std::memcpy(blocks_.data() + kAt, kRange.data(), sizeof(kRange));
        textures_.push_back(kRibbon.material < frame.textures.size() ? frame.textures[kRibbon.material]
                                                                     : render_scene::SceneTextures{});
        counts_.push_back(kRibbon.count);
    }
    view_ = viewOf(frame);
    for (const auto& [kBytes, kMade] :
         {std::pair{std::uint64_t{points_.size() * sizeof(RibbonPointBlock)}, &pointsResource_},
          std::pair{std::uint64_t{blocks_.size()}, &blocksResource_},
          std::pair{std::uint64_t{sizeof(ParticleViewBlock)}, &viewResource_}}) {
        mrhiBufferDef def = mrhiDefaultBufferDef();
        def.size = kBytes;
        if (mrhiDeclareBuffer(native_, &def, kMade) != mrhi_success) {
            return failed("the ribbons could not be declared", mrhi_errorCapacity);
        }
        writes.push_back(wholeOf(*kMade, mrhi_accessCopyDestination));
    }
    return {};
}

void RibbonPass::drawReads(std::vector<mrhiAccess>& reads) const {
    if (!enabled_) {
        return;
    }
    reads.push_back(wholeOf(pointsResource_, mrhi_accessStorageRead));
    reads.push_back(wholeOf(blocksResource_, mrhi_accessUniform));
    reads.push_back(wholeOf(viewResource_, mrhi_accessUniform));
}

result::Status RibbonPass::write(mrhiPassId upload) {
    if (!enabled_) {
        return {};
    }
    if (mrhiWriteBuffer(
            native_, upload, pointsResource_, 0, points_.data(), points_.size() * sizeof(RibbonPointBlock)) !=
            mrhi_success ||
        mrhiWriteBuffer(native_, upload, blocksResource_, 0, blocks_.data(), blocks_.size()) != mrhi_success ||
        mrhiWriteBuffer(native_, upload, viewResource_, 0, &view_, sizeof(view_)) != mrhi_success) {
        return failed("the ribbons could not be written", mrhi_errorCapacity);
    }
    return {};
}

result::Status RibbonPass::recordDraws(mrhiPassId pass, const Pipelines& pipelines, const ParticleDrawing& with) {
    if (!enabled_) {
        return {};
    }
    std::array<mrhiBinding, 11> table = {bufferAt(0, with.block, sizeof(FrameBlock)),
                                         bufferAt(1, viewResource_, sizeof(ParticleViewBlock)),
                                         bufferAt(2, blocksResource_, kRibbonBinding),
                                         bufferAt(3, pointsResource_, points_.size() * sizeof(RibbonPointBlock)),
                                         bufferAt(4, with.exposure, sizeof(ExposureBlock)),
                                         bufferAt(5, with.materials, with.materialsBytes),
                                         depthAt(6, with.depth),
                                         textureAt(7, {}),
                                         samplerAt(8, {}),
                                         textureAt(9, {}),
                                         samplerAt(10, {})};
    if (mrhiSetGraphicsPipeline(native_, pass, pipelines.ribbons.pipeline) != mrhi_success) {
        return failed("the ribbons could not begin", mrhi_errorState);
    }
    // Each ribbon, two triangles between each point and the next.
    for (std::size_t at = 0; at < counts_.size(); ++at) {
        table[2].offset = at * kBlockStride;
        bindTexture(*with.textures, pipelines, table[7], table[8], textures_[at].base);
        bindTexture(*with.textures, pipelines, table[9], table[10], textures_[at].emission);
        if (mrhiSetBindings(native_, pass, 0, table.data(), table.size()) != mrhi_success ||
            mrhiDraw(native_, pass, (counts_[at] - 1) * 6, 1, 0, 0) != mrhi_success) {
            return failed("a ribbon could not be drawn", mrhi_errorState);
        }
    }
    return {};
}

bool RibbonPass::enabled() const noexcept {
    return enabled_;
}

std::size_t RibbonPass::drawn() const noexcept {
    return enabled_ ? counts_.size() : 0;
}

} // namespace rawframe::render_scene_gpu
