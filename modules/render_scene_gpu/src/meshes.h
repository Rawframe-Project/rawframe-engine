#pragma once

#include "rawframe/mesh/mesh.h"
#include "rawframe/render_scene_gpu/renderer.h"
#include "rawframe/result/result.h"

#include <cstdint>
#include <map>
#include <maul-rhi/frame.h>
#include <maul-rhi/resources.h>
#include <memory>
#include <utility>
#include <vector>

namespace rawframe::render_scene_gpu {

/// A mesh held on the device: the mesh it was made from, and whether its
/// vertices and indices are there yet.
struct HeldMesh {
    std::shared_ptr<const mesh::Mesh> source;
    mrhiBufferId vertices{};
    mrhiBufferId indices{};
    bool uploaded = false;
};

/// The meshes held on the device (D284): each made once from the mesh the
/// first draw naming it gives, its vertices and indices uploaded within the
/// frame's budget; one that would pass the budget waits for a later frame.
class DeviceMeshes {
public:
    DeviceMeshes(mrhiDevice* native, std::size_t maximum) noexcept;
    DeviceMeshes(const DeviceMeshes&) = delete;
    DeviceMeshes& operator=(const DeviceMeshes&) = delete;
    ~DeviceMeshes();

    /// Starts choosing what the frame being declared draws, within
    /// `budget` bytes of uploads.
    void begin(std::uint64_t budget) noexcept;
    /// What is left of the budget once the frame's meshes are chosen.
    [[nodiscard]] std::uint64_t left() const noexcept {
        return budget_;
    }
    /// The mesh `id` for the frame, made from what `given` gives if it is
    /// not held; none while the frame cannot draw it.
    const HeldMesh* choose(std::uint64_t id, const MeshSource& given, RendererStatistics& statistics);
    /// Brings every chosen mesh into the open frame, and adds what the
    /// upload pass writes of them to `writes` and what drawing reads to
    /// `reads`.
    result::Status import(std::vector<mrhiAccess>& writes, std::vector<mrhiAccess>& reads);

    /// Writes those uploading, in the upload pass.
    result::Status write(mrhiPassId upload) const;
    /// Sets a chosen mesh's vertices and indices to draw from in `pass`.
    result::Status bind(mrhiPassId pass, const HeldMesh& mesh) const;

    /// The frame ended: what it uploaded is held uploaded only if it was
    /// submitted.
    void ended(bool submitted, RendererStatistics& statistics) noexcept;

private:
    mrhiDevice* native_ = nullptr;
    std::size_t maximum_ = 0;
    std::map<std::uint64_t, HeldMesh> held_;
    std::map<std::uint64_t, HeldMesh*> chosen_;
    std::vector<HeldMesh*> uploads_;
    std::map<const HeldMesh*, std::pair<mrhiResourceId, mrhiResourceId>> imported_;
    std::uint64_t budget_ = 0;
};

} // namespace rawframe::render_scene_gpu
