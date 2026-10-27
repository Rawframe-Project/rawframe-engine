#pragma once

// The views a game's render textures show: of the entities whose
// view names a texture, the one of the lowest order, its World drawn by
// the texture's own scene every frame or on demand.

#include "cameras.h"
#include "pictures.h"
#include "rawframe/diagnostics/emitter.h"
#include "rawframe/execution/time.h"
#include "rawframe/render_scene/frames.h"
#include "rawframe/render_scene/scene.h"
#include "rawframe/world/column_query.h"
#include "rawframe/world_kest/game.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <vector>

namespace rawframe::render_scene {

/// Each render texture's view, its scene and camera, and the frames it
/// queued; and how many were drawn, kept (on demand, nothing asked),
/// missed by the device, left out, and refused. Holds none before it is
/// made from a game's render textures.
class TextureViews {
public:
    TextureViews() = default;
    /// The render textures `textures` declares, shown by the entities that
    /// carry `view`, the game's view component.
    TextureViews(std::optional<schema::ComponentTypeId> view, std::span<const world_kest::GameRenderTexture> textures);

    [[nodiscard]] bool empty() const noexcept;
    /// Each render texture's frame, none where nothing was queued for it
    /// this frame.
    [[nodiscard]] std::span<const TextureFrame> frames() const noexcept;
    /// Every frame forgotten, as the next is extracted.
    void forget() noexcept;
    /// Each render texture's view: of the entities whose view names it,
    /// the one of the lowest order, its World extracted by the render
    /// texture's own scene, made with `settings`, and its camera read when
    /// it is due: every frame, or on demand when the texture shows another
    /// view or request. Two of the lowest order show none, and are told;
    /// the others are left out.
    void extract(world::World& world,
                 const SceneSettings& settings,
                 double shown,
                 const world_animation::AnimationQueries* poses,
                 const GameCameras& cameras,
                 diagnostics::Emitter& emitter);
    /// Each render texture's view queued when due, at its own aspect and
    /// on its own clock; one drawn on demand taken as shown, and its frame
    /// offered again as it was when the device missed it. The pictures its
    /// frames name are asked of `pictures`.
    void present(execution::MonotonicInstant now, ScenePictures& pictures);
    /// The device missed the frame offered for render texture `id`.
    void missed(std::uint64_t id) noexcept;
    /// Every view's scene given `materials`.
    void replaceMaterials(const std::vector<SceneMaterial>& materials,
                          const std::vector<ScenePostProcessMaterial>& postProcesses);
    /// What the render textures were drawn with, if there are any.
    void summarize(diagnostics::Emitter& emitter) const;

private:
    /// What a texture drawn on demand last showed.
    struct Shown {
        world::EntityHandle entity;
        std::uint32_t request = 0;
    };
    struct TextureView {
        std::uint64_t id = 0;
        std::uint32_t width = 0;
        std::uint32_t height = 0;
        bool onDemand = false;
        std::optional<world::EntityHandle> entity;
        std::int32_t order = 0;
        std::uint32_t request = 0;
        bool tied = false;
        bool due = false;
        std::optional<Shown> shown;
        /// The frame last queued, kept by its scene until it queues
        /// another, and whether the device missed it.
        const SceneFrame* offered = nullptr;
        bool resend = false;
        std::unique_ptr<Scene> scene;
        SceneCamera camera;
        std::optional<execution::MonotonicInstant> presented;
    };
    std::optional<schema::ComponentTypeId> viewComponent_;
    std::optional<world::ColumnQuery> viewQuery_;
    std::vector<TextureView> views_;
    std::vector<TextureFrame> frames_;
    std::uint64_t drawn_ = 0;
    std::uint64_t kept_ = 0;
    std::uint64_t missed_ = 0;
    std::uint64_t leftOut_ = 0;
    std::uint64_t refused_ = 0;
};

} // namespace rawframe::render_scene
