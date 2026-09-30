#pragma once

#include "rawframe/render/device.h"
#include "rawframe/render_scene_gpu/renderer.h"
#include "rawframe/result/result.h"

#include <cstdint>
#include <maul-rhi/frame.h>
#include <optional>

namespace rawframe::render_scene_gpu {

/// A frame's light read back from its scene target (D326), when asked: a
/// pass after the models' copies the target out, and once the device has
/// finished the frame its half floats are taken, divided by the exposure
/// the frame was drawn at. A frame not submitted keeps the ask for the
/// next.
class LightCapturing {
public:
    explicit LightCapturing(render::Device& device) noexcept;

    void ask() noexcept;

    /// Joins the open frame if asked, reading `scene`, `width` by `height`,
    /// drawn at `exposure` (the factor light was scaled by); a target past
    /// what the device reads back at once is not read, and the ask is
    /// dropped.
    result::Status declare(mrhiResourceId scene, std::uint32_t width, std::uint32_t height, float exposure);

    /// Its pass recorded, if it joined.
    result::Status record(mrhiResourceId scene);

    void ended(bool submitted) noexcept;

    /// The light read, once the device finished the frame; given once.
    [[nodiscard]] std::optional<LightCapture> taken();

private:
    render::Device* device_ = nullptr;
    bool asked_ = false;
    std::optional<mrhiPassId> pass_;
    std::optional<mrhiRequestId> reading_;
    std::uint32_t width_ = 0;
    std::uint32_t height_ = 0;
    float exposure_ = 1;
    /// The frame's, for the reading it submits.
    std::uint32_t frameWidth_ = 0;
    std::uint32_t frameHeight_ = 0;
    float frameExposure_ = 1;
};

} // namespace rawframe::render_scene_gpu
