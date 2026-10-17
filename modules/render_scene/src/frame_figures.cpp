#include "frame_figures.h"

#include <cstdint>
#include <span>

namespace rawframe::render_scene {

namespace {

/// The draws of `draws` as steps of `pass`, a run sharing a mesh and a
/// material one step; what is past the limit counted in `leftOut`.
void stepsOf(std::string_view pass,
             std::span<const SceneDraw> draws,
             std::vector<view::FrameStep>& steps,
             std::int64_t& leftOut) {
    for (std::size_t at = 0; at < draws.size();) {
        std::size_t end = at + 1;
        while (end < draws.size() && draws[end].mesh == draws[at].mesh && draws[end].material == draws[at].material) {
            ++end;
        }
        if (steps.size() < view::kMostFrameSteps) {
            steps.push_back(view::FrameStep{.pass = pass,
                                            .mesh = draws[at].mesh,
                                            .material = draws[at].material,
                                            .instances = static_cast<std::uint32_t>(end - at)});
        } else {
            ++leftOut;
        }
        at = end;
    }
}

void stepOf(view::FrameStep step, std::vector<view::FrameStep>& steps, std::int64_t& leftOut) {
    if (step.instances == 0) {
        return;
    }
    if (steps.size() < view::kMostFrameSteps) {
        steps.push_back(step);
    } else {
        ++leftOut;
    }
}

std::int64_t counted(std::size_t count) noexcept {
    return static_cast<std::int64_t>(count);
}

} // namespace

void describeFrame(const SceneFrame& frame,
                   std::vector<view::FrameFigure>& figures,
                   std::vector<view::FrameStep>& steps) {
    steps.clear();
    std::int64_t leftOut = 0;
    // The passes in the order a device draws them: the shadow maps, the
    // opaque models, the translucent ones farthest first, then decals,
    // particles, and the post processes.
    stepsOf("shadow_cascade", frame.shadows.casters, steps, leftOut);
    stepsOf("light_shadow", frame.lightShadows.casters, steps, leftOut);
    const std::size_t kOpaque = frame.draws.size() - frame.translucent;
    const std::size_t kBeforeOpaque = steps.size();
    stepsOf("opaque", std::span{frame.draws}.first(kOpaque), steps, leftOut);
    const std::size_t kOpaqueSteps = steps.size() - kBeforeOpaque;
    stepsOf("translucent", std::span{frame.draws}.subspan(kOpaque), steps, leftOut);
    stepOf({.pass = "decals", .instances = static_cast<std::uint32_t>(frame.decals.size())}, steps, leftOut);
    for (const particles::EmitterDraw& kEmitter : frame.particles.emitters) {
        stepOf({.pass = "particles", .material = kEmitter.material, .instances = kEmitter.capacity}, steps, leftOut);
    }
    stepOf(
        {.pass = "ribbons", .instances = static_cast<std::uint32_t>(frame.particles.ribbons.size())}, steps, leftOut);
    stepOf({.pass = "post_processes", .instances = static_cast<std::uint32_t>(frame.postProcesses.size())},
           steps,
           leftOut);

    figures = {
        {.name = "draws", .value = counted(frame.draws.size())},
        {.name = "opaque", .value = counted(kOpaque)},
        {.name = "translucent", .value = counted(frame.translucent)},
        {.name = "opaque_batches", .value = counted(kOpaqueSteps)},
        {.name = "drawn", .value = counted(frame.drawn)},
        {.name = "culled", .value = counted(frame.culled)},
        {.name = "hidden", .value = counted(frame.hidden)},
        {.name = "malformed", .value = counted(frame.malformed)},
        {.name = "unknown_meshes", .value = counted(frame.unknownMeshes)},
        {.name = "unknown_materials", .value = counted(frame.unknownMaterials)},
        {.name = "lights", .value = counted(frame.lights3d.size())},
        {.name = "lights_culled", .value = counted(frame.lightsCulled)},
        {.name = "lights_over_limit", .value = counted(frame.lightsOverLimit)},
        {.name = "cluster_overflow", .value = counted(frame.clusterOverflow)},
        {.name = "shadow_cascades", .value = counted(frame.shadows.count)},
        {.name = "cascade_casters", .value = counted(frame.shadows.casters.size())},
        {.name = "light_shadow_slots", .value = counted(frame.lightShadows.slots.size())},
        {.name = "light_shadow_casters", .value = counted(frame.lightShadows.casters.size())},
        {.name = "probes", .value = counted(frame.probes.size())},
        {.name = "probes_over_limit", .value = counted(frame.probesOverLimit)},
        {.name = "decals", .value = counted(frame.decals.size())},
        {.name = "decals_culled", .value = counted(frame.decalsCulled)},
        {.name = "decals_over_limit", .value = counted(frame.decalsOverLimit)},
        {.name = "emitters", .value = counted(frame.particles.emitters.size())},
        {.name = "emitters_left_out", .value = counted(frame.particles.emittersLeftOut)},
        {.name = "particles_left_out", .value = counted(frame.particles.particlesLeftOut)},
        {.name = "ribbons", .value = counted(frame.particles.ribbons.size())},
        {.name = "post_processes", .value = counted(frame.postProcesses.size())},
        {.name = "post_processes_left_out", .value = counted(frame.postProcessesLeftOut)},
        {.name = "samples", .value = frame.samples},
        {.name = "fxaa", .value = frame.fxaa ? 1 : 0},
        {.name = "temporal", .value = frame.temporal.enabled ? 1 : 0},
        {.name = "occlusion", .value = frame.occlusion.enabled ? 1 : 0},
        {.name = "bloom", .value = frame.bloom.enabled ? 1 : 0},
        {.name = "reflections", .value = frame.reflections.enabled ? 1 : 0},
        {.name = "motion_blur", .value = frame.motionBlur.enabled ? 1 : 0},
        {.name = "depth_of_field", .value = frame.depthOfField.enabled ? 1 : 0},
        {.name = "contact_shadows", .value = frame.contactShadows.enabled ? 1 : 0},
        {.name = "steps", .value = counted(steps.size())},
        {.name = "steps_left_out", .value = leftOut},
    };
}

} // namespace rawframe::render_scene
