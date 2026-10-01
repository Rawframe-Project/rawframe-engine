#pragma once

#include "particles.h"
#include "pipelines.h"
#include "rawframe/render_scene/scene.h"
#include "rawframe/result/result.h"

#include <array>
#include <cstdint>
#include <maul-rhi/frame.h>
#include <vector>

namespace rawframe::render_scene_gpu {

/// A ribbon's point as the ribbons' shader reads it (std430, D354): where
/// it is relative to the eye and its width; its color; and its texture's
/// coordinate along the ribbon.
struct RibbonPointBlock {
    std::array<float, 4> placeWidth{};
    std::array<float, 4> color{};
    std::array<float, 4> along{};
};
static_assert(sizeof(RibbonPointBlock) == 48, "the ribbons' shader reads a point as 48 bytes");

/// The trails and beams on the device (ADR-0053, D354): each frame's
/// ribbons' points uploaded, and each ribbon drawn over the models' light,
/// farthest first, before the particles in the pass the unlit draws share:
/// its points joined by quads facing the eye, shaded and faded as a
/// particle is, by the particles' fragments.
class RibbonPass {
public:
    explicit RibbonPass(mrhiDevice* native) noexcept;

    /// Joins the open frame when it draws ribbons and their pipelines are
    /// `made`, adding what the upload pass writes to `writes`.
    result::Status declare(const render_scene::SceneFrame& frame, bool made, std::vector<mrhiAccess>& writes);

    /// What drawing the ribbons reads, added to `reads`.
    void drawReads(std::vector<mrhiAccess>& reads) const;

    /// Its writes, in the upload pass: its points, its ribbons, and its
    /// view.
    result::Status write(mrhiPassId upload);

    /// The ribbons drawn in `pass`, the pass the unlit draws share.
    result::Status recordDraws(mrhiPassId pass, const Pipelines& pipelines, const ParticleDrawing& with);

    /// Whether the open frame draws ribbons.
    [[nodiscard]] bool enabled() const noexcept;

    /// The open frame's ribbons drawn.
    [[nodiscard]] std::size_t drawn() const noexcept;

private:
    mrhiDevice* native_ = nullptr;
    /// The open frame's: whether it draws ribbons, their points, each
    /// ribbon's block at the particles' stride, their materials' textures,
    /// and its view.
    bool enabled_ = false;
    std::vector<RibbonPointBlock> points_;
    std::vector<std::uint8_t> blocks_;
    std::vector<render_scene::SceneTextures> textures_;
    std::vector<std::uint32_t> counts_;
    ParticleViewBlock view_;
    mrhiResourceId pointsResource_{};
    mrhiResourceId blocksResource_{};
    mrhiResourceId viewResource_{};
};

} // namespace rawframe::render_scene_gpu
