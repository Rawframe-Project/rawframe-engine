#pragma once

#include "blocks.h"
#include "pipelines.h"
#include "rawframe/render_scene/scene.h"
#include "rawframe/result/result.h"

#include <array>
#include <cstdint>
#include <maul-rhi/frame.h>
#include <vector>

namespace rawframe::render_scene_gpu {

/// What the contact shadows' pass reads (std140, D338): the jittered
/// view's inverse, from clip space to the eye-relative World, and the view
/// itself; the direction toward the sun; the length and the near plane.
struct ContactBlock {
    Matrix4 toPoint{};
    Matrix4 viewProjection{};
    std::array<float, 4> toSun{};
    std::array<float, 4> settings{};
};
static_assert(sizeof(ContactBlock) == 160, "the contact shadows' shader reads its block as 160 bytes");

/// ADR-0051's screen-space contact shadows (D338), when the view asks: a
/// pass after the prepass follows each point toward the sun through the
/// prepass's depth, and the lit pass shades the sun's light by what it
/// found. Its target is the frame's own.
class ContactPass {
public:
    explicit ContactPass(mrhiDevice* native) noexcept;

    /// Joins the open frame if its view asks and `made`, its pipeline is
    /// made, at `width` by `height`, seen through `block`'s view: its
    /// target and block declared, and what the upload pass writes added to
    /// `writes`.
    result::Status declare(const render_scene::SceneFrame& frame,
                           bool made,
                           const FrameBlock& block,
                           std::uint32_t width,
                           std::uint32_t height,
                           std::vector<mrhiAccess>& writes);

    [[nodiscard]] bool enabled() const noexcept;
    /// How much of the sun's light reaches each point.
    [[nodiscard]] mrhiResourceId lit() const noexcept;

    /// Its pass, after the prepass that wrote `depth`.
    result::Status addPasses(mrhiResourceId depth);

    /// Its write, in the upload pass.
    result::Status write(mrhiPassId upload);

    /// Its pass recorded.
    result::Status record(const Pipelines& pipelines);

private:
    mrhiDevice* native_ = nullptr;
    bool enabled_ = false;
    ContactBlock block_;
    mrhiResourceId blockResource_{};
    mrhiResourceId depth_{};
    mrhiResourceId lit_{};
    mrhiPassId pass_{};
};

} // namespace rawframe::render_scene_gpu
