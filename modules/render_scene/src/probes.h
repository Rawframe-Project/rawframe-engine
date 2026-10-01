#pragma once

// The view stage's reflection probes (ADR-0051, D325, D340): which of the
// extracted probes a frame keeps, in the order a point takes them, and the
// clusters each names.

#include "clusters.h"
#include "rawframe/render_scene/scene.h"
#include "space.h"

#include <array>
#include <span>
#include <vector>

namespace rawframe::render_scene {

/// Keeps the usable probes (a positive, finite box and an environment)
/// whose boxes reach the view, the nearest to the eye first, at most the
/// limit, placed relative to the eye and ordered as a point takes them:
/// the highest priority first, then the smaller box, then the nearer; and
/// names each in every cluster the sphere about its box grown by a tenth,
/// across which a point's reflection fades out of it, may reach.
void clusterProbes(SceneFrame& frame,
                   std::span<const ProbeInstance> probes,
                   const SceneCamera& camera,
                   const std::array<Vector, 3>& axes,
                   const ViewShape& view,
                   const SceneLimits& limits,
                   std::vector<ClusterName>& named);

} // namespace rawframe::render_scene
