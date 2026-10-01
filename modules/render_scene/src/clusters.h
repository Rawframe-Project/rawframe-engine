#pragma once

#include "rawframe/render_scene/scene.h"
#include "space.h"

#include <array>
#include <cstdint>
#include <vector>

namespace rawframe::render_scene {

/// The view's lens as the view stage made it: whether it sees at all, half
/// its height's angle, its width over its height, and its near plane.
struct ViewShape {
    bool sees = false;
    float half = 0.5F;
    float aspect = 1;
    float near = 0.1F;
};

/// ADR-0051's cluster item classes (D339): punctual lights and decals, in
/// the order a cluster holds them.
enum class ClusterItem : std::uint32_t {
    Light = 0,
    Decal = 1
};

/// An item a cluster names: the cluster, the item's class, and its place
/// among the frame's items of that class.
struct ClusterName {
    std::uint32_t cluster = 0;
    ClusterItem item = ClusterItem::Light;
    std::uint32_t index = 0;
};

/// Names `index` of class `item` in every cluster a sphere `radius` meters
/// about `center` (relative to the eye) may reach: the slices its depth
/// spans, and the tiles between the lines from the eye that touch it; one
/// about the eye covers every tile.
void nameSphere(const SceneClusters& clusters,
                const std::array<Vector, 3>& axes,
                const ViewShape& view,
                const Vector& center,
                float radius,
                ClusterItem item,
                std::uint32_t index,
                std::vector<ClusterName>& named);

/// The clusters' ranges and indices from `named` (D339): each cluster's
/// lights, then its decals, each in the order they were named, at most the
/// limit of each class; the rest are counted in `frame.clusterOverflow`.
void packClusters(SceneFrame& frame, std::vector<ClusterName>& named, const SceneLimits& limits);

} // namespace rawframe::render_scene
