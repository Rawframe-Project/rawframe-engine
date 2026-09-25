#pragma once

// Importing meshes (ADR-0058): glTF 2.0, as `.gltf` with embedded or sibling
// buffers or as `.glb`, cooked into the runtime's mesh. Import tooling only:
// the parser here is trusted with an author's own files and never links
// into a client or a server.
//
// What is supported, required or not:
// - The default scene (or the first), every node's meshes placed by its
//   world transform, one part per primitive in scene order. A transform
//   that mirrors turns its triangles so they still face out.
// - Triangles, triangle strips, and triangle fans, indexed or not; points
//   and lines are refused.
// - POSITION, and NORMAL and TEXCOORD_0 when every primitive has them;
//   either missing anywhere is dropped everywhere. Sparse accessors.
// - Required extensions: KHR_mesh_quantization only. Any other required
//   extension is refused by name; optional ones are ignored, since the
//   cooked mesh holds only geometry.
// - Skins and morph targets are ignored: the rest pose is cooked.
// - Units and axes are glTF's, which are Rawframe's (ADR-0046): nothing is
//   converted.

#include "rawframe/mesh/mesh.h"
#include "rawframe/result/result.h"

#include <cstddef>
#include <functional>
#include <span>
#include <string_view>

namespace rawframe::mesh_import {

/// The bytes of a file a glTF names, at its path relative to the glTF,
/// alive until the import returns.
using ReadFile = std::function<result::Result<std::span<const std::byte>>(std::string_view path)>;

struct ImportLimits {
    mesh::MeshLimits mesh;
    /// The source, and each buffer it reads, at most.
    std::size_t maximumBytes = std::size_t{256} << 20U;
};

/// The required extensions supported, by name.
[[nodiscard]] std::span<const std::string_view> supportedExtensions() noexcept;

/// Refuses (`BadSource`) what is not valid glTF 2.0, a buffer `read`
/// refuses or that is shorter than declared, and geometry that is not
/// triangles; (`UnsupportedExtension`) a required extension not supported;
/// and (`OverLimit`) a source or mesh past the limits.
[[nodiscard]] result::Result<mesh::Mesh>
importGltf(std::span<const std::byte> source, const ReadFile& read, const ImportLimits& limits = {});

} // namespace rawframe::mesh_import
