#pragma once

// Posed models' vertices on the device (D508): each chosen skinned mesh
// and the palette it is posed with skinned once a frame, by a compute pass
// before any drawing, into one buffer of the frame's in the layout the
// scene's pipelines read; every draw of that model, its shadows' too,
// reads them in its mesh's place, so no drawing pipeline changes.

#include "meshes.h"
#include "pipelines.h"
#include "rawframe/render_scene/scene.h"
#include "rawframe/result/result.h"
#include "runs.h"

#include <cstdint>
#include <map>
#include <maul-rhi/frame.h>
#include <optional>
#include <utility>
#include <vector>

namespace rawframe::render_scene_gpu {

class DeviceSkinning {
public:
    explicit DeviceSkinning(mrhiDevice* native) noexcept;

    /// The frame's posed models whose meshes are chosen and skinned, each
    /// mesh and palette once; none where `ready` is false, as the skinning
    /// pipeline is not made yet, and those models are drawn as bound.
    /// Declares the frame's palette, each model's block, and the skinned
    /// vertices, adding what the upload pass writes to `writes`.
    result::Status declare(const render_scene::SceneFrame& frame,
                           const std::map<std::uint64_t, const HeldMesh*>& usable,
                           bool ready,
                           std::vector<mrhiAccess>& writes);
    /// Adds the pass skinning them, after the upload, and adds what drawing
    /// reads of what it skinned to `reads`.
    result::Status addPass(const DeviceMeshes& meshes, std::vector<mrhiAccess>& reads);
    result::Status write(mrhiPassId upload) const;
    result::Status record(const Pipelines& pipelines, const DeviceMeshes& meshes) const;

    /// The vertices a draw of `mesh` posed with the palette from `palette`
    /// reads; none for one this frame draws as bound.
    [[nodiscard]] std::optional<VerticesAt> posedOf(const HeldMesh* mesh, std::uint32_t palette) const noexcept;
    /// The models skinned this frame.
    [[nodiscard]] std::size_t skinned() const noexcept {
        return jobs_.size();
    }

private:
    /// A model to skin: its mesh, its first joint matrix among the frame's,
    /// and its first vertex among the skinned ones.
    struct Job {
        const HeldMesh* mesh = nullptr;
        std::uint32_t palette = 0;
        std::uint32_t first = 0;
    };

    mrhiDevice* native_ = nullptr;
    std::vector<Job> jobs_;
    std::map<std::pair<const HeldMesh*, std::uint32_t>, std::size_t> found_;
    std::vector<float> palette_;
    std::vector<std::uint32_t> blocks_;
    std::uint64_t vertices_ = 0;
    mrhiResourceId paletteResource_{};
    mrhiResourceId blocksResource_{};
    mrhiResourceId posedResource_{};
    mrhiPassId pass_{};
};

/// What `run` draws from in its mesh's place: its model's skinned vertices,
/// where it is posed and they were skinned; none otherwise.
[[nodiscard]] inline std::optional<VerticesAt> posedOf(const DeviceSkinning* skinning, const Run& run) noexcept {
    return skinning != nullptr && run.joints != 0 ? skinning->posedOf(run.mesh, run.palette) : std::nullopt;
}

} // namespace rawframe::render_scene_gpu
