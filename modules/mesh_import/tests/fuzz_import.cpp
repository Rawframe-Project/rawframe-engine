// Coverage-guided fuzzing of a glTF source as the importer reads it: a
// `.gltf` or `.glb` from an author's tools meets cgltf, the scene walk, the
// accessors and the materials through the limits, and what imports is a
// mesh the cooked format takes and gives back unchanged. Every file the
// source names reads as the same small buffer, so sibling buffers and
// images are reached too.

#include "rawframe/mesh/mesh.h"
#include "rawframe/mesh_import/import.h"

#include <array>
#include <bit>
#include <cstdint>
#include <cstdlib>
#include <span>
#include <string_view>

using namespace rawframe;

namespace {

/// One right triangle facing +Z: positions, normals and texture
/// coordinates as floats, then three 16-bit indices and two bytes of
/// padding, as the seeds' buffers hold it.
constexpr auto kSibling = [] {
    constexpr std::array<float, 24> kValues = {0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 1, 0, 0, 1, 0, 1, 1, 1, 0, 0};
    std::array<std::byte, 104> out{};
    for (std::size_t i = 0; i < kValues.size(); ++i) {
        const auto kBits = std::bit_cast<std::uint32_t>(kValues[i]);
        for (std::size_t shift = 0; shift < 4; ++shift) {
            out[(i * 4) + shift] = static_cast<std::byte>((kBits >> (shift * 8)) & 0xFFU);
        }
    }
    out[98] = std::byte{1};
    out[100] = std::byte{2};
    return out;
}();

result::Result<std::span<const std::byte>> readSibling(std::string_view) {
    return std::span<const std::byte>{kSibling};
}

/// Identities as a sidecar gives them, never nought.
result::Result<std::uint64_t> identify(std::string_view key) {
    std::uint64_t made = 0xcbf29ce484222325ULL;
    for (const char kLetter : key) {
        made = (made ^ static_cast<unsigned char>(kLetter)) * 0x100000001b3ULL;
    }
    return made == 0 ? 1 : made;
}

} // namespace

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    const std::span<const std::byte> kBytes{reinterpret_cast<const std::byte*>(data), size};
    const mesh_import::ImportLimits kLimits{
        .mesh = {.maximumVertices = 4096, .maximumIndices = 12288, .maximumParts = 64},
        .maximumBytes = std::size_t{1} << 16U};
    const auto kImported = mesh_import::importGltf(kBytes, &readSibling, &identify, kLimits);
    if (!kImported.has_value()) {
        return 0;
    }
    const auto kEncoded = mesh::encode(kImported->mesh, kLimits.mesh);
    if (!kEncoded.has_value()) {
        std::abort();
    }
    const auto kDecoded = mesh::decode(*kEncoded, kLimits.mesh);
    if (!kDecoded.has_value() || *kDecoded != kImported->mesh) {
        std::abort();
    }
    return 0;
}
