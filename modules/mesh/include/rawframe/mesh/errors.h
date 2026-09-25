#pragma once

#include "rawframe/base/bits128.h"
#include "rawframe/result/error.h"

#include <cstdint>

namespace rawframe::mesh {

/// The domain of every Error the mesh format and its importers create.
inline constexpr result::ErrorDomain kMeshDomain{base::parseBits128Hex("bf9f5edc6bbe90ffea78b2517364254f").value};

/// Codes within kMeshDomain.
enum class MeshError : std::uint32_t {
    /// Bytes that are not a cooked mesh of this version, or a mesh that
    /// breaks the format's rules: an index past the vertices, a value that
    /// is not finite, parts that do not tile the indices.
    BadMesh = 1,
    /// More vertices, indices, parts, or bytes than the limits allow.
    OverLimit = 2,
    /// A source an importer cannot read: not glTF 2.0, a buffer it cannot
    /// find, or geometry that is not triangles.
    BadSource = 3,
    /// A source that requires an extension the importer does not support.
    UnsupportedExtension = 4,
};

[[nodiscard]] constexpr result::ErrorCode code(MeshError error) noexcept {
    return result::ErrorCode{static_cast<std::uint32_t>(error)};
}

} // namespace rawframe::mesh
