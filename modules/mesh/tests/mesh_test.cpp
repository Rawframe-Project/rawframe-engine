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

/// Two parts, the second with a material of its own, every attribute.
Mesh quad() {
    return Mesh{.positions = {{-1.0F, 0.0F, -1.0F}, {1.0F, 0.0F, -1.0F}, {1.0F, 0.0F, 1.0F}, {-1.0F, 0.0F, 1.0F}},
                .normals = {{0.0F, 1.0F, 0.0F}, {0.0F, 1.0F, 0.0F}, {0.0F, 1.0F, 0.0F}, {0.0F, 1.0F, 0.0F}},
                .uvs = {{0.0F, 0.0F}, {1.0F, 0.0F}, {1.0F, 1.0F}, {0.0F, 1.0F}},
                .indices = {0, 2, 1, 0, 3, 2},
                .parts = {{.firstIndex = 0, .indexCount = 3},
                          {.firstIndex = 3, .indexCount = 3, .material = 0x8c41d7e2a95b4f03ULL}}};
}

/// The quad skinned to two joints (D508): its left edge follows the first,
/// its right the second, and its middle would be shared were there one.
Mesh skinned() {
    Mesh mesh = quad();
    constexpr std::array<float, 16> kIdentity = {
        1.0F, 0.0F, 0.0F, 0.0F, 0.0F, 1.0F, 0.0F, 0.0F, 0.0F, 0.0F, 1.0F, 0.0F, 0.0F, 0.0F, 0.0F, 1.0F};
    std::array<float, 16> moved = kIdentity;
    moved[12] = -1.0F;
    mesh.skin = Skin{
        .joints = {{.bone = {.high = 0x0123456789abcdefULL, .low = 0xfedcba9876543210ULL}, .inverseBind = kIdentity},
                   {.bone = {.high = 1, .low = 2}, .inverseBind = moved}},
        .influences = {{0, 1, 0, 0}, {1, 0, 0, 0}, {1, 0, 0, 0}, {0, 1, 0, 0}},
        .weights = {
            {0.75F, 0.25F, 0.0F, 0.0F}, {1.0F, 0.0F, 0.0F, 0.0F}, {0.5F, 0.5F, 0.0F, 0.0F}, {1.0F, 0.0F, 0.0F, 0.0F}}};
    return mesh;
}

} // namespace

RAWFRAME_TEST(ACookedMeshHasFixedBytes) {
    const auto kBytes = encode(triangle());
    RAWFRAME_EXPECT(kBytes.has_value());
    // Changing the format moves this, and needs a new version.
    RAWFRAME_EXPECT(hex(*kBytes) == "52464d530200"
                                    "030000000300000001000000"
                                    "00000000030000000000000000000000"
                                    "000000000000000000000000"
                                    "0000803f0000000000000000"
                                    "000000000000803f00000000"
                                    "000000000100000002000000");
}

RAWFRAME_TEST(ASkinFollowsTheBytesAMeshHadWithoutOne) {
    Mesh mesh = triangle();
    mesh.skin = Skin{.joints = {{.bone = {.high = 1, .low = 2}, .inverseBind = {1.0F}}},
                     .influences = {{0, 0, 0, 0}, {0, 0, 0, 0}, {0, 0, 0, 0}},
                     .weights = {{1.0F, 0.0F, 0.0F, 0.0F}, {1.0F, 0.0F, 0.0F, 0.0F}, {1.0F, 0.0F, 0.0F, 0.0F}}};
    const auto kPlain = encode(triangle());
    const auto kBytes = encode(mesh);
    RAWFRAME_EXPECT(kPlain.has_value() && kBytes.has_value());
    // The attributes byte says so, and the skin follows what the mesh had.
    RAWFRAME_EXPECT(hex(std::span{*kBytes}.first(6)) == "52464d530204");
    RAWFRAME_EXPECT(std::equal(kPlain->begin() + 6, kPlain->end(), kBytes->begin() + 6));
    RAWFRAME_EXPECT(hex(std::span{*kBytes}.subspan(kPlain->size())) ==
                    "01000000"
                    "01000000000000000200000000000000"
                    "0000803f00000000000000000000000000000000000000000000000000000000"
                    "0000000000000000000000000000000000000000000000000000000000000000"
                    "0000000000000000"
                    "0000803f000000000000000000000000"
                    "0000000000000000"
                    "0000803f000000000000000000000000"
                    "0000000000000000"
                    "0000803f000000000000000000000000");
}

RAWFRAME_TEST(ACookedMeshReadsBackWhole) {
    for (const Mesh& kMesh : {triangle(), quad(), skinned()}) {
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

RAWFRAME_TEST(ASkinBreakingTheRulesIsRefused) {
    Mesh pastTheJoints = skinned();
    pastTheJoints.skin.influences[2][1] = 2;
    Mesh fewWeights = skinned();
    fewWeights.skin.weights.pop_back();
    Mesh fewInfluences = skinned();
    fewInfluences.skin.influences.pop_back();
    Mesh notOne = skinned();
    notOne.skin.weights[0][0] = 0.7F;
    Mesh negative = skinned();
    negative.skin.weights[1] = {1.5F, -0.5F, 0.0F, 0.0F};
    Mesh notFinite = skinned();
    notFinite.skin.joints[1].inverseBind[5] = std::numeric_limits<float>::infinity();
    Mesh noJoints = skinned();
    noJoints.skin.joints.clear();
    for (const Mesh& kMesh : {pastTheJoints, fewWeights, fewInfluences, notOne, negative, notFinite, noJoints}) {
        RAWFRAME_EXPECT(refusedWith(encode(kMesh), MeshError::BadMesh));
    }
    // A weight's rounding is kept within a thousandth of one.
    Mesh rounded = skinned();
    rounded.skin.weights[0] = {0.7505F, 0.2499F, 0.0F, 0.0F};
    RAWFRAME_EXPECT(encode(rounded).has_value());
    RAWFRAME_EXPECT(refusedWith(encode(skinned(), {.maximumJoints = 1}), MeshError::OverLimit));
}

RAWFRAME_TEST(DamagedCookedMeshesAreRefused) {
    for (const Mesh& kMesh : {quad(), skinned()}) {
        const auto kBytes = encode(kMesh);
        RAWFRAME_EXPECT(kBytes.has_value());
        // Every length short of whole, and one byte more.
        for (std::size_t length = 0; length < kBytes->size(); ++length) {
            RAWFRAME_EXPECT(refusedWith(decode(std::span{*kBytes}.first(length)), MeshError::BadMesh));
        }
        std::vector<std::byte> longer = *kBytes;
        longer.push_back(std::byte{0});
        RAWFRAME_EXPECT(refusedWith(decode(longer), MeshError::BadMesh));
        // Every bit of every byte flipped: refused, or read as a valid mesh
        // that encodes to the same bytes.
        for (std::size_t at = 0; at < kBytes->size(); ++at) {
            for (std::uint32_t bit = 0; bit < 8; ++bit) {
                std::vector<std::byte> damaged = *kBytes;
                damaged[at] ^= static_cast<std::byte>(1U << bit);
                const auto kRead = decode(damaged);
                RAWFRAME_EXPECT(!kRead.has_value() || validate(*kRead).has_value());
                RAWFRAME_EXPECT(!kRead.has_value() || encode(*kRead) == damaged);
            }
        }
        // Counts past the limits are refused before anything is allocated.
        std::vector<std::byte> huge = *kBytes;
        huge[6] = huge[7] = huge[8] = huge[9] = std::byte{0xFF};
        RAWFRAME_EXPECT(refusedWith(decode(huge), MeshError::OverLimit));
    }
    // A skin's joint count past the limits, too.
    const auto kSkinned = encode(skinned());
    const auto kPlain = encode(quad());
    RAWFRAME_EXPECT(kSkinned.has_value() && kPlain.has_value());
    std::vector<std::byte> many = *kSkinned;
    many[kPlain->size() + 1] = std::byte{0x01};
    RAWFRAME_EXPECT(refusedWith(decode(many), MeshError::OverLimit));
}
