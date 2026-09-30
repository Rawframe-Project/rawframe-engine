#include "runs.h"

#include <algorithm>
#include <span>
#include <utility>

namespace rawframe::render_scene_gpu {

namespace {

/// The draws' placements, and their runs; a shadow's casters' by mesh
/// alone (`materialTextures` none).
void append(std::span<const render_scene::SceneDraw> draws,
            const std::map<std::uint64_t, const HeldMesh*>& usable,
            std::span<const render_scene::SceneTextures> materialTextures,
            Placed& placed,
            Runs& runs,
            std::uint32_t& count,
            bool counted,
            std::uint64_t& modelsLeftOut) {
    for (const render_scene::SceneDraw& draw : draws) {
        const auto kMesh = usable.find(draw.mesh);
        if (kMesh == usable.end()) {
            modelsLeftOut += counted ? 1 : 0;
            continue;
        }
        // The model's rows, then the normals' columns, then the color.
        for (std::size_t row = 0; row < 3; ++row) {
            for (std::size_t column = 0; column < 4; ++column) {
                placed.instances.push_back(draw.model[(column * 4) + row]);
            }
        }
        for (std::size_t column = 0; column < 3; ++column) {
            for (std::size_t row = 0; row < 3; ++row) {
                placed.instances.push_back(draw.normal[(column * 4) + row]);
            }
        }
        placed.instances.insert(placed.instances.end(), draw.color.begin(), draw.color.end());
        for (std::size_t row = 0; row < 3; ++row) {
            for (std::size_t column = 0; column < 4; ++column) {
                placed.instances.push_back(draw.previous[(column * 4) + row]);
            }
        }
        placed.instances.push_back(static_cast<float>(draw.material));
        placed.instances.push_back(static_cast<float>(draw.probe));
        const render_scene::SceneTextures kTexture =
            draw.material < materialTextures.size() ? materialTextures[draw.material] : render_scene::SceneTextures{};
        const auto kIndices = static_cast<std::uint32_t>(kMesh->second->source->indices.size());
        const std::uint32_t kCount = draw.indexCount != 0 ? draw.indexCount : kIndices - draw.firstIndex;
        if (runs.empty() || runs.back().mesh != kMesh->second || runs.back().firstIndex != draw.firstIndex ||
            runs.back().indexCount != kCount || runs.back().texture != kTexture || runs.back().probe != draw.probe) {
            runs.push_back({.mesh = kMesh->second,
                            .firstIndex = draw.firstIndex,
                            .indexCount = kCount,
                            .first = count,
                            .count = 0,
                            .texture = kTexture,
                            .probe = draw.probe});
        }
        ++runs.back().count;
        ++count;
    }
}

} // namespace

Placed placeDraws(const render_scene::SceneFrame& scene,
                  const std::map<std::uint64_t, const HeldMesh*>& usable,
                  std::uint64_t& modelsLeftOut) {
    Placed placed;
    std::uint32_t count = 0;
    const std::span<const render_scene::SceneDraw> kDraws = scene.draws;
    const std::size_t kOpaque = kDraws.size() - std::min(scene.translucent, kDraws.size());
    // The opaque, then the masked: a material with a cutoff (D310).
    const auto kSplit = [&scene](std::span<const render_scene::SceneDraw> draws) {
        std::pair<std::vector<render_scene::SceneDraw>, std::vector<render_scene::SceneDraw>> split;
        for (const render_scene::SceneDraw& draw : draws) {
            const bool kMasked = draw.material < scene.materials.size() && scene.materials[draw.material][14] > 0;
            (kMasked ? split.second : split.first).push_back(draw);
        }
        return split;
    };
    const auto [kSolid, kMasked] = kSplit(kDraws.first(kOpaque));
    append(kSolid, usable, scene.textures, placed, placed.runs, count, true, modelsLeftOut);
    append(kMasked, usable, scene.textures, placed, placed.maskedRuns, count, true, modelsLeftOut);
    append(kDraws.subspan(kOpaque), usable, scene.textures, placed, placed.translucentRuns, count, true, modelsLeftOut);
    const std::span<const render_scene::SceneDraw> kSunCasters = scene.shadows.casters;
    for (std::size_t at = 0; at < scene.shadows.count && at < placed.cascadeRuns.size(); ++at) {
        const render_scene::ShadowCascade& kCascade = scene.shadows.cascades[at];
        const auto [kCastSolid, kCastMasked] = kSplit(kSunCasters.subspan(kCascade.firstCaster, kCascade.casterCount));
        append(kCastSolid, usable, {}, placed, placed.cascadeRuns[at].solid, count, false, modelsLeftOut);
        append(kCastMasked, usable, scene.textures, placed, placed.cascadeRuns[at].masked, count, false, modelsLeftOut);
    }
    const std::span<const render_scene::SceneDraw> kCasters = scene.lightShadows.casters;
    for (const render_scene::ShadowSlot& slot : scene.lightShadows.slots) {
        const auto [kCastSolid, kCastMasked] = kSplit(kCasters.subspan(slot.firstCaster, slot.casterCount));
        Casters& casters = placed.slotRuns.emplace_back();
        append(kCastSolid, usable, {}, placed, casters.solid, count, false, modelsLeftOut);
        append(kCastMasked, usable, scene.textures, placed, casters.masked, count, false, modelsLeftOut);
    }
    return placed;
}

} // namespace rawframe::render_scene_gpu
