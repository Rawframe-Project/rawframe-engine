#pragma once

// The mesh importer (`rawframe.mesh`, ADR-0058): a glTF 2.0 source, `.gltf`
// or `.glb`, cooked into the runtime's mesh. Buffers beside a `.gltf` are
// read as inputs, so changing one cooks the mesh again.

#include "rawframe/cook/cook.h"

namespace rawframe::cook {

[[nodiscard]] Importer meshImporter() noexcept;

} // namespace rawframe::cook
