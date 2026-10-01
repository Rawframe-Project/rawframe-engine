#pragma once

// The view stage's decals (ADR-0051, D339): which of the extracted decals a
// frame keeps, how a device places each, and the clusters each names.

#include "clusters.h"
#include "rawframe/render_scene/scene.h"
#include "space.h"

#include <array>
#include <span>
#include <vector>

namespace rawframe::render_scene {

/// Each sound decal (a positive, finite box, a texture) whose bounding
/// sphere reaches the view, in its entity's order, into `frame.decals`,
/// at most the limit, and into `named` for every cluster that sphere may
/// reach; the rest counted.
void clusterDecals(SceneFrame& frame,
                   std::span<const DecalInstance> decals,
                   const SceneCamera& camera,
                   const std::array<Vector, 3>& axes,
                   const ViewShape& view,
                   const SceneLimits& limits,
                   std::vector<ClusterName>& named);

} // namespace rawframe::render_scene
