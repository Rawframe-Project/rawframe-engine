#pragma once

#include "blocks.h"
#include "meshes.h"
#include "pipelines.h"
#include "rawframe/render/textures.h"
#include "rawframe/result/result.h"
#include "runs.h"
#include "skinning.h"

#include <array>
#include <cstdint>
#include <maul-rhi/encoder.h>
#include <maul-rhi/frame.h>
#include <span>
#include <vector>

namespace rawframe::render_scene_gpu {

/// A square of a shadow map: its view, where it lies, and its casters.
struct Square {
    mrhiResourceId view{};
    mrhiViewport viewport{};
    const Casters* casters = nullptr;
};

/// What a frame's shadow passes draw with: the device, its pipelines, the
/// meshes and textures held, and the frame's placements and materials.
struct Casting {
    mrhiDevice* native = nullptr;
    const Pipelines* pipelines = nullptr;
    const DeviceMeshes* held = nullptr;
    const DeviceSkinning* skinning = nullptr;
    const render::DeviceTextures* textures = nullptr;
    mrhiResourceId instances{};
    mrhiResourceId materials{};
    std::uint64_t materialsBytes = 0;
};

/// A shadow pass recorded: each square drawn from its view, its solid
/// casters in their runs, then its masked ones cut by their material's
/// texture (D310).
result::Status cast(const Casting& with, mrhiPassId pass, std::span<const Square> squares);

/// A frame's shadow maps (D289, D292): the sun's, its cascades' squares
/// two by two, and the punctual lights' atlas, with the view each square
/// is drawn from, the squares as the lit shaders read them, and the two
/// passes drawing their casters.
class ShadowPasses {
public:
    explicit ShadowPasses(mrhiDevice* native) noexcept;

    /// The maps, views, and squares declared for `frame`, its cascades'
    /// views those `block` holds; what the upload pass writes added to
    /// `writes`.
    result::Status
    declare(const render_scene::SceneFrame& frame, const FrameBlock& block, std::vector<mrhiAccess>& writes);

    /// The two passes, each reading `reads` (what drawing a model reads)
    /// and its own views.
    result::Status addPasses(const std::vector<mrhiAccess>& reads);

    /// Its writes, in the upload pass.
    result::Status write(mrhiPassId upload);

    /// Its passes recorded, the casters those `placed` holds.
    result::Status record(const Casting& with, const Placed& placed);

    /// What the lit passes read of it, added to `reads`: the maps' depths
    /// and the squares.
    void readBy(std::vector<mrhiAccess>& reads) const;

    [[nodiscard]] mrhiResourceId sunMap() const noexcept;
    [[nodiscard]] mrhiResourceId lightMap() const noexcept;
    [[nodiscard]] mrhiResourceId squares() const noexcept;
    [[nodiscard]] std::uint64_t squareBytes() const noexcept;

private:
    mrhiDevice* native_ = nullptr;
    /// The sun's map, its cascades' views and their matrices, a square's
    /// side, and its pass.
    mrhiResourceId sunMap_{};
    std::array<mrhiResourceId, 4> cascades_{};
    std::array<Matrix4, 4> cascadeMatrices_{};
    std::size_t cascadeCount_ = 0;
    std::uint32_t side_ = 0;
    mrhiPassId sunPass_{};
    /// The punctual lights' atlas, its squares as the shaders read them,
    /// each square's view and where it lies, and its pass.
    mrhiResourceId lightMap_{};
    std::vector<SlotBlock> slots_;
    mrhiResourceId slotsResource_{};
    std::vector<Matrix4> slotMatrices_;
    std::vector<mrhiResourceId> slotViews_;
    std::vector<mrhiViewport> slotViewports_;
    mrhiPassId lightPass_{};
};

} // namespace rawframe::render_scene_gpu
