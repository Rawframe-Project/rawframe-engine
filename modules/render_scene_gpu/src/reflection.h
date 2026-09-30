#pragma once

#include "blocks.h"
#include "pipelines.h"
#include "rawframe/render_scene/scene.h"
#include "rawframe/result/result.h"

#include <array>
#include <maul-rhi/frame.h>
#include <vector>

namespace rawframe::render_scene_gpu {

/// What the reflections' pass reads (std140, D331): the jittered view's
/// inverse, from clip space to the eye-relative World; the view itself;
/// the frame before's view taking this frame's places; the distance
/// followed and the near plane.
struct ReflectionBlock {
    Matrix4 toPoint{};
    Matrix4 viewProjection{};
    Matrix4 previous{};
    std::array<float, 4> settings{};
};
static_assert(sizeof(ReflectionBlock) == 208, "the reflections' shader reads its block as 208 bytes");

/// ADR-0051's screen-space reflections (D331), when the view asks and the
/// temporal pass holds a picture before: from the prepass's depth and
/// surfaces, a pass follows each smooth point's reflection through the
/// depth and takes what the picture before saw where it meets something,
/// with how much. The lit pass lays it over what the probe or the sky
/// would give. Its target is the frame's own.
class ReflectionPass {
public:
    explicit ReflectionPass(mrhiDevice* native) noexcept;

    /// Joins the open frame if its view asks and `before`, the temporal
    /// pass's picture before, is one to read (none for none), at `width`
    /// by `height`, seen through `block`'s view: its target and block
    /// declared, and what the upload pass writes added to `writes`.
    result::Status declare(const render_scene::SceneFrame& frame,
                           bool made,
                           const FrameBlock& block,
                           mrhiResourceId before,
                           std::uint32_t width,
                           std::uint32_t height,
                           std::vector<mrhiAccess>& writes);

    [[nodiscard]] bool enabled() const noexcept;
    /// What each point reflects, pre-exposed, and how much in its alpha.
    [[nodiscard]] mrhiResourceId reflected() const noexcept;

    /// Its pass, after the prepass that wrote `depth` and `surfaces`.
    result::Status addPasses(mrhiResourceId depth, mrhiResourceId surfaces);

    /// Its write, in the upload pass.
    result::Status write(mrhiPassId upload);

    /// Its pass recorded.
    result::Status record(const Pipelines& pipelines);

private:
    mrhiDevice* native_ = nullptr;
    bool enabled_ = false;
    ReflectionBlock block_;
    mrhiResourceId blockResource_{};
    mrhiResourceId before_{};
    mrhiResourceId depth_{};
    mrhiResourceId surfaces_{};
    mrhiResourceId reflected_{};
    mrhiPassId pass_{};
};

} // namespace rawframe::render_scene_gpu
