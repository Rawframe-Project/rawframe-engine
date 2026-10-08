#include "meshes.h"

#include "blocks.h"
#include "pipelines.h"

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

DeviceMeshes::DeviceMeshes(mrhiDevice* native, std::size_t maximum) noexcept : native_(native), maximum_(maximum) {
}

DeviceMeshes::~DeviceMeshes() {
    // Maul RHI retires what a frame still uses once the frame is done.
    for (auto& [id, made] : held_) {
        static_cast<void>(mrhiDestroyBuffer(native_, made.vertices));
        static_cast<void>(mrhiDestroyBuffer(native_, made.indices));
        if (made.influences.index1 != 0) {
            static_cast<void>(mrhiDestroyBuffer(native_, made.influences));
        }
    }
}

void DeviceMeshes::begin(std::uint64_t budget) noexcept {
    chosen_.clear();
    uploads_.clear();
    imported_.clear();
    budget_ = budget;
}

const HeldMesh* DeviceMeshes::choose(std::uint64_t id, const MeshSource& given, RendererStatistics& statistics) {
    if (const auto kChosen = chosen_.find(id); kChosen != chosen_.end()) {
        return kChosen->second;
    }
    auto found = held_.find(id);
    if (found == held_.end()) {
        std::shared_ptr<const mesh::Mesh> source = given ? given(id) : nullptr;
        if (source == nullptr || source->positions.empty() || source->indices.empty() || held_.size() >= maximum_) {
            return nullptr;
        }
        // A skinned mesh's vertices are read by skinning too (D508).
        const bool kSkinned = !source->skin.joints.empty();
        mrhiBufferDef vertexDef = mrhiDefaultBufferDef();
        vertexDef.size = std::uint64_t{source->positions.size()} * kVertexBytes;
        vertexDef.usage = mrhi_bufferVertex | mrhi_bufferCopyDestination;
        if (kSkinned) {
            vertexDef.usage |= mrhi_bufferStorage;
        }
        mrhiBufferDef indexDef = mrhiDefaultBufferDef();
        indexDef.size = std::uint64_t{source->indices.size()} * 4;
        indexDef.usage = mrhi_bufferIndex | mrhi_bufferCopyDestination;
        HeldMesh made{.source = std::move(source)};
        if (mrhiCreateBuffer(native_, &vertexDef, &made.vertices) != mrhi_success) {
            return nullptr;
        }
        if (mrhiCreateBuffer(native_, &indexDef, &made.indices) != mrhi_success) {
            static_cast<void>(mrhiDestroyBuffer(native_, made.vertices));
            return nullptr;
        }
        if (kSkinned) {
            mrhiBufferDef influenceDef = mrhiDefaultBufferDef();
            influenceDef.size = std::uint64_t{made.source->positions.size()} * kInfluenceWords * 4;
            influenceDef.usage = mrhi_bufferStorage | mrhi_bufferCopyDestination;
            if (mrhiCreateBuffer(native_, &influenceDef, &made.influences) != mrhi_success) {
                static_cast<void>(mrhiDestroyBuffer(native_, made.vertices));
                static_cast<void>(mrhiDestroyBuffer(native_, made.indices));
                return nullptr;
            }
        }
        found = held_.emplace(id, std::move(made)).first;
    }
    HeldMesh& mesh = found->second;
    if (!mesh.uploaded) {
        const std::uint64_t kBytes = bytesOf(*mesh.source);
        if (kBytes > budget_) {
            ++statistics.uploadsDeferred;
            return nullptr;
        }
        budget_ -= kBytes;
        uploads_.push_back(&mesh);
    }
    chosen_.emplace(id, &mesh);
    return &mesh;
}

result::Status DeviceMeshes::import(std::vector<mrhiAccess>& writes, std::vector<mrhiAccess>& reads) {
    for (const auto& [id, mesh] : chosen_) {
        FrameMesh made;
        if (const mrhiResult kImported = mrhiImportBuffer(native_, mesh->vertices, &made.vertices);
            kImported != mrhi_success) {
            return failed("a mesh could not join the frame", kImported);
        }
        if (const mrhiResult kImported = mrhiImportBuffer(native_, mesh->indices, &made.indices);
            kImported != mrhi_success) {
            return failed("a mesh could not join the frame", kImported);
        }
        if (mesh->influences.index1 != 0) {
            if (const mrhiResult kImported = mrhiImportBuffer(native_, mesh->influences, &made.influences);
                kImported != mrhi_success) {
                return failed("a mesh could not join the frame", kImported);
            }
        }
        imported_.emplace(mesh, made);
        reads.push_back(wholeOf(made.vertices, mrhi_accessVertex));
        reads.push_back(wholeOf(made.indices, mrhi_accessIndex));
    }
    for (const HeldMesh* mesh : uploads_) {
        const FrameMesh& kMade = imported_.at(mesh);
        writes.push_back(wholeOf(kMade.vertices, mrhi_accessCopyDestination));
        writes.push_back(wholeOf(kMade.indices, mrhi_accessCopyDestination));
        if (kMade.influences.index1 != 0) {
            writes.push_back(wholeOf(kMade.influences, mrhi_accessCopyDestination));
        }
    }
    return {};
}

result::Status DeviceMeshes::write(mrhiPassId upload) const {
    for (const HeldMesh* mesh : uploads_) {
        const std::vector<float> kVertices = verticesOf(*mesh->source);
        const FrameMesh& kMade = imported_.at(mesh);
        if (mrhiWriteBuffer(native_, upload, kMade.vertices, 0, kVertices.data(), kVertices.size() * sizeof(float)) !=
                mrhi_success ||
            mrhiWriteBuffer(
                native_, upload, kMade.indices, 0, mesh->source->indices.data(), mesh->source->indices.size() * 4) !=
                mrhi_success) {
            return failed("a mesh could not be written", mrhi_errorCapacity);
        }
        if (kMade.influences.index1 != 0) {
            const std::vector<std::uint32_t> kInfluences = influencesOf(*mesh->source);
            if (mrhiWriteBuffer(native_, upload, kMade.influences, 0, kInfluences.data(), kInfluences.size() * 4) !=
                mrhi_success) {
                return failed("a mesh's influences could not be written", mrhi_errorCapacity);
            }
        }
    }
    return {};
}

const FrameMesh& DeviceMeshes::frameMeshOf(const HeldMesh& mesh) const {
    return imported_.at(&mesh);
}

result::Status DeviceMeshes::bind(mrhiPassId pass, const HeldMesh& mesh, std::optional<VerticesAt> posed) const {
    const FrameMesh& kMade = imported_.at(&mesh);
    const mrhiResourceId kVertices = posed.has_value() ? posed->buffer : kMade.vertices;
    const std::uint64_t kSize = std::uint64_t{mesh.source->positions.size()} * kVertexBytes;
    if (mrhiSetVertexBuffer(native_,
                            pass,
                            0,
                            kVertices,
                            posed.has_value() ? posed->offset : 0,
                            posed.has_value() ? kSize : MRHI_WHOLE_SIZE) != mrhi_success ||
        mrhiSetIndexBuffer(native_, pass, kMade.indices, mrhi_indexUint32, 0, MRHI_WHOLE_SIZE) != mrhi_success) {
        return failed("a mesh could not be set to draw from", mrhi_errorState);
    }
    return {};
}

void DeviceMeshes::ended(bool submitted, RendererStatistics& statistics) noexcept {
    if (submitted) {
        for (HeldMesh* mesh : uploads_) {
            mesh->uploaded = true;
            ++statistics.meshesUploaded;
            statistics.uploadBytes += bytesOf(*mesh->source);
        }
    }
    uploads_.clear();
}

} // namespace rawframe::render_scene_gpu
