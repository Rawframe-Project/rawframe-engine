#include "decals.h"

#include <algorithm>
#include <cmath>

namespace rawframe::render_scene {

void clusterDecals(SceneFrame& frame,
                   std::span<const DecalInstance> decals,
                   const SceneCamera& camera,
                   const std::array<Vector, 3>& axes,
                   const ViewShape& view,
                   const SceneLimits& limits,
                   std::vector<ClusterName>& named) {
    frame.decals.clear();
    frame.decalsCulled = 0;
    frame.decalsOverLimit = 0;
    std::vector<const DecalInstance*> ordered;
    for (const DecalInstance& decal : decals) {
        ordered.push_back(&decal);
    }
    std::ranges::sort(ordered, {}, &DecalInstance::entity);
    for (const DecalInstance* instance : ordered) {
        const Decal& kDecal = instance->decal;
        const std::array<float, 3> kHalf = {kDecal.halfX, kDecal.halfY, kDecal.halfZ};
        const bool kSound = kDecal.texture != 0 && std::isfinite(kDecal.roughness) &&
                            std::ranges::all_of(kHalf,
                                                [](float half) {
                                                    return std::isfinite(half) && half > 0;
                                                }) &&
                            std::ranges::all_of(instance->position,
                                                [](double value) {
                                                    return std::isfinite(value);
                                                }) &&
                            std::ranges::all_of(instance->rotation, [](float value) {
                                return std::isfinite(value);
                            });
        const Vector kCenter = {static_cast<float>(instance->position[0] - camera.eye[0]),
                                static_cast<float>(instance->position[1] - camera.eye[1]),
                                static_cast<float>(instance->position[2] - camera.eye[2])};
        const float kRadius = std::sqrt((kHalf[0] * kHalf[0]) + (kHalf[1] * kHalf[1]) + (kHalf[2] * kHalf[2]));
        if (!view.sees || !kSound || outsideView(axes, view, kCenter, kRadius)) {
            ++frame.decalsCulled;
            continue;
        }
        if (frame.decals.size() == limits.maximumDecals) {
            ++frame.decalsOverLimit;
            continue;
        }
        // Into its box: its turn undone, then each axis over its half; a
        // pose that does not turn is no turn.
        const bool kTurned = std::ranges::any_of(instance->rotation, [](float value) {
            return value != 0;
        });
        const std::array<Vector, 3> kTurn = turnOf(kTurned ? instance->rotation : std::array<float, 4>{0, 0, 0, 1});
        SceneDecal made{.color = {},
                        .texture = kDecal.texture,
                        .normal = kDecal.normal,
                        .roughness = std::clamp(kDecal.roughness, 0.0F, 1.0F)};
        for (std::size_t row = 0; row < 3; ++row) {
            float moved = 0;
            for (std::size_t column = 0; column < 3; ++column) {
                made.toBox[(column * 4) + row] = kTurn[row][column] / kHalf[row];
                moved += made.toBox[(column * 4) + row] * kCenter[column];
            }
            made.toBox[12 + row] = -moved;
        }
        made.toBox[15] = 1;
        const Vector kColor = colorOf(kDecal.color);
        made.color = {kColor[0], kColor[1], kColor[2], static_cast<float>(kDecal.color & 0xFFU) / 255.0F};
        const auto kIndex = static_cast<std::uint32_t>(frame.decals.size());
        frame.decals.push_back(made);
        nameSphere(frame.clusters, axes, view, kCenter, kRadius, ClusterItem::Decal, kIndex, named);
    }
}

} // namespace rawframe::render_scene
