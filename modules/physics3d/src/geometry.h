#pragma once

// The static bodies' shapes as triangles (D400): private to the module.

#include "bodies.h"
#include "meshes.h"
#include "rawframe/physics3d/physics.h"

#include <cstdint>
#include <map>

namespace rawframe::physics3d {

/// Appends each static, made, non-sensor body of `mapped` to `into`, in
/// entity order; a mesh body's triangles from `meshes`.
void appendStatic(const std::map<world::EntityHandle, Mapped>& mapped,
                  const std::map<std::uint64_t, PreparedMesh>& meshes,
                  StaticGeometry& into);

} // namespace rawframe::physics3d
