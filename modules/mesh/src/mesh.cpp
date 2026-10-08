#include "rawframe/mesh/mesh.h"

#include "rawframe/mesh/errors.h"

#include <bit>
#include <cmath>
#include <cstring>

namespace rawframe::mesh {

namespace {

constexpr std::uint8_t kNormals = 1;
constexpr std::uint8_t kUvs = 2;
constexpr std::uint8_t kSkinned = 4;
constexpr std::size_t kHeaderBytes = 4 + 1 + 1 + (3 * 4);
constexpr std::size_t kPartBytes = 4 + 4 + 8;
constexpr std::size_t kJointBytes = 16 + (16 * 4);
constexpr std::size_t kInfluenceBytes = (4 * 2) + (4 * 4);

std::unexpected<result::Error> bad(std::string_view why) {
    return result::fail(result::ErrorClass::InvalidArgument, kMeshDomain, code(MeshError::BadMesh), why);
}

std::unexpected<result::Error> overLimit(std::string_view why) {
    return result::fail(result::ErrorClass::ResourceExhausted, kMeshDomain, code(MeshError::OverLimit), why);
}

result::Status withinLimits(std::size_t vertices, std::size_t indices, std::size_t parts, const MeshLimits& limits) {
    if (vertices > limits.maximumVertices) {
        return overLimit("a mesh holds more vertices than its limits allow");
    }
    if (indices > limits.maximumIndices) {
        return overLimit("a mesh holds more indices than its limits allow");
    }
    if (parts > limits.maximumParts) {
        return overLimit("a mesh holds more parts than its limits allow");
    }
    return {};
}

template <std::size_t N> bool finite(std::span<const std::array<float, N>> values) noexcept {
    for (const std::array<float, N>& kValue : values) {
        for (const float kComponent : kValue) {
            if (!std::isfinite(kComponent)) {
                return false;
            }
        }
    }
    return true;
}

void putU32(std::vector<std::byte>& out, std::uint32_t value) {
    for (std::uint32_t shift = 0; shift < 32; shift += 8) {
        out.push_back(static_cast<std::byte>((value >> shift) & 0xFFU));
    }
}

void putU64(std::vector<std::byte>& out, std::uint64_t value) {
    putU32(out, static_cast<std::uint32_t>(value));
    putU32(out, static_cast<std::uint32_t>(value >> 32U));
}

void putFloats(std::vector<std::byte>& out, std::span<const float> values) {
    for (const float kValue : values) {
        putU32(out, std::bit_cast<std::uint32_t>(kValue));
    }
}

std::uint32_t getU32(std::span<const std::byte> bytes, std::size_t& at) noexcept {
    std::uint32_t value = 0;
    for (std::uint32_t shift = 0; shift < 32; shift += 8) {
        value |= std::to_integer<std::uint32_t>(bytes[at++]) << shift;
    }
    return value;
}

std::uint64_t getU64(std::span<const std::byte> bytes, std::size_t& at) noexcept {
    const std::uint64_t kLow = getU32(bytes, at);
    return kLow | (std::uint64_t{getU32(bytes, at)} << 32U);
}

result::Status validateSkin(const Mesh& mesh, const MeshLimits& limits) {
    const Skin& kSkin = mesh.skin;
    if (kSkin.joints.size() > limits.maximumJoints) {
        return overLimit("a skin holds more joints than its limits allow");
    }
    if (kSkin.joints.empty()) {
        if (!kSkin.influences.empty() || !kSkin.weights.empty()) {
            return bad("a mesh without joints has no influences or weights");
        }
        return {};
    }
    if (kSkin.influences.size() != mesh.positions.size() || kSkin.weights.size() != mesh.positions.size()) {
        return bad("a skin's influences and weights are one per vertex");
    }
    for (const Joint& kJoint : kSkin.joints) {
        for (const float kValue : kJoint.inverseBind) {
            if (!std::isfinite(kValue)) {
                return bad("every value of a mesh is finite");
            }
        }
    }
    for (std::size_t vertex = 0; vertex < kSkin.weights.size(); ++vertex) {
        float sum = 0.0F;
        for (std::size_t each = 0; each < 4; ++each) {
            const float kWeight = kSkin.weights[vertex][each];
            if (!std::isfinite(kWeight) || kWeight < 0.0F) {
                return bad("a skin's weights are finite and not negative");
            }
            if (kSkin.influences[vertex][each] >= kSkin.joints.size()) {
                return bad("a skin's influence names a joint it does not hold");
            }
            sum += kWeight;
        }
        if (std::abs(sum - 1.0F) > 1.0e-3F) {
            return bad("a vertex's weights sum to one");
        }
    }
    return {};
}

template <std::size_t N>
void getVectors(std::span<const std::byte> bytes, std::size_t& at, std::vector<std::array<float, N>>& out) {
    for (std::array<float, N>& value : out) {
        for (float& component : value) {
            component = std::bit_cast<float>(getU32(bytes, at));
        }
    }
}

} // namespace

result::Status validate(const Mesh& mesh, const MeshLimits& limits) {
    RAWFRAME_TRY(withinLimits(mesh.positions.size(), mesh.indices.size(), mesh.parts.size(), limits));
    if (mesh.indices.empty() || mesh.parts.empty()) {
        return bad("a mesh holds at least one triangle in one part");
    }
    if (!mesh.normals.empty() && mesh.normals.size() != mesh.positions.size()) {
        return bad("a mesh's normals are none or one per vertex");
    }
    if (!mesh.uvs.empty() && mesh.uvs.size() != mesh.positions.size()) {
        return bad("a mesh's texture coordinates are none or one per vertex");
    }
    if (!finite<3>(mesh.positions) || !finite<3>(mesh.normals) || !finite<2>(mesh.uvs)) {
        return bad("every value of a mesh is finite");
    }
    for (const std::uint32_t kIndex : mesh.indices) {
        if (kIndex >= mesh.positions.size()) {
            return bad("an index of a mesh names a vertex it does not hold");
        }
    }
    std::size_t next = 0;
    for (const Part& kPart : mesh.parts) {
        if (kPart.firstIndex != next || kPart.indexCount == 0 || kPart.indexCount % 3 != 0) {
            return bad("a mesh's parts are whole triangles, in order, with no gap");
        }
        next += kPart.indexCount;
        if (next > mesh.indices.size()) {
            return bad("a mesh's parts run past its indices");
        }
    }
    if (next != mesh.indices.size()) {
        return bad("a mesh's parts cover all of its indices");
    }
    return validateSkin(mesh, limits);
}

result::Result<std::vector<std::byte>> encode(const Mesh& mesh, const MeshLimits& limits) {
    RAWFRAME_TRY(validate(mesh, limits));
    const std::uint8_t kAttributes =
        static_cast<std::uint8_t>((mesh.normals.empty() ? 0U : kNormals) | (mesh.uvs.empty() ? 0U : kUvs) |
                                  (mesh.skin.joints.empty() ? 0U : kSkinned));
    std::vector<std::byte> out;
    out.reserve(kHeaderBytes + (mesh.parts.size() * kPartBytes) + (mesh.positions.size() * 32) +
                (mesh.indices.size() * 4));
    for (const char kCharacter : kCookedMeshSignature) {
        out.push_back(static_cast<std::byte>(kCharacter));
    }
    out.push_back(std::byte{kCookedMeshVersion});
    out.push_back(std::byte{kAttributes});
    putU32(out, static_cast<std::uint32_t>(mesh.positions.size()));
    putU32(out, static_cast<std::uint32_t>(mesh.indices.size()));
    putU32(out, static_cast<std::uint32_t>(mesh.parts.size()));
    for (const Part& kPart : mesh.parts) {
        putU32(out, kPart.firstIndex);
        putU32(out, kPart.indexCount);
        putU32(out, static_cast<std::uint32_t>(kPart.material));
        putU32(out, static_cast<std::uint32_t>(kPart.material >> 32U));
    }
    putFloats(out, {mesh.positions.data()->data(), mesh.positions.size() * 3});
    if (!mesh.normals.empty()) {
        putFloats(out, {mesh.normals.data()->data(), mesh.normals.size() * 3});
    }
    if (!mesh.uvs.empty()) {
        putFloats(out, {mesh.uvs.data()->data(), mesh.uvs.size() * 2});
    }
    for (const std::uint32_t kIndex : mesh.indices) {
        putU32(out, kIndex);
    }
    if (!mesh.skin.joints.empty()) {
        putU32(out, static_cast<std::uint32_t>(mesh.skin.joints.size()));
        for (const Joint& kJoint : mesh.skin.joints) {
            putU64(out, kJoint.bone.high);
            putU64(out, kJoint.bone.low);
            putFloats(out, kJoint.inverseBind);
        }
        for (std::size_t vertex = 0; vertex < mesh.positions.size(); ++vertex) {
            for (const std::uint16_t kJoint : mesh.skin.influences[vertex]) {
                out.push_back(static_cast<std::byte>(kJoint & 0xFFU));
                out.push_back(static_cast<std::byte>(kJoint >> 8U));
            }
            putFloats(out, mesh.skin.weights[vertex]);
        }
    }
    return out;
}

result::Result<Mesh> decode(std::span<const std::byte> bytes, const MeshLimits& limits) {
    if (bytes.size() < kHeaderBytes ||
        std::memcmp(bytes.data(), kCookedMeshSignature.data(), kCookedMeshSignature.size()) != 0) {
        return bad("not a cooked mesh");
    }
    if (std::to_integer<std::uint8_t>(bytes[4]) != kCookedMeshVersion) {
        return bad("a cooked mesh of a version this engine does not read");
    }
    const std::uint8_t kAttributes = std::to_integer<std::uint8_t>(bytes[5]);
    if ((kAttributes & ~(kNormals | kUvs | kSkinned)) != 0) {
        return bad("a cooked mesh with attributes this engine does not know");
    }
    std::size_t at = 6;
    const std::size_t kVertices = getU32(bytes, at);
    const std::size_t kIndices = getU32(bytes, at);
    const std::size_t kParts = getU32(bytes, at);
    RAWFRAME_TRY(withinLimits(kVertices, kIndices, kParts, limits));
    // Within the limits, none of this can overflow a 64-bit size.
    const std::size_t kPerVertex = 3 + ((kAttributes & kNormals) != 0 ? 3 : 0) + ((kAttributes & kUvs) != 0 ? 2 : 0);
    const std::size_t kUnskinned = kHeaderBytes + (kParts * kPartBytes) + (kVertices * kPerVertex * 4) + (kIndices * 4);
    std::size_t joints = 0;
    if ((kAttributes & kSkinned) != 0) {
        if (bytes.size() < kUnskinned + 4) {
            return bad("a cooked mesh is not as long as its counts say");
        }
        std::size_t countAt = kUnskinned;
        joints = getU32(bytes, countAt);
        if (joints > limits.maximumJoints) {
            return overLimit("a skin holds more joints than its limits allow");
        }
        if (joints == 0) {
            return bad("a cooked mesh's skin holds joints");
        }
    }
    const std::size_t kSkinBytes =
        (kAttributes & kSkinned) != 0 ? 4 + (joints * kJointBytes) + (kVertices * kInfluenceBytes) : 0;
    if (bytes.size() != kUnskinned + kSkinBytes) {
        return bad("a cooked mesh is not as long as its counts say");
    }
    Mesh mesh;
    mesh.parts.resize(kParts);
    for (Part& part : mesh.parts) {
        part.firstIndex = getU32(bytes, at);
        part.indexCount = getU32(bytes, at);
        part.material = getU32(bytes, at);
        part.material |= std::uint64_t{getU32(bytes, at)} << 32U;
    }
    mesh.positions.resize(kVertices);
    getVectors(bytes, at, mesh.positions);
    if ((kAttributes & kNormals) != 0) {
        mesh.normals.resize(kVertices);
        getVectors(bytes, at, mesh.normals);
    }
    if ((kAttributes & kUvs) != 0) {
        mesh.uvs.resize(kVertices);
        getVectors(bytes, at, mesh.uvs);
    }
    mesh.indices.resize(kIndices);
    for (std::uint32_t& index : mesh.indices) {
        index = getU32(bytes, at);
    }
    if (joints != 0) {
        at += 4;
        mesh.skin.joints.resize(joints);
        for (Joint& joint : mesh.skin.joints) {
            joint.bone.high = getU64(bytes, at);
            joint.bone.low = getU64(bytes, at);
            for (float& value : joint.inverseBind) {
                value = std::bit_cast<float>(getU32(bytes, at));
            }
        }
        mesh.skin.influences.resize(kVertices);
        mesh.skin.weights.resize(kVertices);
        for (std::size_t vertex = 0; vertex < kVertices; ++vertex) {
            for (std::uint16_t& joint : mesh.skin.influences[vertex]) {
                joint = static_cast<std::uint16_t>(std::to_integer<std::uint16_t>(bytes[at]) |
                                                   (std::to_integer<std::uint16_t>(bytes[at + 1]) << 8U));
                at += 2;
            }
            for (float& weight : mesh.skin.weights[vertex]) {
                weight = std::bit_cast<float>(getU32(bytes, at));
            }
        }
    }
    RAWFRAME_TRY(validate(mesh, limits));
    return mesh;
}

} // namespace rawframe::mesh
