#pragma once

#include "pipelines.h"
#include "rawframe/render_scene/scene.h"
#include "rawframe/result/result.h"

#include <array>
#include <cstdint>
#include <maul-rhi/frame.h>
#include <vector>

namespace rawframe::render_scene_gpu {

/// What the motion blur's passes read (std140, D334): half the shutter's
/// share of the frame, a tile's side and the longest blur in pixels, and
/// the near plane.
struct MotionBlock {
    std::array<float, 4> settings{};
};
static_assert(sizeof(MotionBlock) == 16, "the motion blur's shaders read their block as 16 bytes");

/// ADR-0051's motion blur (D334), the post chain's second stage, when the
/// view asks: McGuire et al. 2012's reconstruction filter over the light
/// the temporal slot shows, its motion, and its depth. One pass finds the
/// longest blur in each tile, another the longest among each tile's
/// neighbors, and a third gathers each point's blurred light. Its targets
/// are the frame's own.
class MotionBlurPass {
public:
    explicit MotionBlurPass(mrhiDevice* native) noexcept;

    /// Joins the open frame if its view asks, at `width` by `height`: its
    /// targets and block declared, and what the upload pass writes added to
    /// `writes`.
    result::Status declare(const render_scene::SceneFrame& frame,
                           std::uint32_t width,
                           std::uint32_t height,
                           std::vector<mrhiAccess>& writes);

    [[nodiscard]] bool enabled() const noexcept;
    /// What the frame shows after it: the blurred light, or `shown`
    /// without it.
    [[nodiscard]] mrhiResourceId shown(mrhiResourceId shown) const noexcept;

    /// Its passes, reading the light `shown`, its `motion`, and its
    /// `depth`.
    result::Status addPasses(mrhiResourceId shown, mrhiResourceId motion, mrhiResourceId depth);

    /// Its write, in the upload pass.
    result::Status write(mrhiPassId upload);

    /// Its passes recorded.
    result::Status record(const Pipelines& pipelines);

private:
    mrhiDevice* native_ = nullptr;
    bool enabled_ = false;
    MotionBlock block_;
    mrhiResourceId blockResource_{};
    mrhiResourceId light_{};
    mrhiResourceId motion_{};
    mrhiResourceId depth_{};
    mrhiResourceId tiles_{};
    mrhiResourceId neighbors_{};
    mrhiResourceId blurred_{};
    mrhiPassId tilePass_{};
    mrhiPassId neighborPass_{};
    mrhiPassId gatherPass_{};
};

} // namespace rawframe::render_scene_gpu
