#pragma once

// The view stage's trails and beams (ADR-0053, D354): the ribbons a frame
// draws, farthest first, and their points.

#include "clusters.h"
#include "rawframe/render_scene/scene.h"
#include "space.h"

#include <array>
#include <cstdint>
#include <deque>
#include <map>
#include <span>
#include <utility>

namespace rawframe::render_scene {

/// A point a trail has left on a client: where in the World, and when on
/// the particle clock (seconds, before wrapping).
struct TrailPoint {
    std::array<double, 3> position{};
    double left = 0;
};

/// What a trail keeps from frame to frame on a client (D354): the points
/// it has left and still holds, oldest first.
struct TrailHistory {
    std::deque<TrailPoint> points;
};

/// A trail's identity across frames: its entity and which of the game's
/// trail components it is.
using TrailKey = std::pair<world::EntityHandle, std::uint32_t>;

/// The ribbons a frame draws (D354), on the particle clock `clock`
/// (seconds, before wrapping). Every sound trail leaves a point where its
/// pose has gone its spacing from the last, and lets go of those past its
/// life, seen or not; a trail runs from its pose through what it holds, a
/// beam along its curve. Those whose points meet the view are kept, the
/// nearest up to the limit, drawn farthest first, each held to the limit
/// points. `histories` keeps what each trail keeps, dropped once its trail
/// is gone.
void makeRibbons(SceneFrame& frame,
                 std::span<const TrailInstance> trails,
                 std::span<const BeamInstance> beams,
                 const SceneCamera& camera,
                 const std::array<Vector, 3>& axes,
                 const ViewShape& view,
                 const std::map<std::uint64_t, std::uint32_t>& materials,
                 const SceneLimits& limits,
                 double clock,
                 std::map<TrailKey, TrailHistory>& histories);

} // namespace rawframe::render_scene
