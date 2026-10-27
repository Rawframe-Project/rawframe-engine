#pragma once

// The cameras a game's entities carry, read as the scene draws through
// them: a player's or a render texture's view's, its effects and
// post processes, and its eye where its pose places it.

#include "rawframe/render_scene/scene.h"

#include <optional>
#include <vector>

namespace rawframe::render_scene {

/// The game's camera components and their effects', and how an entity's
/// are read. Reads none before it is made from a game's scene.
class GameCameras {
public:
    GameCameras() = default;
    explicit GameCameras(const GameScene& game);

    /// The camera on `entity` and its effects read into `camera`, the eye
    /// placed by the entity's pose, or in the World where it is fixed;
    /// whether it has a camera (else it looks as the default
    /// does).
    [[nodiscard]] bool read(world::World& world, world::EntityHandle entity, SceneCamera& camera) const;

private:
    std::vector<schema::ComponentTypeId> cameras_;
    std::optional<schema::ComponentTypeId> autoExposure_;
    std::optional<schema::ComponentTypeId> grading_;
    std::optional<schema::ComponentTypeId> occlusion_;
    std::optional<schema::ComponentTypeId> bloom_;
    std::optional<schema::ComponentTypeId> reflections_;
    std::optional<schema::ComponentTypeId> motionBlur_;
    std::optional<schema::ComponentTypeId> depthOfField_;
    std::optional<schema::ComponentTypeId> contactShadows_;
    std::vector<schema::ComponentTypeId> postProcesses_;
};

} // namespace rawframe::render_scene
