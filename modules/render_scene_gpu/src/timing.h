#pragma once

// The scene's passes timed on the GPU: the renderer points the
// timer at the frame it declares, and each pass it times is named as it
// is added, through render's one timing of a pass.

#include "rawframe/render/frame.h"
#include "rawframe/render/timing.h"

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
        if (frame_ != nullptr) {
            render::timePass(*frame_, def, pass);
        }
    }

private:
    render::Frame* frame_ = nullptr;
};

} // namespace rawframe::render_scene_gpu
