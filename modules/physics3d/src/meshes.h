#pragma once

// Meshes as Maul3D takes them (D112): private to the module, as Maul3D is.

#include "rawframe/mesh/mesh.h"

#include <cstddef>
#include <cstdint>
#include <maul3d/maul3d.h>
#include <vector>

namespace rawframe::physics3d {

/// The most vertices and triangles Maul3D takes in one mesh shape.
inline constexpr std::size_t kPieceMost = 65'535;

/// A mesh as Maul3D takes it: pieces of at most kPieceMost vertices and
/// triangles, and how far from its origin any of it reaches.
struct PreparedMesh {
    struct Piece {
        std::vector<m3Vec3> vertices;
        std::vector<std::uint16_t> indices;
    };
    std::vector<Piece> pieces;
    double reach = 0;
};

/// Triangles in order, a piece closed when the next might not fit. A
/// triangle of no area touches nothing and is left out, since Maul3D's
/// contacts need a face's normal.
[[nodiscard]] PreparedMesh prepare(const mesh::Mesh& source);

} // namespace rawframe::physics3d
