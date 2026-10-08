#pragma once

// The cooked mesh (ADR-0058's mesh family): triangles in the canonical space
// of ADR-0046, meters with +Y up, in a form Rawframe owns. Import tooling
// writes it; the runtime reads it and decodes no source format. It holds
// geometry, and what each part is drawn with only by identity, so a server
// may read it for collision.

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
    /// The game's material it is drawn with, by the identity a Model would
    /// name it by (D314); nought for the Model's own.
    std::uint64_t material = 0;

    friend bool operator==(const Part&, const Part&) noexcept = default;
};

/// A bone a skinned mesh's vertices follow (D508): the skeleton's bone by
/// its target identity (SPEC-0035), and the matrix taking the mesh's space
/// into the bone's at its bind, column-major.
struct Joint {
    base::Bits128 bone{};
    std::array<float, 16> inverseBind{};

    friend bool operator==(const Joint&, const Joint&) noexcept = default;
};

/// What a mesh's vertices follow as a skeleton moves (D508): its joints,
/// and for each vertex up to four of them by index and their weights,
/// which sum to one. No joints, no skin.
struct Skin {
    std::vector<Joint> joints;
    std::vector<std::array<std::uint16_t, 4>> influences;
    std::vector<std::array<float, 4>> weights;

    friend bool operator==(const Skin&, const Skin&) noexcept = default;
};

/// Triangles as indices into the vertices, counter-clockwise from the front.
/// Normals and texture coordinates are either absent or one per vertex; a
/// skinned mesh's positions and normals are in the space its joints' binds
/// take from (D508).
struct Mesh {
    std::vector<Vector3> positions;
    std::vector<Vector3> normals;
    /// Origin top-left (ADR-0046).
    std::vector<Vector2> uvs;
    std::vector<std::uint32_t> indices;
    /// In order, together covering every index exactly once.
    std::vector<Part> parts;
    Skin skin;

    friend bool operator==(const Mesh&, const Mesh&) noexcept = default;
};

/// The explicit limit profile of one mesh.
struct MeshLimits {
    std::size_t maximumVertices = std::size_t{1} << 22U;
    std::size_t maximumIndices = std::size_t{3} << 22U;
    std::size_t maximumParts = std::size_t{1} << 12U;
    /// What one draw's bone palette holds (D508).
    std::size_t maximumJoints = 256;
};

/// Refuses (`BadMesh`) a mesh with no triangles, an index past the vertices,
/// a value that is not finite, attributes not one per vertex, or parts that
/// are empty, hold partial triangles, or do not tile the indices in order;
/// a skin whose influences or weights are not one per vertex, that names a
/// joint it does not hold, or whose weights are negative or do not sum to
/// one within a thousandth; and (`OverLimit`) one past the limits.
[[nodiscard]] result::Status validate(const Mesh& mesh, const MeshLimits& limits = {});

/// The cooked form, all little-endian: the signature, the version, a byte of
/// attributes (1 normals, 2 texture coordinates, 4 a skin), the vertex,
/// index, and part counts as 32 bits each, then the parts (first index and
/// count as 32 bits each, the material as 64), the positions, the normals
/// and texture coordinates when present, and the indices as 32 bits each.
/// A skin follows them (D508): its joint count as 32 bits, each joint's
/// bone (high then low 64 bits) and inverse bind's sixteen floats, then
/// each vertex's four joint indices as 16 bits each and four weights. A
/// mesh without one keeps the bytes it had.
inline constexpr std::array<char, 4> kCookedMeshSignature = {'R', 'F', 'M', 'S'};
inline constexpr std::uint8_t kCookedMeshVersion = 2;

/// Refuses what `validate` refuses.
[[nodiscard]] result::Result<std::vector<std::byte>> encode(const Mesh& mesh, const MeshLimits& limits = {});

/// Refuses (`BadMesh`) anything but a cooked mesh of this version, exactly
/// as long as its counts say, whose mesh `validate` accepts; and
/// (`OverLimit`) counts past the limits before anything is allocated.
[[nodiscard]] result::Result<Mesh> decode(std::span<const std::byte> bytes, const MeshLimits& limits = {});

} // namespace rawframe::mesh
