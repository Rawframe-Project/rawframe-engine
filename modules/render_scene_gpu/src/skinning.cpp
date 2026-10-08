#include "skinning.h"

#include "blocks.h"
#include "tables.h"

#include <maul-rhi/encoder.h>

namespace rawframe::render_scene_gpu {

namespace {

/// Each model's block apart, as far as any device asks a uniform binding's
/// offset to be aligned.
constexpr std::uint32_t kBlockBytes = 256;
constexpr std::uint32_t kBlockWords = kBlockBytes / 4;
/// The vertices one workgroup skins, as the shader's entry says.
constexpr std::uint32_t kGroup = 64;

mrhiAccess wholeOf(mrhiResourceId resource, mrhiAccessKind kind) noexcept {
    return mrhiAccess{
        .resource = resource,
        .kind = kind,
        .range = {.baseMip = 0, .mipCount = MRHI_REMAINING, .baseLayer = 0, .layerCount = 1, .aspect = {}}};
}

} // namespace

DeviceSkinning::DeviceSkinning(mrhiDevice* native) noexcept : native_(native) {
}

result::Status DeviceSkinning::declare(const render_scene::SceneFrame& frame,
                                       const std::map<std::uint64_t, const HeldMesh*>& usable,
                                       bool ready,
                                       std::vector<mrhiAccess>& writes) {
    jobs_.clear();
    found_.clear();
    palette_.clear();
    blocks_.clear();
    vertices_ = 0;
    if (!ready || frame.palette.empty()) {
        return {};
    }
    // Each palette a draw is posed with, and for the models drawn, not the
    // casters, the one it was posed with the frame before.
    const auto kSkin = [&](const render_scene::SceneDraw& draw, std::uint32_t palette) {
        const auto kMesh = usable.find(draw.mesh);
        if (draw.joints == 0 || kMesh == usable.end() || kMesh->second->influences.index1 == 0 ||
            palette + std::uint64_t{draw.joints} > frame.palette.size() ||
            draw.joints != kMesh->second->source->skin.joints.size() || found_.contains({kMesh->second, palette})) {
            return;
        }
        found_.emplace(std::pair{kMesh->second, palette}, jobs_.size());
        jobs_.push_back({.mesh = kMesh->second, .palette = palette, .first = static_cast<std::uint32_t>(vertices_)});
        vertices_ += kMesh->second->source->positions.size();
    };
    for (const render_scene::SceneDraw& draw : frame.draws) {
        kSkin(draw, draw.palette);
        kSkin(draw, draw.previousPalette);
    }
    for (const std::vector<render_scene::SceneDraw>* kList : {&frame.shadows.casters, &frame.lightShadows.casters}) {
        for (const render_scene::SceneDraw& draw : *kList) {
            kSkin(draw, draw.palette);
        }
    }
    if (jobs_.empty()) {
        return {};
    }
    palette_.reserve(frame.palette.size() * 16);
    for (const render_scene::Matrix& kMatrix : frame.palette) {
        palette_.insert(palette_.end(), kMatrix.begin(), kMatrix.end());
    }
    blocks_.assign(jobs_.size() * kBlockWords, 0);
    for (std::size_t job = 0; job < jobs_.size(); ++job) {
        blocks_[job * kBlockWords] = static_cast<std::uint32_t>(jobs_[job].mesh->source->positions.size());
        blocks_[(job * kBlockWords) + 1] = jobs_[job].palette;
        blocks_[(job * kBlockWords) + 2] = jobs_[job].first;
    }
    for (const auto& [kBytes, kMade] : {std::pair{std::uint64_t{palette_.size()} * 4, &paletteResource_},
                                        std::pair{std::uint64_t{blocks_.size()} * 4, &blocksResource_},
                                        std::pair{vertices_ * kVertexBytes, &posedResource_}}) {
        mrhiBufferDef def = mrhiDefaultBufferDef();
        def.size = kBytes;
        if (mrhiDeclareBuffer(native_, &def, kMade) != mrhi_success) {
            return failed("the skinned models could not be declared", mrhi_errorCapacity);
        }
    }
    writes.push_back(wholeOf(paletteResource_, mrhi_accessCopyDestination));
    writes.push_back(wholeOf(blocksResource_, mrhi_accessCopyDestination));
    return {};
}

result::Status DeviceSkinning::addPass(const DeviceMeshes& meshes, std::vector<mrhiAccess>& reads) {
    if (jobs_.empty()) {
        return {};
    }
    std::vector<mrhiAccess> accesses = {wholeOf(paletteResource_, mrhi_accessStorageRead),
                                        wholeOf(blocksResource_, mrhi_accessUniform),
                                        wholeOf(posedResource_, mrhi_accessStorageWrite)};
    std::map<const HeldMesh*, bool> named;
    for (const Job& kJob : jobs_) {
        if (named.emplace(kJob.mesh, true).second) {
            const FrameMesh& kMade = meshes.frameMeshOf(*kJob.mesh);
            accesses.push_back(wholeOf(kMade.vertices, mrhi_accessStorageRead));
            accesses.push_back(wholeOf(kMade.influences, mrhi_accessStorageRead));
        }
    }
    mrhiPassDef def = mrhiDefaultPassDef();
    def.accesses = accesses.data();
    def.accessCount = static_cast<std::uint32_t>(accesses.size());
    if (const mrhiResult kAdded = mrhiAddPass(native_, &def, &pass_); kAdded != mrhi_success) {
        return failed("the skinning pass could not be added", kAdded);
    }
    reads.push_back(wholeOf(posedResource_, mrhi_accessVertex));
    return {};
}

result::Status DeviceSkinning::write(mrhiPassId upload) const {
    if (jobs_.empty()) {
        return {};
    }
    if (mrhiWriteBuffer(native_, upload, paletteResource_, 0, palette_.data(), palette_.size() * 4) != mrhi_success ||
        mrhiWriteBuffer(native_, upload, blocksResource_, 0, blocks_.data(), blocks_.size() * 4) != mrhi_success) {
        return failed("the skinned models could not be written", mrhi_errorCapacity);
    }
    return {};
}

result::Status DeviceSkinning::record(const Pipelines& pipelines, const DeviceMeshes& meshes) const {
    if (jobs_.empty()) {
        return {};
    }
    if (mrhiBeginPass(native_, pass_) != mrhi_success ||
        mrhiSetComputePipeline(native_, pass_, pipelines.skin.compute) != mrhi_success) {
        return failed("the skinning pass could not be recorded", mrhi_errorState);
    }
    for (std::size_t job = 0; job < jobs_.size(); ++job) {
        const Job& kJob = jobs_[job];
        const FrameMesh& kMade = meshes.frameMeshOf(*kJob.mesh);
        const std::uint64_t kVertices = kJob.mesh->source->positions.size();
        mrhiBinding block = bufferAt(4, blocksResource_, kBlockBytes);
        block.offset = std::uint64_t{job} * kBlockBytes;
        const std::array<mrhiBinding, 5> kBindings = {bufferAt(0, kMade.vertices, kVertices * kVertexBytes),
                                                      bufferAt(1, kMade.influences, kVertices * kInfluenceWords * 4),
                                                      bufferAt(2, paletteResource_, palette_.size() * 4),
                                                      bufferAt(3, posedResource_, vertices_ * kVertexBytes),
                                                      block};
        if (mrhiSetBindings(native_, pass_, 0, kBindings.data(), kBindings.size()) != mrhi_success ||
            mrhiDispatch(native_, pass_, static_cast<std::uint32_t>((kVertices + kGroup - 1) / kGroup), 1, 1) !=
                mrhi_success) {
            return failed("the skinning pass could not be recorded", mrhi_errorState);
        }
    }
    if (mrhiEndPass(native_, pass_) != mrhi_success) {
        return failed("the skinning pass could not be recorded", mrhi_errorState);
    }
    return {};
}

std::optional<VerticesAt> DeviceSkinning::posedOf(const HeldMesh* mesh, std::uint32_t palette) const noexcept {
    const auto kFound = found_.find({mesh, palette});
    if (kFound == found_.end()) {
        return std::nullopt;
    }
    return VerticesAt{.buffer = posedResource_, .offset = std::uint64_t{jobs_[kFound->second].first} * kVertexBytes};
}

} // namespace rawframe::render_scene_gpu
