#pragma once

// The scene's light captured for a tool (D326): a frame drawn in place of
// the one the scene queued, and its light read back before the tonemapper.

#include "rawframe/composition/participant.h"
#include "rawframe/render_scene/scene.h"
#include "rawframe/render_scene_gpu/renderer.h"

#include <optional>

namespace rawframe::render_scene_gpu {

class SceneCaptures {
public:
    SceneCaptures() = default;
    SceneCaptures(const SceneCaptures&) = delete;
    SceneCaptures& operator=(const SceneCaptures&) = delete;
    virtual ~SceneCaptures() = default;

    /// Draws `frame` in place of the one the scene queues, from the next
    /// frame made until its light is captured, and reads its light back.
    /// False while nothing is drawn, and while another is being captured.
    [[nodiscard]] virtual bool capture(render_scene::SceneFrame frame) = 0;
    /// The light captured, once the device finished its frame; given once.
    /// Never waits.
    [[nodiscard]] virtual std::optional<LightCapture> captured() = 0;
};

inline constexpr composition::Capability<SceneCaptures> kSceneCaptures{"rawframe.render_scene_gpu.captures"};

} // namespace rawframe::render_scene_gpu
