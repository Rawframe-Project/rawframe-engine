#pragma once

#include "blocks.h"
#include "pipelines.h"
#include "rawframe/render_scene/scene.h"
#include "rawframe/result/result.h"

#include <maul-rhi/frame.h>
#include <vector>

namespace rawframe::render_scene_gpu {

/// The exposure the device holds, and the metering that moves it (D293):
/// one buffer kept from frame to frame, which every scene pass reads. Each
/// frame either writes the camera's exposure into it, or, metered, counts
/// the frame's light into a histogram after the models' pass and moves the
/// exposure toward what it measured, for the next frame to draw with; the
/// first metered frame starts from the camera's.
class Metering {
public:
    explicit Metering(mrhiDevice* native) noexcept;
    Metering(const Metering&) = delete;
    Metering& operator=(const Metering&) = delete;
    ~Metering();

    /// The buffer, made once.
    result::Status make();

    /// Joins the open frame: the buffer imported, what the metering needs
    /// declared, and what the upload pass writes added to `writes`.
    result::Status declare(const render_scene::SceneFrame& frame, std::vector<mrhiAccess>& writes);

    /// The exposure's resource in the open frame.
    [[nodiscard]] mrhiResourceId exposure() const noexcept;

    /// The metering's passes, after the models' pass that drew `scene`.
    result::Status addPasses(mrhiResourceId scene);

    /// Its writes, in the upload pass.
    result::Status write(mrhiPassId upload);

    /// Its passes recorded, over a target `width` by `height`.
    result::Status record(const Pipelines& pipelines, mrhiResourceId scene, std::uint32_t width, std::uint32_t height);

    /// The frame ended: a metered exposure carries on only after a frame
    /// that was submitted.
    void ended(bool submitted) noexcept;

    /// Whether the open frame is metered.
    [[nodiscard]] bool metered() const noexcept;

private:
    mrhiDevice* native_ = nullptr;
    mrhiBufferId buffer_{};
    /// Whether the buffer holds a metered exposure to carry on from.
    bool carried_ = false;
    /// The open frame's: whether it is metered, whether the camera's
    /// exposure is written, and what is.
    bool metered_ = false;
    bool writes_ = false;
    ExposureBlock start_;
    MeterBlock meter_;
    mrhiResourceId exposure_{};
    mrhiResourceId histogram_{};
    mrhiResourceId meterResource_{};
    mrhiPassId histogramPass_{};
    mrhiPassId adaptPass_{};
};

} // namespace rawframe::render_scene_gpu
