#pragma once

// The cooked mesh (ADR-0058's mesh family): triangles in the canonical space
// of ADR-0046, meters with +Y up, in a form Rawframe owns. Import tooling
// writes it; the runtime reads it and decodes no source format. It holds
// geometry only, so a server may read it for collision.

#include "rawframe/base/bits128.h"
#include "rawframe/result/result.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

namespace rawframe::mesh {

/// The resource type of every cooked mesh: the identity content catalogs and
/// cooks name it by.
inline constexpr base::Bits128 kMeshType = base::parseBits128Hex("e59501ffdc15650524921ed22ad3a712").value;
inline constexpr std::string_view kMeshRepresentation = "rawframe.mesh";

using Vector2 = std::array<float, 2>;
using Vector3 = std::array<float, 3>;

/// A run of whole triangles, one per source primitive, so what is drawn or
/// collided with differently later keeps its own range.
struct Part {
    std::uint32_t firstIndex = 0;
    std::uint32_t indexCount = 0;

    friend bool operator==(const Part&, const Part&) noexcept = default;
};

/// Triangles as indices into the vertices, counter-clockwise from the front.
/// Normals and texture coordinates are either absent or one per vertex.
struct Mesh {
    std::vector<Vector3> positions;
    std::vector<Vector3> normals;
    /// Origin top-left (ADR-0046).
    std::vector<Vector2> uvs;
    std::vector<std::uint32_t> indices;
    /// In order, together covering every index exactly once.
    std::vector<Part> parts;

    friend bool operator==(const Mesh&, const Mesh&) noexcept = default;
};

/// The explicit limit profile of one mesh.
struct MeshLimits {
    std::size_t maximumVertices = std::size_t{1} << 22U;
    std::size_t maximumIndices = std::size_t{3} << 22U;
    std::size_t maximumParts = std::size_t{1} << 12U;
};

/// Refuses (`BadMesh`) a mesh with no triangles, an index past the vertices,
/// a value that is not finite, attributes not one per vertex, or parts that
/// are empty, hold partial triangles, or do not tile the indices in order;
/// and (`OverLimit`) one past the limits.
[[nodiscard]] result::Status validate(const Mesh& mesh, const MeshLimits& limits = {});

/// The cooked form, all little-endian: the signature, the version, a byte of
/// attributes (1 normals, 2 texture coordinates), the vertex, index, and
/// part counts as 32 bits each, then the parts, the positions, the normals
/// and texture coordinates when present, and the indices as 32 bits each.
inline constexpr std::array<char, 4> kCookedMeshSignature = {'R', 'F', 'M', 'S'};
inline constexpr std::uint8_t kCookedMeshVersion = 1;

/// Refuses what `validate` refuses.
[[nodiscard]] result::Result<std::vector<std::byte>> encode(const Mesh& mesh, const MeshLimits& limits = {});

/// Refuses (`BadMesh`) anything but a cooked mesh of this version, exactly
/// as long as its counts say, whose mesh `validate` accepts; and
/// (`OverLimit`) counts past the limits before anything is allocated.
[[nodiscard]] result::Result<Mesh> decode(std::span<const std::byte> bytes, const MeshLimits& limits = {});

} // namespace rawframe::mesh
