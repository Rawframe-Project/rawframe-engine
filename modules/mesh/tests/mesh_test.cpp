// The cooked mesh: its bytes are fixed, what it writes it reads back, and
// what breaks the format's rules, arrives cut short, or arrives damaged is
// refused without reading past the end.

#include "rawframe/mesh/errors.h"
#include "rawframe/mesh/mesh.h"
#include "rawframe/test/test.h"

#include <cstdint>
#include <limits>
#include <string>

using namespace rawframe;
using namespace rawframe::mesh;

namespace {

bool refusedWith(const auto& outcome, MeshError error) {
    return !outcome.has_value() && outcome.error().domain() == kMeshDomain && outcome.error().code() == code(error);
}

std::string hex(std::span<const std::byte> bytes) {
    constexpr std::string_view kDigits = "0123456789abcdef";
    std::string out;
    for (const std::byte kByte : bytes) {
        out.push_back(kDigits[std::to_integer<std::size_t>(kByte) >> 4U]);
        out.push_back(kDigits[std::to_integer<std::size_t>(kByte) & 0xFU]);
    }
    return out;
}

Mesh triangle() {
    return Mesh{.positions = {{0.0F, 0.0F, 0.0F}, {1.0F, 0.0F, 0.0F}, {0.0F, 1.0F, 0.0F}},
                .normals = {},
                .uvs = {},
                .indices = {0, 1, 2},
                .parts = {{.firstIndex = 0, .indexCount = 3}}};
}

/// Two parts, every attribute.
Mesh quad() {
    return Mesh{.positions = {{-1.0F, 0.0F, -1.0F}, {1.0F, 0.0F, -1.0F}, {1.0F, 0.0F, 1.0F}, {-1.0F, 0.0F, 1.0F}},
                .normals = {{0.0F, 1.0F, 0.0F}, {0.0F, 1.0F, 0.0F}, {0.0F, 1.0F, 0.0F}, {0.0F, 1.0F, 0.0F}},
                .uvs = {{0.0F, 0.0F}, {1.0F, 0.0F}, {1.0F, 1.0F}, {0.0F, 1.0F}},
                .indices = {0, 2, 1, 0, 3, 2},
                .parts = {{.firstIndex = 0, .indexCount = 3}, {.firstIndex = 3, .indexCount = 3}}};
}

} // namespace

RAWFRAME_TEST(ACookedMeshHasFixedBytes) {
    const auto kBytes = encode(triangle());
    RAWFRAME_EXPECT(kBytes.has_value());
    // Changing the format moves this, and needs a new version.
    RAWFRAME_EXPECT(hex(*kBytes) == "52464d530100"
                                    "030000000300000001000000"
                                    "0000000003000000"
                                    "000000000000000000000000"
                                    "0000803f0000000000000000"
                                    "000000000000803f00000000"
                                    "000000000100000002000000");
}

RAWFRAME_TEST(ACookedMeshReadsBackWhole) {
    for (const Mesh& kMesh : {triangle(), quad()}) {
        const auto kBytes = encode(kMesh);
        RAWFRAME_EXPECT(kBytes.has_value());
        const auto kRead = decode(*kBytes);
        RAWFRAME_EXPECT(kRead.has_value() && *kRead == kMesh);
    }
}

RAWFRAME_TEST(AMeshBreakingTheRulesIsRefused) {
    Mesh pastTheVertices = triangle();
    pastTheVertices.indices[2] = 3;
    Mesh notFinite = triangle();
    notFinite.positions[1][2] = std::numeric_limits<float>::quiet_NaN();
    Mesh fewNormals = quad();
    fewNormals.normals.pop_back();
    Mesh gap = quad();
    gap.parts[1].firstIndex = 0;
    Mesh partial = quad();
    partial.parts = {{.firstIndex = 0, .indexCount = 4}, {.firstIndex = 4, .indexCount = 2}};
    Mesh uncovered = quad();
    uncovered.parts.pop_back();
    Mesh empty = triangle();
    empty.indices.clear();
    empty.parts.clear();
    for (const Mesh& kMesh : {pastTheVertices, notFinite, fewNormals, gap, partial, uncovered, empty}) {
        RAWFRAME_EXPECT(refusedWith(encode(kMesh), MeshError::BadMesh));
    }
    RAWFRAME_EXPECT(refusedWith(encode(quad(), {.maximumVertices = 3}), MeshError::OverLimit));
    RAWFRAME_EXPECT(refusedWith(encode(quad(), {.maximumParts = 1}), MeshError::OverLimit));
}

RAWFRAME_TEST(DamagedCookedMeshesAreRefused) {
    const auto kBytes = encode(quad());
    RAWFRAME_EXPECT(kBytes.has_value());
    // Every length short of whole, and one byte more.
    for (std::size_t length = 0; length < kBytes->size(); ++length) {
        RAWFRAME_EXPECT(refusedWith(decode(std::span{*kBytes}.first(length)), MeshError::BadMesh));
    }
    std::vector<std::byte> longer = *kBytes;
    longer.push_back(std::byte{0});
    RAWFRAME_EXPECT(refusedWith(decode(longer), MeshError::BadMesh));
    // Every bit of every byte flipped: refused, or read as a valid mesh.
    for (std::size_t at = 0; at < kBytes->size(); ++at) {
        for (std::uint32_t bit = 0; bit < 8; ++bit) {
            std::vector<std::byte> damaged = *kBytes;
            damaged[at] ^= static_cast<std::byte>(1U << bit);
            const auto kRead = decode(damaged);
            RAWFRAME_EXPECT(!kRead.has_value() || validate(*kRead).has_value());
        }
    }
    // Counts past the limits are refused before anything is allocated.
    std::vector<std::byte> huge = *kBytes;
    huge[6] = huge[7] = huge[8] = huge[9] = std::byte{0xFF};
    RAWFRAME_EXPECT(refusedWith(decode(huge), MeshError::OverLimit));
}
