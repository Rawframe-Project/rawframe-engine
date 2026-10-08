#pragma once

// A scene's meshes as it culls and draws them: each with the sphere around
// it and its parts joined into runs of one material.

#include "rawframe/mesh/mesh.h"
#include "space.h"

#include <cstdint>
#include <memory>
#include <vector>

namespace rawframe::render_scene {

/// A run of a mesh's parts that draw with one material: its indices and
/// the material's identity, nought for the Model's.
struct Run {
    std::uint32_t firstIndex = 0;
    std::uint32_t indexCount = 0;
    std::uint64_t material = 0;
};

/// A mesh with the sphere around it, in its own space.
struct Bounded {
    std::shared_ptr<const mesh::Mesh> mesh;
    Vector center{};
    float radius = 0;
    /// Its parts, the next joined to one of the same material.
    std::vector<Run> runs;
};

/// `made` bounded, its runs joined.
[[nodiscard]] Bounded bounded(std::shared_ptr<const mesh::Mesh> made);

} // namespace rawframe::render_scene
