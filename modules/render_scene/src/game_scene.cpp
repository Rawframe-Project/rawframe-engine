#include "rawframe/render_scene/errors.h"
#include "rawframe/render_scene/scene.h"
#include "rawframe/world_kest/game.h"
#include "rawframe/world_kest/layouts.h"

#include <cstddef>

namespace rawframe::render_scene {

using particles::Beam;
using particles::ParticleEmitter;
using particles::Trail;

namespace {

std::unexpected<result::Error> refuse(result::ErrorClass errorClass, RenderSceneError error, std::string_view why) {
    return std::unexpected<result::Error>{result::fail(errorClass, kRenderSceneDomain, code(error), why).error()};
}

} // namespace

result::Result<GameScene> loadGameScene(const world_kest::GameFiles& game, const kest::Program& program) {
    const world_kest::GameDescription& kDescription = game.description();
    GameScene loaded;
    const auto kOne = [](std::optional<schema::ComponentTypeId>& slot,
                         schema::ComponentTypeId id,
                         std::string_view why) -> result::Status {
        if (slot.has_value()) {
            return refuse(result::ErrorClass::InvalidArgument, RenderSceneError::BadComponents, why);
        }
        slot = id;
        return {};
    };
    for (const world_kest::GameComponent& component : kDescription.components) {
        if (world_kest::ofEngineType(component, "rawframe.model.Model")) {
            loaded.models.push_back(component.id);
        } else if (world_kest::ofEngineType(component, "rawframe.model.Camera")) {
            loaded.cameras.push_back(component.id);
        } else if (world_kest::ofEngineType(component, "rawframe.model.View")) {
            RAWFRAME_TRY(kOne(loaded.view, component.id, "a game has at most one view component"));
        } else if (world_kest::ofEngineType(component, "rawframe.model.AutoExposure")) {
            RAWFRAME_TRY(kOne(loaded.autoExposure, component.id, "a game has at most one auto-exposure: one view"));
        } else if (world_kest::ofEngineType(component, "rawframe.model.Grading")) {
            RAWFRAME_TRY(kOne(loaded.grading, component.id, "a game has at most one grading: one view"));
        } else if (world_kest::ofEngineType(component, "rawframe.model.ScreenSpaceReflections")) {
            RAWFRAME_TRY(
                kOne(loaded.reflections, component.id, "a game has at most one screen-space reflections: one view"));
        } else if (world_kest::ofEngineType(component, "rawframe.model.ContactShadows")) {
            RAWFRAME_TRY(kOne(loaded.contactShadows, component.id, "a game has at most one contact shadows: one view"));
        } else if (world_kest::ofEngineType(component, "rawframe.model.DepthOfField")) {
            RAWFRAME_TRY(kOne(loaded.depthOfField, component.id, "a game has at most one depth of field: one view"));
        } else if (world_kest::ofEngineType(component, "rawframe.model.MotionBlur")) {
            RAWFRAME_TRY(kOne(loaded.motionBlur, component.id, "a game has at most one motion blur: one view"));
        } else if (world_kest::ofEngineType(component, "rawframe.model.Bloom")) {
            RAWFRAME_TRY(kOne(loaded.bloom, component.id, "a game has at most one bloom: one view"));
        } else if (world_kest::ofEngineType(component, "rawframe.model.AmbientOcclusion")) {
            RAWFRAME_TRY(kOne(loaded.occlusion, component.id, "a game has at most one ambient occlusion: one view"));
        } else if (world_kest::ofEngineType(component, "rawframe.model.Sun")) {
            RAWFRAME_TRY(kOne(loaded.sun, component.id, "a game has at most one sun"));
        } else if (world_kest::ofEngineType(component, "rawframe.model.Sky")) {
            RAWFRAME_TRY(kOne(loaded.sky, component.id, "a game has at most one sky"));
        } else if (world_kest::ofEngineType(component, "rawframe.model.PointLight")) {
            loaded.points.push_back(component.id);
        } else if (world_kest::ofEngineType(component, "rawframe.model.SpotLight")) {
            loaded.spots.push_back(component.id);
        } else if (world_kest::ofEngineType(component, "rawframe.model.ReflectionProbe")) {
            loaded.probes.push_back(component.id);
        } else if (world_kest::ofEngineType(component, "rawframe.model.Decal")) {
            loaded.decals.push_back(component.id);
        } else if (world_kest::ofEngineType(component, "rawframe.model.PostProcess")) {
            loaded.postProcesses.push_back(component.id);
        } else if (world_kest::ofEngineType(component, particles::kEmitterType)) {
            loaded.emitters.push_back(component.id);
        } else if (world_kest::ofEngineType(component, particles::kTrailType)) {
            loaded.trails.push_back(component.id);
        } else if (world_kest::ofEngineType(component, particles::kBeamType)) {
            loaded.beams.push_back(component.id);
        }
    }
    if (loaded.models.empty()) {
        return refuse(result::ErrorClass::NotFound, RenderSceneError::NoModels, "the game declares no models");
    }
    // By their full names: a game's own type called `Model` is not this one.
    const auto kLaidOut =
        [&program](bool declared,
                   std::string_view type,
                   std::size_t size,
                   std::initializer_list<std::pair<std::string_view, std::size_t>> fields) -> result::Status {
        if (declared && !world_kest::laidOutAs(program, type, size, fields)) {
            return refuse(result::ErrorClass::InvalidArgument,
                          RenderSceneError::BadComponents,
                          "the program lays out a rawframe.model type otherwise than this engine reads it");
        }
        return {};
    };
    RAWFRAME_TRY(kLaidOut(true,
                          "rawframe.model.Model",
                          sizeof(Model),
                          {{"mesh", offsetof(Model, mesh)},
                           {"scaleX", offsetof(Model, scaleX)},
                           {"scaleY", offsetof(Model, scaleY)},
                           {"scaleZ", offsetof(Model, scaleZ)},
                           {"color", offsetof(Model, color)},
                           {"material", offsetof(Model, material)},
                           {"lift", offsetof(Model, lift)}}));
    RAWFRAME_TRY(kLaidOut(!loaded.cameras.empty(),
                          "rawframe.model.Camera",
                          sizeof(Camera),
                          {{"offsetX", offsetof(Camera, offsetX)},
                           {"offsetY", offsetof(Camera, offsetY)},
                           {"offsetZ", offsetof(Camera, offsetZ)},
                           {"yaw", offsetof(Camera, yaw)},
                           {"pitch", offsetof(Camera, pitch)},
                           {"fovY", offsetof(Camera, fovY)},
                           {"near", offsetof(Camera, near)},
                           {"exposure", offsetof(Camera, exposure)},
                           {"tonemapper", offsetof(Camera, tonemapper)}}));
    RAWFRAME_TRY(kLaidOut(
        loaded.view.has_value(),
        "rawframe.model.View",
        sizeof(View),
        {{"target", offsetof(View, target)}, {"order", offsetof(View, order)}, {"request", offsetof(View, request)}}));
    RAWFRAME_TRY(kLaidOut(loaded.autoExposure.has_value(),
                          "rawframe.model.AutoExposure",
                          sizeof(AutoExposure),
                          {{"minimum", offsetof(AutoExposure, minimum)},
                           {"maximum", offsetof(AutoExposure, maximum)},
                           {"brighten", offsetof(AutoExposure, brighten)},
                           {"darken", offsetof(AutoExposure, darken)},
                           {"compensation", offsetof(AutoExposure, compensation)},
                           {"low", offsetof(AutoExposure, low)},
                           {"high", offsetof(AutoExposure, high)},
                           {"centered", offsetof(AutoExposure, centered)}}));
    RAWFRAME_TRY(kLaidOut(
        loaded.bloom.has_value(), "rawframe.model.Bloom", sizeof(Bloom), {{"intensity", offsetof(Bloom, intensity)}}));
    RAWFRAME_TRY(kLaidOut(loaded.reflections.has_value(),
                          "rawframe.model.ScreenSpaceReflections",
                          sizeof(ScreenSpaceReflections),
                          {{"distance", offsetof(ScreenSpaceReflections, distance)}}));
    RAWFRAME_TRY(kLaidOut(loaded.motionBlur.has_value(),
                          "rawframe.model.MotionBlur",
                          sizeof(MotionBlur),
                          {{"shutter", offsetof(MotionBlur, shutter)}}));
    RAWFRAME_TRY(kLaidOut(loaded.depthOfField.has_value(),
                          "rawframe.model.DepthOfField",
                          sizeof(DepthOfField),
                          {{"focus", offsetof(DepthOfField, focus)}, {"aperture", offsetof(DepthOfField, aperture)}}));
    RAWFRAME_TRY(kLaidOut(loaded.contactShadows.has_value(),
                          "rawframe.model.ContactShadows",
                          sizeof(ContactShadows),
                          {{"length", offsetof(ContactShadows, length)}}));
    RAWFRAME_TRY(kLaidOut(
        loaded.occlusion.has_value(),
        "rawframe.model.AmbientOcclusion",
        sizeof(AmbientOcclusion),
        {{"radius", offsetof(AmbientOcclusion, radius)}, {"intensity", offsetof(AmbientOcclusion, intensity)}}));
    RAWFRAME_TRY(kLaidOut(loaded.grading.has_value(),
                          "rawframe.model.Grading",
                          sizeof(Grading),
                          {{"slopeR", offsetof(Grading, slopeR)},
                           {"slopeG", offsetof(Grading, slopeG)},
                           {"slopeB", offsetof(Grading, slopeB)},
                           {"offsetR", offsetof(Grading, offsetR)},
                           {"offsetG", offsetof(Grading, offsetG)},
                           {"offsetB", offsetof(Grading, offsetB)},
                           {"powerR", offsetof(Grading, powerR)},
                           {"powerG", offsetof(Grading, powerG)},
                           {"powerB", offsetof(Grading, powerB)},
                           {"saturation", offsetof(Grading, saturation)},
                           {"contrast", offsetof(Grading, contrast)},
                           {"temperature", offsetof(Grading, temperature)},
                           {"tint", offsetof(Grading, tint)},
                           {"table", offsetof(Grading, table)}}));
    RAWFRAME_TRY(kLaidOut(loaded.sun.has_value(),
                          "rawframe.model.Sun",
                          sizeof(Sun),
                          {{"directionX", offsetof(Sun, directionX)},
                           {"directionY", offsetof(Sun, directionY)},
                           {"directionZ", offsetof(Sun, directionZ)},
                           {"illuminance", offsetof(Sun, illuminance)},
                           {"color", offsetof(Sun, color)},
                           {"angle", offsetof(Sun, angle)}}));
    RAWFRAME_TRY(kLaidOut(loaded.sky.has_value(),
                          "rawframe.model.Sky",
                          sizeof(Sky),
                          {{"luminance", offsetof(Sky, luminance)},
                           {"color", offsetof(Sky, color)},
                           {"ground", offsetof(Sky, ground)},
                           {"environment", offsetof(Sky, environment)}}));
    RAWFRAME_TRY(kLaidOut(!loaded.points.empty(),
                          "rawframe.model.PointLight",
                          sizeof(PointLight),
                          {{"lumens", offsetof(PointLight, lumens)},
                           {"range", offsetof(PointLight, range)},
                           {"color", offsetof(PointLight, color)},
                           {"shadows", offsetof(PointLight, shadows)}}));
    RAWFRAME_TRY(kLaidOut(!loaded.spots.empty(),
                          "rawframe.model.SpotLight",
                          sizeof(SpotLight),
                          {{"lumens", offsetof(SpotLight, lumens)},
                           {"range", offsetof(SpotLight, range)},
                           {"inner", offsetof(SpotLight, inner)},
                           {"outer", offsetof(SpotLight, outer)},
                           {"color", offsetof(SpotLight, color)},
                           {"shadows", offsetof(SpotLight, shadows)}}));
    RAWFRAME_TRY(kLaidOut(!loaded.probes.empty(),
                          "rawframe.model.ReflectionProbe",
                          sizeof(ReflectionProbe),
                          {{"halfX", offsetof(ReflectionProbe, halfX)},
                           {"halfY", offsetof(ReflectionProbe, halfY)},
                           {"halfZ", offsetof(ReflectionProbe, halfZ)},
                           {"intensity", offsetof(ReflectionProbe, intensity)},
                           {"priority", offsetof(ReflectionProbe, priority)},
                           {"environment", offsetof(ReflectionProbe, environment)}}));
    RAWFRAME_TRY(kLaidOut(!loaded.decals.empty(),
                          "rawframe.model.Decal",
                          sizeof(Decal),
                          {{"halfX", offsetof(Decal, halfX)},
                           {"halfY", offsetof(Decal, halfY)},
                           {"halfZ", offsetof(Decal, halfZ)},
                           {"color", offsetof(Decal, color)},
                           {"texture", offsetof(Decal, texture)},
                           {"normal", offsetof(Decal, normal)},
                           {"roughness", offsetof(Decal, roughness)}}));
    RAWFRAME_TRY(kLaidOut(!loaded.postProcesses.empty(),
                          "rawframe.model.PostProcess",
                          sizeof(PostProcess),
                          {{"material", offsetof(PostProcess, material)}, {"weight", offsetof(PostProcess, weight)}}));
    // The particle triad as its own module reads it (D357).
    for (const auto& [kDeclared, kType, kSize, kFields] :
         {std::tuple{
              !loaded.emitters.empty(), particles::kEmitterType, sizeof(ParticleEmitter), particles::emitterFields()},
          std::tuple{!loaded.trails.empty(), particles::kTrailType, sizeof(Trail), particles::trailFields()},
          std::tuple{!loaded.beams.empty(), particles::kBeamType, sizeof(Beam), particles::beamFields()}}) {
        if (kDeclared && !world_kest::laidOutAs(program, kType, kSize, kFields)) {
            return refuse(result::ErrorClass::InvalidArgument,
                          RenderSceneError::BadComponents,
                          "the program lays out a rawframe.model type otherwise than this engine reads it");
        }
    }
    for (const physics3d::BodyMesh& kMesh : game.meshes()) {
        loaded.meshes.push_back(SceneMesh{.id = kMesh.id, .mesh = kMesh.mesh});
    }
    return loaded;
}

} // namespace rawframe::render_scene
