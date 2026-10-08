// glTF import: every container form gives the same mesh, nodes place their
// meshes and a mirror keeps triangles facing out, strips and fans become
// lists, materials and their images become subassets the parts name, a
// skinned mesh keeps its space and names its joints' bones, what is not
// supported is refused by name, and damaged sources are refused without a
// crash.

#include "rawframe/animation/skeleton.h"
#include "rawframe/mesh/errors.h"
#include "rawframe/mesh_import/import.h"
#include "rawframe/test/test.h"

#include <array>
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

/// Identities as a sidecar gives them: one for each key, but none for
/// `material/Unmapped`.
result::Result<std::uint64_t> identify(std::string_view key) {
    if (key == "material/Unmapped") {
        return result::fail(result::ErrorClass::NotFound, mesh::kMeshDomain, result::ErrorCode{98}, "unmapped");
    }
    std::uint64_t made = 0xcbf29ce484222325ULL;
    for (const char kLetter : key) {
        made = (made ^ static_cast<unsigned char>(kLetter)) * 0x100000001b3ULL;
    }
    return made;
}

result::Result<Imported> importWhole(const std::string& text, Files& files) {
    return importGltf(bytesOf(text), files.reader(), &identify);
}

result::Result<mesh::Mesh> importText(const std::string& text, Files& files, const ImportLimits& limits = {}) {
    RAWFRAME_TRY_ASSIGN(Imported imported, importGltf(bytesOf(text), files.reader(), &identify, limits));
    return std::move(imported.mesh);
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
    const auto kBinary = importGltf(glb(gltf(binary), kBuffer), none.reader(), &identify);
    RAWFRAME_EXPECT(kBinary.has_value() && kBinary->mesh == kTriangle && kBinary->materials.empty());
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

namespace {

/// The triangle drawn with `material`, beside the textures, images, and
/// samplers given, and the extensions used.
std::string drawn(std::string_view material,
                  std::string_view more,
                  std::string_view attributes = R"("POSITION": 0, "NORMAL": 1, "TEXCOORD_0": 2)") {
    Shape shape;
    shape.primitive = R"("indices": 3, "material": 0)";
    shape.attributes = std::string{attributes};
    shape.extras = R"("extensionsUsed": ["KHR_texture_transform", "KHR_materials_emissive_strength"], )" +
                   std::string{more} + R"("materials": [)" + std::string{material} + "], ";
    return gltf(shape);
}

constexpr std::string_view kImages =
    R"("samplers": [{"magFilter": 9728, "wrapS": 33071, "wrapT": 33071}], )"
    R"("images": [{"name": "Wood", "uri": "wood.png"}, {"name": "Orm", "uri": "data:image/png;base64,b3Jt"}, )"
    R"({"uri": "bumps.png"}], )"
    R"("textures": [{"source": 0}, {"source": 1}, {"source": 2, "sampler": 0}], )";

} // namespace

RAWFRAME_TEST(MaterialsAndTheirImagesAreSubassetsThePartsName) {
    Files files{
        .files = {{"triangle.bin", triangleBuffer()}, {"wood.png", bytesOf("wood")}, {"bumps.png", bytesOf("bumps")}}};
    const auto kWhole = importWhole(
        drawn(R"({"name": "Painted", "pbrMetallicRoughness": {"baseColorFactor": [0.5, 0.25, 1, 0.5], )"
              R"("baseColorTexture": {"index": 0, "extensions": {"KHR_texture_transform": )"
              R"({"offset": [0.5, 0], "scale": [2, 2]}}}, "metallicFactor": 0.25, "roughnessFactor": 0.75, )"
              R"("metallicRoughnessTexture": {"index": 1}}, "occlusionTexture": {"index": 1}, )"
              R"("normalTexture": {"index": 2, "scale": 0.5}, "emissiveFactor": [0, 0.5, 0.25], )"
              R"("extensions": {"KHR_materials_emissive_strength": {"emissiveStrength": 2}}, )"
              R"("alphaMode": "MASK", "alphaCutoff": 0.25, "doubleSided": true})",
              kImages),
        files);
    RAWFRAME_EXPECT(kWhole.has_value());
    if (!kWhole.has_value() || kWhole->materials.size() != 1 || kWhole->textures.size() != 3) {
        RAWFRAME_EXPECT(false);
        return;
    }
    RAWFRAME_EXPECT(kWhole->mesh.parts[0].material == *identify("material/Painted"));
    const material::Material& kMade = kWhole->materials[0].made;
    RAWFRAME_EXPECT(kWhole->materials[0].key == "material/Painted");
    RAWFRAME_EXPECT(kMade.blend == material::Blend::Masked && kMade.alphaCutoff == 0.25F && kMade.doubleSided);
    RAWFRAME_EXPECT(kMade.surface.baseColor == (std::array<float, 3>{0.5F, 0.25F, 1}) &&
                    kMade.surface.geometryOpacity == 0.5F);
    RAWFRAME_EXPECT(kMade.textures.base.id == *identify("texture/Wood") && kMade.textures.baseColor &&
                    kMade.textures.opacity);
    RAWFRAME_EXPECT(kMade.textures.base.scale == (std::array<float, 2>{2, 2}) &&
                    kMade.textures.base.offset == (std::array<float, 2>{0.5F, 0}));
    // The packed texture: metalness blue, roughness green, occlusion red.
    RAWFRAME_EXPECT(
        kMade.textures.packed.id == *identify("texture/Orm") && kMade.textures.metalness == material::Channel::Blue &&
        kMade.textures.roughness == material::Channel::Green && kMade.textures.occlusion == material::Channel::Red);
    RAWFRAME_EXPECT(kMade.surface.baseMetalness == 0.25F && kMade.surface.specularRoughness == 0.75F);
    // An image with no name is keyed by its URI; its sampler is nearest and
    // clamped.
    RAWFRAME_EXPECT(kMade.textures.normal.id == *identify("texture/bumps.png") && kMade.textures.normalScale == 0.5F &&
                    kMade.textures.normal.filter == material::Filter::Nearest &&
                    kMade.textures.normal.address == material::Address::Clamp);
    // Emission: its color at its peak, and a thousand nits for each of
    // glTF's ones.
    RAWFRAME_EXPECT(kMade.surface.emissionColor == (std::array<float, 3>{0, 1, 0.5F}) &&
                    kMade.surface.emissionLuminance == 1000.0F);
    // Images as the glTF holds them, colors or data, in key order.
    RAWFRAME_EXPECT(kWhole->textures[0].key == "texture/Orm" && kWhole->textures[0].image == bytesOf("orm") &&
                    !kWhole->textures[0].color);
    RAWFRAME_EXPECT(kWhole->textures[1].key == "texture/Wood" && kWhole->textures[1].image == bytesOf("wood") &&
                    kWhole->textures[1].color);
    RAWFRAME_EXPECT(kWhole->textures[2].key == "texture/bumps.png" && !kWhole->textures[2].color);

    // An opaque material ignores its alpha; unlit is kept.
    const auto kPlain =
        importWhole(drawn(R"({"name": "Chalk", "pbrMetallicRoughness": {"baseColorFactor": [1, 1, 1, 0.5]}, )"
                          R"("extensions": {"KHR_materials_unlit": {}}})",
                          ""),
                    files);
    RAWFRAME_EXPECT(kPlain.has_value() && kPlain->materials[0].made.surface.geometryOpacity == 1 &&
                    kPlain->materials[0].made.shading == material::Shading::Unlit && kPlain->textures.empty());
}

RAWFRAME_TEST(WhatAMaterialCannotBeIsRefused) {
    Files files{
        .files = {{"triangle.bin", triangleBuffer()}, {"wood.png", bytesOf("wood")}, {"bumps.png", bytesOf("bumps")}}};
    for (const std::string_view kMaterial :
         {// No name: no stable key.
          R"({"pbrMetallicRoughness": {}})",
          // Coordinates other than the first.
          R"({"name": "A", "pbrMetallicRoughness": {"baseColorTexture": {"index": 0, "texCoord": 1}}})",
          // Turned.
          R"({"name": "A", "pbrMetallicRoughness": {"baseColorTexture": {"index": 0, "extensions": )"
          R"({"KHR_texture_transform": {"rotation": 1}}}}})",
          // Occlusion apart from the metallic-roughness texture.
          R"({"name": "A", "pbrMetallicRoughness": {"metallicRoughnessTexture": {"index": 1}}, )"
          R"("occlusionTexture": {"index": 0}})",
          // Occlusion at another strength.
          R"({"name": "A", "occlusionTexture": {"index": 1, "strength": 0.5}})",
          // One image as colors and as data.
          R"({"name": "A", "pbrMetallicRoughness": {"baseColorTexture": {"index": 0}, )"
          R"("metallicRoughnessTexture": {"index": 0}}})",
          // A factor out of range.
          R"({"name": "A", "pbrMetallicRoughness": {"roughnessFactor": 2}})"}) {
        RAWFRAME_EXPECT(refusedWith(importWhole(drawn(kMaterial, kImages), files), MeshError::UnsupportedMaterial));
    }
    // Textures on a mesh without coordinates.
    RAWFRAME_EXPECT(refusedWith(importWhole(drawn(R"({"name": "A", "pbrMetallicRoughness": )"
                                                  R"({"baseColorTexture": {"index": 0}}})",
                                                  kImages,
                                                  R"("POSITION": 0, "NORMAL": 1)"),
                                            files),
                                MeshError::UnsupportedMaterial));
    // A material the sidecar does not map: the identity's own refusal.
    const auto kUnmapped = importWhole(drawn(R"({"name": "Unmapped"})", ""), files);
    RAWFRAME_EXPECT(!kUnmapped.has_value() && kUnmapped.error().code() == result::ErrorCode{98});
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
        RAWFRAME_EXPECT(!importGltf(std::span{kWhole}.first(length), none.reader(), &identify).has_value());
    }
    // Every byte flipped whole: refused, or a mesh the format accepts.
    for (std::size_t at = 0; at < kWhole.size(); ++at) {
        std::vector<std::byte> damaged = kWhole;
        damaged[at] ^= std::byte{0xFF};
        const auto kImported = importGltf(damaged, none.reader(), &identify);
        RAWFRAME_EXPECT(!kImported.has_value() || mesh::validate(kImported->mesh).has_value());
    }
}

namespace {

/// A leg (D508): a mesh node placed five meters along x, skinned to a hip
/// and a knee under an armature, the skin listing the knee first. The
/// buffer: three positions, three vertices' joints as bytes, their weights
/// (the first split two to two, the third naming a joint past the skin with
/// no weight), two inverse binds (the knee's raised a meter), and three
/// indices.
struct Leg {
    std::array<std::uint8_t, 12> joints = {0, 1, 0, 0, 1, 0, 0, 0, 0, 9, 0, 0};
    std::array<float, 12> weights = {2.0F, 2.0F, 0.0F, 0.0F, 1.0F, 0.0F, 0.0F, 0.0F, 1.0F, 0.0F, 0.0F, 0.0F};
    std::string attributes = R"("POSITION": 0, "JOINTS_0": 1, "WEIGHTS_0": 2)";
    std::string extraNode;
};

std::string leg(const Leg& shape) {
    std::vector<std::byte> buffer;
    for (const float kValue : {0.0F, 0.0F, 0.0F, 1.0F, 0.0F, 0.0F, 0.0F, 1.0F, 0.0F}) {
        putU32(buffer, std::bit_cast<std::uint32_t>(kValue));
    }
    for (const std::uint8_t kJoint : shape.joints) {
        buffer.push_back(std::byte{kJoint});
    }
    for (const float kValue : shape.weights) {
        putU32(buffer, std::bit_cast<std::uint32_t>(kValue));
    }
    for (std::size_t joint = 0; joint < 2; ++joint) {
        for (std::size_t at = 0; at < 16; ++at) {
            const float kValue = at % 5 == 0 ? 1.0F : (joint == 0 && at == 13 ? 1.0F : 0.0F);
            putU32(buffer, std::bit_cast<std::uint32_t>(kValue));
        }
    }
    for (const std::uint8_t kByte : {0, 0, 1, 0, 2, 0, 0, 0}) {
        buffer.push_back(std::byte{kByte});
    }
    return R"({"asset": {"version": "2.0"}, "scene": 0, "scenes": [{"nodes": [0]}], "nodes": [)"
           R"({"name": "Armature", "children": [1, 3)" +
           std::string{shape.extraNode.empty() ? "" : ", 4"} +
           R"(]}, {"name": "hip", "children": [2]}, {"name": "knee", "translation": [0, -1, 0]}, )"
           R"({"mesh": 0, "skin": 0, "translation": [5, 0, 0]})" +
           shape.extraNode + R"(], "skins": [{"joints": [2, 1], "inverseBindMatrices": 3}], )" +
           R"("meshes": [{"primitives": [{"attributes": {)" + shape.attributes + R"(}, "indices": 4}]}], )" +
           R"("buffers": [{"byteLength": 232, "uri": "data:application/octet-stream;base64,)" + base64(buffer) +
           R"("}], "bufferViews": [{"buffer": 0, "byteOffset": 0, "byteLength": 36}, )"
           R"({"buffer": 0, "byteOffset": 36, "byteLength": 12}, {"buffer": 0, "byteOffset": 48, "byteLength": 48}, )"
           R"({"buffer": 0, "byteOffset": 96, "byteLength": 128}, {"buffer": 0, "byteOffset": 224, "byteLength": 6}], )"
           R"("accessors": [{"bufferView": 0, "componentType": 5126, "count": 3, "type": "VEC3", )"
           R"("min": [0, 0, 0], "max": [1, 1, 0]}, {"bufferView": 1, "componentType": 5121, "count": 3, "type": "VEC4"}, )"
           R"({"bufferView": 2, "componentType": 5126, "count": 3, "type": "VEC4"}, )"
           R"({"bufferView": 3, "componentType": 5126, "count": 2, "type": "MAT4"}, )"
           R"({"bufferView": 4, "componentType": 5123, "count": 3, "type": "SCALAR"}]})";
}

result::Result<Imported> importedLeg(const Leg& shape) {
    const std::string kText = leg(shape);
    const std::vector<std::byte> kBytes = bytesOf(kText);
    const ReadFile kNothing = [](std::string_view) -> result::Result<std::span<const std::byte>> {
        return result::fail(result::ErrorClass::NotFound, mesh::kMeshDomain, {}, "no file");
    };
    const Identify kIdentity = [](std::string_view) {
        return std::uint64_t{1};
    };
    return importGltf(kBytes, kNothing, kIdentity, {});
}

} // namespace

RAWFRAME_TEST(ASkinnedMeshKeepsItsSpaceAndNamesItsJointsBones) {
    const auto kImported = importedLeg({});
    RAWFRAME_EXPECT(kImported.has_value());
    if (!kImported.has_value()) {
        return;
    }
    const mesh::Mesh& kMesh = kImported->mesh;
    // The mesh's node places nothing; its joints will.
    RAWFRAME_EXPECT(kMesh.positions[1] == (mesh::Vector3{1.0F, 0.0F, 0.0F}));
    // In the skin's order, each joint names the bone the skeleton has for it.
    const std::array<std::string_view, 2> kKnee = {"hip", "knee"};
    const std::array<std::string_view, 1> kHip = {"hip"};
    RAWFRAME_EXPECT(kMesh.skin.joints.size() == 2 && kMesh.skin.joints[0].bone == animation::targetIdOf(kKnee) &&
                    kMesh.skin.joints[1].bone == animation::targetIdOf(kHip));
    RAWFRAME_EXPECT(kMesh.skin.joints[0].inverseBind[13] == 1.0F && kMesh.skin.joints[1].inverseBind[13] == 0.0F);
    // Weights made to sum to one; a joint with none named as the first.
    RAWFRAME_EXPECT(kMesh.skin.weights[0] == (std::array<float, 4>{0.5F, 0.5F, 0.0F, 0.0F}));
    RAWFRAME_EXPECT(kMesh.skin.influences[0] == (std::array<std::uint16_t, 4>{0, 1, 0, 0}) &&
                    kMesh.skin.influences[2] == (std::array<std::uint16_t, 4>{0, 0, 0, 0}));
    RAWFRAME_EXPECT(mesh::encode(kMesh).has_value());
}

RAWFRAME_TEST(SkinsAMeshCannotFollowAreRefused) {
    Leg mixed;
    mixed.extraNode = R"(, {"mesh": 0})";
    Leg past;
    past.joints[4] = 2;
    Leg unweighted;
    unweighted.weights[4] = 0.0F;
    Leg noWeights;
    noWeights.attributes = R"("POSITION": 0, "JOINTS_0": 1)";
    Leg negative;
    negative.weights[1] = -1.0F;
    for (const Leg& kLeg : {mixed, past, unweighted, noWeights, negative}) {
        RAWFRAME_EXPECT(refusedWith(importedLeg(kLeg), MeshError::BadSource));
    }
}
