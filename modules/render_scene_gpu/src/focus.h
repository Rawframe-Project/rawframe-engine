#pragma once

#include "pipelines.h"
#include "rawframe/render_scene/scene.h"
#include "rawframe/result/result.h"

#include <array>
#include <cstdint>
#include <maul-rhi/frame.h>
#include <vector>

namespace rawframe::render_scene_gpu {

/// What the depth of field's passes read (std140, D336): the circle of
/// confusion's radius in pixels for a point infinitely far, the focus and
/// the near plane in meters, and the longest radius in pixels.
struct LensBlock {
    std::array<float, 4> settings{};
};
static_assert(sizeof(LensBlock) == 16, "the depth of field's shaders read their block as 16 bytes");

/// ADR-0051's depth of field (D336), the post chain's third stage, when the
/// view asks: a thin lens of the view's field on a full-frame sensor. One
/// pass halves the light beside each texel's circle of confusion, another
/// gathers Gustafsson's golden-angle bokeh at half size, and a third
/// blends each full-size point toward it as far as its circle, or a nearer
/// one over it, reaches. Its targets are the frame's own.
class DepthOfFieldPass {
public:
    explicit DepthOfFieldPass(mrhiDevice* native) noexcept;

    /// Joins the open frame if its view asks, at `width` by `height`: its
    /// targets and block declared, and what the upload pass writes added to
    /// `writes`.
    result::Status declare(const render_scene::SceneFrame& frame,
                           bool made,
                           std::uint32_t width,
                           std::uint32_t height,
                           std::vector<mrhiAccess>& writes);

    [[nodiscard]] bool enabled() const noexcept;
    /// What the frame shows after it: the focused light, or `shown` without
    /// it.
    [[nodiscard]] mrhiResourceId shown(mrhiResourceId shown) const noexcept;

    /// Its passes, reading the light `shown` and its `depth`.
    result::Status addPasses(mrhiResourceId shown, mrhiResourceId depth);

    /// Its write, in the upload pass.
    result::Status write(mrhiPassId upload);

    /// Its passes recorded.
    result::Status record(const Pipelines& pipelines);

private:
    mrhiDevice* native_ = nullptr;
    bool enabled_ = false;
    LensBlock block_;
    mrhiResourceId blockResource_{};
    mrhiResourceId light_{};
    mrhiResourceId depth_{};
    mrhiResourceId halved_{};
    mrhiResourceId bokeh_{};
    mrhiResourceId focused_{};
    mrhiPassId prefilterPass_{};
    mrhiPassId bokehPass_{};
    mrhiPassId combinePass_{};
};

} // namespace rawframe::render_scene_gpu
