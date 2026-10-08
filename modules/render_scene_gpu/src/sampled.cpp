#include "sampled.h"

namespace rawframe::render_scene_gpu {

std::vector<std::uint64_t> sampledTexturesOf(const render_scene::SceneFrame& scene) {
    std::vector<std::uint64_t> ids;
    // Each emitter's and ribbon's material's base and emission textures.
    std::vector<std::uint32_t> shown;
    for (const rawframe::particles::EmitterDraw& kEmitter : scene.particles.emitters) {
        shown.push_back(kEmitter.material);
    }
    for (const rawframe::particles::Ribbon& kRibbon : scene.particles.ribbons) {
        shown.push_back(kRibbon.material);
    }
    for (const std::uint32_t kMaterial : shown) {
        if (kMaterial >= scene.textures.size()) {
            continue;
        }
        const render_scene::SceneTextures& kTextures = scene.textures[kMaterial];
        for (const std::uint64_t kId : {kTextures.base.id, kTextures.emission.id}) {
            if (kId != 0) {
                ids.push_back(kId);
            }
        }
    }
    // Each post process's texture.
    for (const render_scene::ScenePostProcess& kProcess : scene.postProcesses) {
        if (kProcess.texture.id != 0) {
            ids.push_back(kProcess.texture.id);
        }
    }
    // Each decal's texture, and its normals'.
    for (const render_scene::SceneDecal& kDecal : scene.decals) {
        ids.push_back(kDecal.texture);
        if (kDecal.normal != 0) {
            ids.push_back(kDecal.normal);
        }
    }
    // Each draw's material's maps, its shadows' too.
    for (const std::vector<render_scene::SceneDraw>* kList :
         {&scene.draws, &scene.shadows.casters, &scene.lightShadows.casters}) {
        for (const render_scene::SceneDraw& draw : *kList) {
            if (draw.material >= scene.textures.size()) {
                continue;
            }
            const render_scene::SceneTextures& kTextures = scene.textures[draw.material];
            for (const std::uint64_t kId :
                 {kTextures.base.id, kTextures.packed.id, kTextures.emission.id, kTextures.normal.id}) {
                if (kId != 0) {
                    ids.push_back(kId);
                }
            }
        }
    }
    return ids;
}

} // namespace rawframe::render_scene_gpu
