#pragma once

// A frame's draws placed for the device: each draw's instance data, in
// order, and the runs of one mesh, one material's program, and its textures
// each, an
// instanced draw apiece from its first instance; the opaque, masked, and
// translucent draws', and each shadow's casters'.

#include "meshes.h"
#include "rawframe/render_scene/scene.h"

#include <array>
#include <cstdint>
#include <map>
#include <vector>

namespace rawframe::render_scene_gpu {

struct Run {
    const HeldMesh* mesh = nullptr;
    /// The mesh's indices it draws (D314).
    std::uint32_t firstIndex = 0;
    std::uint32_t indexCount = 0;
    std::uint32_t first = 0;
    std::uint32_t count = 0;
    render_scene::SceneTextures texture;
    /// Its material's own program, which draws it (D485, D487), and cuts
    /// it as a masked caster (D488); none for the engine's own, and for
    /// the solid casters.
    const material::ProgramMaterial* program = nullptr;
    /// A posed model's first joint matrix among the frame's, and how many;
    /// none for one drawn as bound (D508). Each posed model is a run.
    std::uint32_t palette = 0;
    std::uint32_t joints = 0;
    /// Its first joint matrix the frame before (D510).
    std::uint32_t previousPalette = 0;
};
using Runs = std::vector<Run>;

/// A shadow square's casters: the solid by mesh, the masked by mesh and
/// texture, cut (D310).
struct Casters {
    Runs solid;
    Runs masked;

    [[nodiscard]] bool empty() const noexcept {
        return solid.empty() && masked.empty();
    }
};

struct Placed {
    std::vector<float> instances;
    /// The opaque draws', the translucent draws' (D305), then each of the
    /// sun's cascades' casters' (D298), then each square of the punctual
    /// shadows' atlas's (D292).
    Runs runs;
    /// The masked draws', cut in the depth prepass (D310).
    Runs maskedRuns;
    Runs translucentRuns;
    std::array<Casters, 4> cascadeRuns;
    std::vector<Casters> slotRuns;
};

/// The placements of `scene`'s draws whose mesh is `usable`, and their
/// runs; each model left out for its mesh is counted in `modelsLeftOut`.
[[nodiscard]] Placed placeDraws(const render_scene::SceneFrame& scene,
                                const std::map<std::uint64_t, const HeldMesh*>& usable,
                                std::uint64_t& modelsLeftOut);

} // namespace rawframe::render_scene_gpu
