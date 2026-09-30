#pragma once

#include "pipelines.h"
#include "rawframe/render_scene/scene.h"
#include "rawframe/result/result.h"

#include <cstdint>
#include <maul-rhi/frame.h>
#include <vector>

namespace rawframe::render_scene_gpu {

/// ADR-0051's bloom (D328), when the view asks: the scene's light, after
/// the temporal slot, halved level by level into a chain (at most six, the
/// first at a quarter of the target's sides) by Jimenez's thirteen taps,
/// the first level through Karis's average; then each level doubled by a
/// tent and added to the one above, so the first holds every level's
/// light. The picture's pass mixes it with the scene's light before the
/// grade. Its levels are the frame's own.
class BloomPass {
public:
    explicit BloomPass(mrhiDevice* native) noexcept;

    /// Joins the open frame if its view asks, for a target `width` by
    /// `height`: its levels declared.
    result::Status declare(const render_scene::SceneFrame& frame, std::uint32_t width, std::uint32_t height);

    [[nodiscard]] bool enabled() const noexcept;
    /// The first level, holding every level's light, and how many there
    /// are.
    [[nodiscard]] mrhiResourceId spread() const noexcept;
    [[nodiscard]] std::size_t levels() const noexcept;

    /// Its passes, reading `scene`.
    result::Status addPasses(mrhiResourceId scene);

    /// Its passes recorded.
    result::Status record(const Pipelines& pipelines, mrhiResourceId scene);

private:
    mrhiDevice* native_ = nullptr;
    bool enabled_ = false;
    std::vector<mrhiResourceId> levels_;
    /// Each halving, then each doubling, from the smallest level up.
    std::vector<mrhiPassId> down_;
    std::vector<mrhiPassId> up_;
};

} // namespace rawframe::render_scene_gpu
