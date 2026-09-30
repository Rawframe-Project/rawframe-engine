#pragma once

// The scene's frames as a device draws them (D283): what the scene
// participant queued in this Host iteration's `present`, the view's size,
// and the meshes its draws name, for the GPU half to record after it.

#include "rawframe/composition/participant.h"
#include "rawframe/render_scene/scene.h"

#include <cstdint>
#include <memory>

namespace rawframe::render_scene {

class SceneFrames {
public:
    SceneFrames() = default;
    SceneFrames(const SceneFrames&) = delete;
    SceneFrames& operator=(const SceneFrames&) = delete;
    virtual ~SceneFrames() = default;

    /// The frame queued in this Host iteration's `present`, until the next
    /// iteration's `presentation_extract`; none in an iteration that queued
    /// none, and while the scene is idle.
    [[nodiscard]] virtual const SceneFrame* queued() const noexcept = 0;
    /// The view's size in pixels: `scene.width` and `scene.height` until
    /// what shows it sets another.
    [[nodiscard]] virtual std::uint32_t width() const noexcept = 0;
    [[nodiscard]] virtual std::uint32_t height() const noexcept = 0;
    /// The view's size from the next frame on, as the window it is shown in
    /// has it: the camera's aspect follows. Sides of nought are ignored.
    virtual void resize(std::uint32_t width, std::uint32_t height) noexcept = 0;
    /// The mesh a draw names, the game's or the engine's; none for another,
    /// and while the scene is idle.
    [[nodiscard]] virtual std::shared_ptr<const mesh::Mesh> mesh(std::uint64_t id) const = 0;
};

inline constexpr composition::Capability<SceneFrames> kSceneFrames{"rawframe.render_scene.frames"};

} // namespace rawframe::render_scene
