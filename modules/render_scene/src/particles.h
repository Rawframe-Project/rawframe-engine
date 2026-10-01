#pragma once

// The view stage's particle emitters (ADR-0053, D352): which of the
// extracted emitters a frame draws, farthest first, and what each spawns.

#include "clusters.h"
#include "rawframe/render_scene/scene.h"
#include "space.h"

#include <array>
#include <cstdint>
#include <map>
#include <span>
#include <utility>

namespace rawframe::render_scene {

/// What an emitter keeps from frame to frame on a client: its anchor in
/// the World, the bursts it has seen, the particles it owes (a fraction),
/// its ring's size and where its next spawn starts in it, and the frame it
/// was last drawn in.
struct EmitterHistory {
    std::array<double, 3> anchor{};
    std::uint32_t bursts = 0;
    double owed = 0;
    std::uint32_t capacity = 0;
    std::uint32_t next = 0;
    std::uint64_t drawn = 0;
};

/// An emitter's identity across frames: its entity and which of the
/// game's emitter components it is.
using EmitterKey = std::pair<world::EntityHandle, std::uint32_t>;

/// The emitters a frame draws (D352): those sound, with particles to show,
/// whose reach meets the view, the nearest kept up to the limit and drawn
/// farthest first, each held to the limit points; and what each spawns on
/// the particle clock `clock` (seconds, before wrapping), `elapsed` seconds
/// after the frame before, the `frame`th: steadily at its rate (an emitter
/// not drawn the frame before owes nothing for the frames it was not), and
/// its burst for each time its `bursts` moved since the frame before.
/// `histories` keeps what each emitter keeps, dropped once its emitter is
/// gone.
void spawnParticles(SceneFrame& frame,
                    std::span<const EmitterInstance> emitters,
                    const SceneCamera& camera,
                    const std::array<Vector, 3>& axes,
                    const ViewShape& view,
                    const std::map<std::uint64_t, std::uint32_t>& materials,
                    const SceneLimits& limits,
                    double clock,
                    float elapsed,
                    std::uint64_t frameIndex,
                    std::map<EmitterKey, EmitterHistory>& histories);

} // namespace rawframe::render_scene
