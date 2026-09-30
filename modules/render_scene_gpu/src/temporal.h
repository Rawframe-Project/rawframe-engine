#pragma once

#include "blocks.h"
#include "pipelines.h"
#include "rawframe/render_scene/scene.h"
#include "rawframe/result/result.h"

#include <array>
#include <maul-rhi/frame.h>
#include <vector>

namespace rawframe::render_scene_gpu {

/// The temporal pass (D291) and the two pictures it keeps from frame to
/// frame: each antialiased frame blends into one of them with the other,
/// the picture before, which the next frame reads. The pictures follow
/// the target's size, made anew (and the history forgotten) when it
/// changes; a frame not submitted forgets it too.
class TemporalPass {
public:
    explicit TemporalPass(mrhiDevice* native) noexcept;
    TemporalPass(const TemporalPass&) = delete;
    TemporalPass& operator=(const TemporalPass&) = delete;
    ~TemporalPass();

    /// Joins the open frame if it is antialiased over time, at `width` by
    /// `height`: the pictures imported, its state declared, and what the
    /// upload pass writes added to `writes`.
    result::Status declare(const render_scene::SceneFrame& frame,
                           std::uint32_t width,
                           std::uint32_t height,
                           std::vector<mrhiAccess>& writes);

    /// Whether the open frame is antialiased over time, and whether it
    /// reuses the picture before.
    [[nodiscard]] bool enabled() const noexcept;
    [[nodiscard]] bool reused() const noexcept;

    /// What the frame shows: the blended picture, or `scene` without it.
    [[nodiscard]] mrhiResourceId shown(mrhiResourceId scene) const noexcept;

    /// Its pass, reading the frame drawn into `scene` and its `motion`.
    result::Status addPass(mrhiResourceId scene, mrhiResourceId motion);

    /// Its write, in the upload pass.
    result::Status write(mrhiPassId upload);

    /// Its pass recorded.
    result::Status record(const Pipelines& pipelines, mrhiResourceId scene, mrhiResourceId motion);

    /// The frame ended: the picture written is the next frame's picture
    /// before only when the frame was submitted and antialiased.
    void ended(bool submitted) noexcept;

private:
    void drop() noexcept;
    result::Status sized(std::uint32_t width, std::uint32_t height);

    mrhiDevice* native_ = nullptr;
    std::array<mrhiTextureId, 2> pictures_{};
    std::uint32_t width_ = 0;
    std::uint32_t height_ = 0;
    /// Which picture was written last, and whether it holds one a
    /// submitted frame drew.
    std::size_t last_ = 0;
    bool ready_ = false;
    /// The open frame's.
    bool enabled_ = false;
    std::size_t writing_ = 0;
    TemporalBlock block_;
    mrhiResourceId resolved_{};
    mrhiResourceId before_{};
    mrhiResourceId state_{};
    mrhiPassId pass_{};
};

} // namespace rawframe::render_scene_gpu
