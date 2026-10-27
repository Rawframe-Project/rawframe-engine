#include "cameras.h"

#include "rawframe/physics3d/components.h"

namespace rawframe::render_scene {

namespace {

/// A client's view without a camera of its own: behind its player and
/// above, looking a little down, in a sunny day's exposure.
constexpr Camera kDefaultCamera{
    .offsetX = 0, .offsetY = 3, .offsetZ = 6, .yaw = 0, .pitch = -0.35F, .fovY = 1, .near = 0.1F, .exposure = 15};

} // namespace

GameCameras::GameCameras(const GameScene& game)
    : cameras_(game.cameras), autoExposure_(game.autoExposure), grading_(game.grading), occlusion_(game.occlusion),
      bloom_(game.bloom), reflections_(game.reflections), motionBlur_(game.motionBlur),
      depthOfField_(game.depthOfField), contactShadows_(game.contactShadows), postProcesses_(game.postProcesses) {
}

bool GameCameras::read(world::World& world, world::EntityHandle entity, SceneCamera& camera) const {
    Camera view = kDefaultCamera;
    bool found = false;
    // The first of the game's camera components the entity has (a
    // player's and its views' may differ).
    for (const schema::ComponentTypeId kComponent : cameras_) {
        if (const auto kCamera = world.registry().find(kComponent)) {
            if (const auto* placed = static_cast<const Camera*>(world.getErased(entity, *kCamera))) {
                view = *placed;
                found = true;
                break;
            }
        }
    }
    camera.yaw = view.yaw;
    camera.pitch = view.pitch;
    camera.fovY = view.fovY;
    camera.near = view.near;
    camera.exposure = view.exposure;
    camera.tonemapper = view.tonemapper;
    const auto kRead = [&world, entity]<typename T>(const std::optional<schema::ComponentTypeId>& component,
                                                    std::optional<T>& into) {
        into.reset();
        if (component.has_value()) {
            if (const auto kId = world.registry().find(*component)) {
                if (const auto* asked = static_cast<const T*>(world.getErased(entity, *kId))) {
                    into = *asked;
                }
            }
        }
    };
    kRead(grading_, camera.grading);
    kRead(reflections_, camera.reflections);
    kRead(motionBlur_, camera.motionBlur);
    kRead(depthOfField_, camera.depthOfField);
    kRead(contactShadows_, camera.contactShadows);
    kRead(bloom_, camera.bloom);
    kRead(occlusion_, camera.occlusion);
    kRead(autoExposure_, camera.metering);
    // Its post processes, in the game's order.
    camera.postProcesses.clear();
    for (const schema::ComponentTypeId kComponent : postProcesses_) {
        if (const auto kProcess = world.registry().find(kComponent)) {
            if (const auto* asked = static_cast<const PostProcess*>(world.getErased(entity, *kProcess))) {
                camera.postProcesses.push_back(*asked);
            }
        }
    }
    const auto kPose = world.registry().key<physics3d::Pose3D>();
    if (const auto kEye = eyeOf(view, kPose ? world.get(entity, *kPose) : nullptr)) {
        camera.eye = *kEye;
    }
    return found;
}

} // namespace rawframe::render_scene
