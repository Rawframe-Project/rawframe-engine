#pragma once

// The scene's frames as a device draws them (D283): what the scene
// participant queued in this Host iteration's `present`, the view's size,
// and the meshes and textures its draws name, for the GPU half to record
// after it.

#include "rawframe/composition/participant.h"
#include "rawframe/render_scene/scene.h"
#include "rawframe/texture/texture.h"

#include <cstdint>
#include <memory>
#include <span>

namespace rawframe::render_scene {

/// A render texture's frame (ADR-0052, D361): its identity and size, and
/// the frame its view queued this Host iteration; none while no view names
/// it.
struct TextureFrame {
    std::uint64_t id = 0;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    const SceneFrame* frame = nullptr;
};

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
    /// The game's render textures, each with the frame its view queued in
    /// this Host iteration's `present` (D361), until the next iteration's
    /// `presentation_extract`; a device draws them before the view's.
    [[nodiscard]] virtual std::span<const TextureFrame> textureFrames() const noexcept = 0;
    /// The device did not draw the frame the render texture `id` was last
    /// given: one drawn on demand is offered it again in the next
    /// iteration, unless a new one is due.
    virtual void missed(std::uint64_t id) noexcept = 0;
    /// The view's size in pixels: `scene.width` and `scene.height` until
    /// what shows it sets another.
    [[nodiscard]] virtual std::uint32_t width() const noexcept = 0;
    [[nodiscard]] virtual std::uint32_t height() const noexcept = 0;
    /// The view's size from the next frame on, as the window it is shown in
    /// has it: the camera's aspect follows. Sides of nought are ignored.
    virtual void resize(std::uint32_t width, std::uint32_t height) noexcept = 0;
    /// Queues a frame from `camera` now, in place of the one queued, from
    /// the World last extracted: for a tool drawing the scene from
    /// elsewhere (D326). The next `present` queues the view's own again;
    /// the models' motion then is measured from this frame. None while the
    /// scene is idle.
    [[nodiscard]] virtual const SceneFrame* queueFrom(const SceneCamera& camera) noexcept = 0;
    /// The reflection probes last extracted, where their poses put them
    /// (D325); none while the scene is idle.
    [[nodiscard]] virtual std::span<const ProbeInstance> probes() const noexcept = 0;
    /// The mesh a draw names, the game's or the engine's; none for another,
    /// and while the scene is idle.
    [[nodiscard]] virtual std::shared_ptr<const mesh::Mesh> mesh(std::uint64_t id) const = 0;
    /// The texture a material samples, decoded and held (D309); none while
    /// it is not ready, for one the game does not declare, and while the
    /// scene is idle.
    [[nodiscard]] virtual std::shared_ptr<const texture::Texture> texture(std::uint64_t id) const = 0;
};

inline constexpr composition::Capability<SceneFrames> kSceneFrames{"rawframe.render_scene.frames"};

} // namespace rawframe::render_scene
