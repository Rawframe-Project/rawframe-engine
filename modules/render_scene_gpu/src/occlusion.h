#pragma once

#include "blocks.h"
#include "pipelines.h"
#include "rawframe/render_scene/scene.h"
#include "rawframe/result/result.h"

#include <array>
#include <maul-rhi/frame.h>
#include <vector>

namespace rawframe::render_scene_gpu {

/// What the ambient occlusion's passes read (std140, D327): the jittered
/// view's inverse, from clip space to the eye-relative World, and the view
/// itself; the radius, the intensity, and the near plane.
struct OcclusionBlock {
    Matrix4 toPoint{};
    Matrix4 viewProjection{};
    std::array<float, 4> settings{};
};
static_assert(sizeof(OcclusionBlock) == 144, "the occlusion's shaders read their block as 144 bytes");

/// ADR-0051's screen-space ambient occlusion (D327), when the view asks:
/// the prepass leaves each point's surface beside its depth, a pass finds
/// what of the light from all around reaches each point, at half the
/// target's size, and another blurs it over the four-by-four tile its
/// sample turns repeat in. The lit pass reads what reaches. Its targets
/// are the frame's own.
class OcclusionPass {
public:
    explicit OcclusionPass(mrhiDevice* native) noexcept;

    /// Joins the open frame if its view asks, at `width` by `height`, seen
    /// through `block`'s view: its targets and block declared, and what
    /// the upload pass writes added to `writes`. The prepass's surfaces
    /// target is the renderer's, shared with the reflections (D331).
    result::Status declare(const render_scene::SceneFrame& frame,
                           bool made,
                           const FrameBlock& block,
                           std::uint32_t width,
                           std::uint32_t height,
                           std::vector<mrhiAccess>& writes);

    [[nodiscard]] bool enabled() const noexcept;
    /// What reaches each point, blurred.
    [[nodiscard]] mrhiResourceId reaching() const noexcept;

    /// Its two passes, after the prepass that wrote `depth` and `surfaces`.
    result::Status addPasses(mrhiResourceId depth, mrhiResourceId surfaces);

    /// Its write, in the upload pass.
    result::Status write(mrhiPassId upload);

    /// Its passes recorded.
    result::Status record(const Pipelines& pipelines, mrhiResourceId depth);

private:
    mrhiDevice* native_ = nullptr;
    bool enabled_ = false;
    OcclusionBlock block_;
    mrhiResourceId blockResource_{};
    mrhiResourceId surfaces_{};
    mrhiResourceId raw_{};
    mrhiResourceId blurred_{};
    mrhiPassId occludePass_{};
    mrhiPassId blurPass_{};
};

} // namespace rawframe::render_scene_gpu
