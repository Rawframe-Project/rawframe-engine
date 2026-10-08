#pragma once

// Skinned models' palettes (D508): for each joint of a skinned mesh, the
// matrix taking its vertices from the mesh's bind space to its entity's
// model space as the entity's pose has it, the joint's bone posed after
// its inverse bind.

#include "rawframe/animation/sample.h"
#include "rawframe/base/bits128.h"
#include "rawframe/mesh/mesh.h"
#include "rawframe/render_scene/scene.h"

#include <cstdint>
#include <map>
#include <span>
#include <utility>
#include <vector>

namespace rawframe::render_scene {

/// A transform as a column-major matrix: scaled, then turned, then moved.
[[nodiscard]] Matrix matrixOf(const animation::Transform& transform) noexcept;

/// Puts a posed model's palette `now` among the frame's `palette`, and
/// after it `before`, the frame before's, where it holds as many joints,
/// naming both on `draw` (D508, D510); false, putting nothing, past `room`
/// matrices.
bool paletteInto(std::span<const Matrix> now,
                 std::span<const Matrix> before,
                 std::size_t room,
                 std::vector<Matrix>& palette,
                 SceneDraw& draw);

class Skinning {
public:
    /// Appends `skin`'s palette posed by `pose`, whose bones are `bones` in
    /// order, to `palette`; false, appending nothing, where a joint names a
    /// bone not among them or the pose is not one transform a bone.
    /// `mesh` names the skin, so which bone each joint is is found once
    /// for a mesh and a skeleton's bones.
    bool pose(std::uint64_t mesh,
              const mesh::Skin& skin,
              std::span<const base::Bits128> bones,
              const animation::Pose& pose,
              std::vector<Matrix>& palette);

private:
    /// Each joint's bone, by mesh and by the bones' list; none past it
    /// where a joint's bone is not among them.
    std::map<std::pair<std::uint64_t, const base::Bits128*>, std::vector<std::uint32_t>> joints_;
};

} // namespace rawframe::render_scene
