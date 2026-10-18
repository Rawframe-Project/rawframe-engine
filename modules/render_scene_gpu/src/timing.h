#pragma once

// The scene's passes timed on the GPU (D894): the renderer points the
// timer at the frame it declares, and each pass it times is named as it
// is added, its start and end written to the frame's timestamps where the
// frame times its passes.

#include "rawframe/render/frame.h"

#include <cstdint>
#include <maul-rhi/frame.h>
#include <string_view>

namespace rawframe::render_scene_gpu {

class PassTimer {
public:
    /// The frame being declared, none between frames.
    void declaring(render::Frame* frame) noexcept {
        frame_ = frame;
    }
    /// `def`'s pass, of the graphics class, timed as `pass`.
    void time(mrhiPassDef& def, std::string_view pass) const {
        if (frame_ == nullptr) {
            return;
        }
        if (const std::optional<std::uint32_t> kAt = frame_->time(pass); kAt.has_value()) {
            def.timestampQuerySet = mrhiQuerySetId{.index1 = static_cast<std::uint32_t>(frame_->timestamps >> 32U),
                                                   .generation = static_cast<std::uint32_t>(frame_->timestamps)};
            def.timestampBegin = *kAt;
            def.timestampEnd = *kAt + 1;
        }
    }

private:
    render::Frame* frame_ = nullptr;
};

} // namespace rawframe::render_scene_gpu
