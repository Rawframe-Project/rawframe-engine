// glTF import: every container form gives the same mesh, nodes place their
// meshes and a mirror keeps triangles facing out, strips and fans become
// lists, what is not supported is refused by name, and damaged sources are
// refused without a crash.

#include "rawframe/mesh/errors.h"
#include "rawframe/mesh_import/import.h"
#include "rawframe/test/test.h"

#include <bit>
#include <cstdint>
#include <cstring>
#include <map>
#include <string>
#include <vector>

using namespace rawframe;
using namespace rawframe::mesh_import;
using mesh::MeshError;

namespace {

bool refusedWith(const auto& outcome, MeshError error) {
    return !outcome.has_value() && outcome.error().domain() == mesh::kMeshDomain &&
           outcome.error().code() == mesh::code(error);
}

std::vector<std::byte> bytesOf(std::string_view text) {
    std::vector<std::byte> out(text.size());
    std::memcpy(out.data(), text.data(), text.size());
    return out;
}

void putU32(std::vector<std::byte>& out, std::uint32_t value) {
    for (std::uint32_t shift = 0; shift < 32; shift += 8) {
        out.push_back(static_cast<std::byte>((value >> shift) & 0xFFU));
    }
}

/// One right triangle facing +Z: positions, normals, texture coordinates,
/// then three 16-bit indices and two bytes of padding.
std::vector<std::byte> triangleBuffer() {
    std::vector<std::byte> out;
    for (const float kValue : {0.0F, 0.0F, 0.0F, 1.0F, 0.0F, 0.0F, 0.0F, 1.0F, 0.0F}) {
        putU32(out, std::bit_cast<std::uint32_t>(kValue));
    }
    for (std::size_t i = 0; i < 3; ++i) {
        for (const float kValue : {0.0F, 0.0F, 1.0F}) {
            putU32(out, std::bit_cast<std::uint32_t>(kValue));
        }
    }
    for (const float kValue : {0.0F, 1.0F, 1.0F, 1.0F, 0.0F, 0.0F}) {
        putU32(out, std::bit_cast<std::uint32_t>(kValue));
    }
    for (const std::uint8_t kByte : {0, 0, 1, 0, 2, 0, 0, 0}) {
        out.push_back(std::byte{kByte});
    }
    return out;
}

std::string base64(std::span<const std::byte> bytes) {
    constexpr std::string_view kAlphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    for (std::size_t i = 0; i < bytes.size(); i += 3) {
        std::uint32_t group = std::to_integer<std::uint32_t>(bytes[i]) << 16U;
        if (i + 1 < bytes.size()) {
            group |= std::to_integer<std::uint32_t>(bytes[i + 1]) << 8U;
        }
        if (i + 2 < bytes.size()) {
            group |= std::to_integer<std::uint32_t>(bytes[i + 2]);
        }
        out.push_back(kAlphabet[(group >> 18U) & 63U]);
        out.push_back(kAlphabet[(group >> 12U) & 63U]);
        out.push_back(i + 1 < bytes.size() ? kAlphabet[(group >> 6U) & 63U] : '=');
        out.push_back(i + 2 < bytes.size() ? kAlphabet[group & 63U] : '=');
    }
    return out;
}

struct Shape {
    /// The buffer's `"uri"` member, or none for a `.glb`'s own chunk.
    std::string buffer = R"("uri": "triangle.bin", )";
    std::string node = R"({"mesh": 0})";
    std::string attributes = R"("POSITION": 0, "NORMAL": 1, "TEXCOORD_0": 2)";
    std::string primitive = R"("indices": 3)";
    std::string extras;
};

std::string gltf(const Shape& shape) {
    return R"({"asset": {"version": "2.0"}, )" + shape.extras + R"("scene": 0, "scenes": [{"nodes": [0]}], )" +
           R"("nodes": [)" + shape.node + "], " + R"("meshes": [{"primitives": [{"attributes": {)" + shape.attributes +
           "}, " + shape.primitive + "}]}], " + R"("buffers": [{)" + shape.buffer + R"("byteLength": 104}], )" +
           R"("bufferViews": [{"buffer": 0, "byteOffset": 0, "byteLength": 96}, )" +
           R"({"buffer": 0, "byteOffset": 96, "byteLength": 6}], )" + R"("accessors": [)" +
           R"({"bufferView": 0, "byteOffset": 0, "componentType": 5126, "count": 3, "type": "VEC3", )" +
           R"("min": [0, 0, 0], "max": [1, 1, 0]}, )" +
           R"({"bufferView": 0, "byteOffset": 36, "componentType": 5126, "count": 3, "type": "VEC3"}, )" +
           R"({"bufferView": 0, "byteOffset": 72, "componentType": 5126, "count": 3, "type": "VEC2"}, )" +
           R"({"bufferView": 1, "componentType": 5123, "count": 3, "type": "SCALAR"}]})";
}

std::vector<std::byte> glb(std::string json, std::span<const std::byte> buffer) {
    while (json.size() % 4 != 0) {
        json.push_back(' ');
    }
    std::vector<std::byte> out;
    putU32(out, 0x46546C67U);
    putU32(out, 2);
    putU32(out, static_cast<std::uint32_t>(12 + 8 + json.size() + 8 + buffer.size()));
    putU32(out, static_cast<std::uint32_t>(json.size()));
    putU32(out, 0x4E4F534AU);
    const std::vector<std::byte> kJson = bytesOf(json);
    out.insert(out.end(), kJson.begin(), kJson.end());
    putU32(out, static_cast<std::uint32_t>(buffer.size()));
    putU32(out, 0x004E4942U);
    out.insert(out.end(), buffer.begin(), buffer.end());
    return out;
}

/// Files by path, as a cook's reads give them.
struct Files {
    std::map<std::string, std::vector<std::byte>, std::less<>> files;

    ReadFile reader() {
        return [this](std::string_view path) -> result::Result<std::span<const std::byte>> {
            const auto kFound = files.find(path);
            if (kFound == files.end()) {
                return result::fail(result::ErrorClass::NotFound, mesh::kMeshDomain, result::ErrorCode{99}, "absent");
            }
            return std::span<const std::byte>{kFound->second};
        };
    }
};

result::Result<mesh::Mesh> importText(const std::string& text, Files& files, const ImportLimits& limits = {}) {
    return importGltf(bytesOf(text), files.reader(), limits);
}

const mesh::Mesh kTriangle{.positions = {{0.0F, 0.0F, 0.0F}, {1.0F, 0.0F, 0.0F}, {0.0F, 1.0F, 0.0F}},
                           .normals = {{0.0F, 0.0F, 1.0F}, {0.0F, 0.0F, 1.0F}, {0.0F, 0.0F, 1.0F}},
                           .uvs = {{0.0F, 1.0F}, {1.0F, 1.0F}, {0.0F, 0.0F}},
                           .indices = {0, 1, 2},
                           .parts = {{.firstIndex = 0, .indexCount = 3}}};

} // namespace

RAWFRAME_TEST(EveryContainerGivesTheSameMesh) {
    const std::vector<std::byte> kBuffer = triangleBuffer();
    Files files{.files = {{"triangle.bin", kBuffer}}};
    const auto kSibling = importText(gltf({}), files);
    RAWFRAME_EXPECT(kSibling.has_value() && *kSibling == kTriangle);

    Shape embedded;
    embedded.buffer = R"("uri": "data:application/octet-stream;base64,)" + base64(kBuffer) + R"(", )";
    Files none;
    const auto kEmbedded = importText(gltf(embedded), none);
    RAWFRAME_EXPECT(kEmbedded.has_value() && *kEmbedded == kTriangle);

    Shape binary;
    binary.buffer.clear();
    const auto kBinary = importGltf(glb(gltf(binary), kBuffer), none.reader());
    RAWFRAME_EXPECT(kBinary.has_value() && *kBinary == kTriangle);
}

RAWFRAME_TEST(NodesPlaceMeshesAndMirrorsKeepFaces) {
    Files files{.files = {{"triangle.bin", triangleBuffer()}}};
    // A child mirrored in X under a parent moved up two meters.
    Shape placed;
    placed.node = R"({"translation": [0, 2, 0], "children": [1]}, {"mesh": 0, "scale": [-1, 1, 1]})";
    const auto kPlaced = importText(gltf(placed), files);
    RAWFRAME_EXPECT(kPlaced.has_value());
    RAWFRAME_EXPECT(kPlaced->positions ==
                    (std::vector<mesh::Vector3>{{0.0F, 2.0F, 0.0F}, {-1.0F, 2.0F, 0.0F}, {0.0F, 3.0F, 0.0F}}));
    // Mirrored, the triangle would face -Z; its order turns so it faces +Z,
    // as its normals, which a mirror in X leaves alone, say.
    RAWFRAME_EXPECT(kPlaced->indices == (std::vector<std::uint32_t>{0, 2, 1}));
    RAWFRAME_EXPECT(kPlaced->normals == kTriangle.normals);
}

RAWFRAME_TEST(StripsAndFansBecomeLists) {
    Files files{.files = {{"triangle.bin", triangleBuffer()}}};
    Shape strip;
    strip.primitive = R"("mode": 5)";
    const auto kStrip = importText(gltf(strip), files);
    RAWFRAME_EXPECT(kStrip.has_value() && kStrip->indices == (std::vector<std::uint32_t>{0, 1, 2}));
    Shape fan;
    fan.primitive = R"("mode": 6)";
    const auto kFan = importText(gltf(fan), files);
    RAWFRAME_EXPECT(kFan.has_value() && kFan->indices == (std::vector<std::uint32_t>{1, 2, 0}));
    Shape points;
    points.primitive = R"("mode": 0)";
    RAWFRAME_EXPECT(refusedWith(importText(gltf(points), files), MeshError::BadSource));
}

RAWFRAME_TEST(AnAttributeMissingAnywhereIsDroppedEverywhere) {
    Files files{.files = {{"triangle.bin", triangleBuffer()}}};
    Shape bare;
    bare.attributes = R"("POSITION": 0)";
    const auto kBare = importText(gltf(bare), files);
    RAWFRAME_EXPECT(kBare.has_value() && kBare->normals.empty() && kBare->uvs.empty());
    RAWFRAME_EXPECT(kBare.has_value() && kBare->positions == kTriangle.positions);
}

RAWFRAME_TEST(ExtensionsAreRefusedByName) {
    Files files{.files = {{"triangle.bin", triangleBuffer()}}};
    Shape compressed;
    compressed.extras = R"("extensionsUsed": ["KHR_draco_mesh_compression"], )"
                        R"("extensionsRequired": ["KHR_draco_mesh_compression"], )";
    const auto kCompressed = importText(gltf(compressed), files);
    RAWFRAME_EXPECT(refusedWith(kCompressed, MeshError::UnsupportedExtension));
    RAWFRAME_EXPECT(!kCompressed.has_value() &&
                    kCompressed.error().description().find("KHR_draco_mesh_compression") != std::string_view::npos);
    Shape quantized;
    quantized.extras = R"("extensionsUsed": ["KHR_mesh_quantization", "KHR_materials_unlit"], )"
                       R"("extensionsRequired": ["KHR_mesh_quantization"], )";
    RAWFRAME_EXPECT(importText(gltf(quantized), files).has_value());
}

RAWFRAME_TEST(WhatCannotBeReadIsRefused) {
    Files absent;
    RAWFRAME_EXPECT(refusedWith(importText(gltf({}), absent), MeshError::BadSource));
    std::vector<std::byte> shortBuffer = triangleBuffer();
    shortBuffer.resize(90);
    Files cut{.files = {{"triangle.bin", shortBuffer}}};
    RAWFRAME_EXPECT(refusedWith(importText(gltf({}), cut), MeshError::BadSource));
    Files files{.files = {{"triangle.bin", triangleBuffer()}}};
    Shape wrongShape;
    wrongShape.attributes = R"("POSITION": 3)";
    RAWFRAME_EXPECT(refusedWith(importText(gltf(wrongShape), files), MeshError::BadSource));
    RAWFRAME_EXPECT(refusedWith(importText(R"({"asset": {"version": "1.0"}})", files), MeshError::BadSource));
    RAWFRAME_EXPECT(refusedWith(importText(R"({"asset": {"version": "2.0"}})", files), MeshError::BadSource));
    RAWFRAME_EXPECT(refusedWith(importText(gltf({}), files, {.mesh = {.maximumVertices = 2}}), MeshError::OverLimit));
    RAWFRAME_EXPECT(refusedWith(importText(gltf({}), files, {.maximumBytes = 64}), MeshError::OverLimit));
}

RAWFRAME_TEST(DamagedSourcesAreRefusedWithoutACrash) {
    const std::vector<std::byte> kBuffer = triangleBuffer();
    Shape binary;
    binary.buffer.clear();
    const std::vector<std::byte> kWhole = glb(gltf(binary), kBuffer);
    Files none;
    for (std::size_t length = 0; length < kWhole.size(); ++length) {
        RAWFRAME_EXPECT(!importGltf(std::span{kWhole}.first(length), none.reader()).has_value());
    }
    // Every byte flipped whole: refused, or a mesh the format accepts.
    for (std::size_t at = 0; at < kWhole.size(); ++at) {
        std::vector<std::byte> damaged = kWhole;
        damaged[at] ^= std::byte{0xFF};
        const auto kImported = importGltf(damaged, none.reader());
        RAWFRAME_EXPECT(!kImported.has_value() || mesh::validate(*kImported).has_value());
    }
}
