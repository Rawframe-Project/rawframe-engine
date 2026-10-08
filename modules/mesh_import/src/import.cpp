#include "rawframe/mesh_import/import.h"

#include "materials.h"
#include "rawframe/animation_import/import.h"
#include "rawframe/mesh/errors.h"

#include <array>
#include <cgltf.h>
#include <cmath>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace rawframe::mesh_import {

namespace {

using mesh::MeshError;

constexpr std::array<std::string_view, 4> kSupportedExtensions = {
    "KHR_mesh_quantization", "KHR_materials_unlit", "KHR_materials_emissive_strength", "KHR_texture_transform"};

/// Names what the glTF's buffer paths resolve against: with no directory in
/// it, a path comes to `read` as the glTF wrote it, decoded.
constexpr const char* kSourceName = "source";

std::unexpected<result::Error> badSource(std::string_view why) {
    return result::fail(result::ErrorClass::InvalidArgument, mesh::kMeshDomain, mesh::code(MeshError::BadSource), why);
}

std::unexpected<result::Error> overLimit(std::string_view why) {
    return result::fail(
        result::ErrorClass::ResourceExhausted, mesh::kMeshDomain, mesh::code(MeshError::OverLimit), why);
}

struct Buffers {
    const ReadFile* read = nullptr;
    std::string refused;
};

cgltf_result readBuffer(const cgltf_memory_options* /*memory*/,
                        const cgltf_file_options* options,
                        const char* path,
                        cgltf_size* size,
                        void** data) {
    auto* buffers = static_cast<Buffers*>(options->user_data);
    const auto kBytes = (*buffers->read)(path);
    if (!kBytes.has_value()) {
        buffers->refused = path;
        return cgltf_result_file_not_found;
    }
    if (kBytes->empty() || kBytes->size() < *size) {
        buffers->refused = path;
        return cgltf_result_data_too_short;
    }
    *size = kBytes->size();
    // cgltf only reads a buffer, and `read` keeps it alive, so it is lent
    // rather than copied and releasing it does nothing.
    *data = const_cast<std::byte*>(kBytes->data());
    return cgltf_result_success;
}

void releaseBuffer(const cgltf_memory_options* /*memory*/, const cgltf_file_options* /*options*/, void* /*data*/) {
}

struct Free {
    void operator()(cgltf_data* data) const noexcept {
        cgltf_free(data);
    }
};

const cgltf_accessor* attribute(const cgltf_primitive& primitive, cgltf_attribute_type type) noexcept {
    for (cgltf_size i = 0; i < primitive.attributes_count; ++i) {
        if (primitive.attributes[i].type == type && primitive.attributes[i].index == 0) {
            return primitive.attributes[i].data;
        }
    }
    return nullptr;
}

template <std::size_t N>
result::Result<std::vector<std::array<float, N>>> unpack(const cgltf_accessor& accessor, cgltf_type type) {
    if (accessor.type != type) {
        return badSource("a vertex attribute of the wrong shape");
    }
    std::vector<std::array<float, N>> values(accessor.count);
    if (accessor.count != 0 &&
        cgltf_accessor_unpack_floats(&accessor, values.data()->data(), accessor.count * N) != accessor.count * N) {
        return badSource("a vertex attribute that cannot be read");
    }
    return values;
}

/// A primitive's vertices in order as triangles, whatever its mode.
result::Result<std::vector<std::uint32_t>> triangles(const cgltf_primitive& primitive, std::size_t vertices) {
    std::vector<std::uint32_t> order;
    if (primitive.indices != nullptr) {
        const cgltf_accessor& kIndices = *primitive.indices;
        if (kIndices.type != cgltf_type_scalar) {
            return badSource("indices that are not scalars");
        }
        order.resize(kIndices.count);
        if (kIndices.count != 0 &&
            cgltf_accessor_unpack_indices(&kIndices, order.data(), sizeof(std::uint32_t), kIndices.count) !=
                kIndices.count) {
            return badSource("indices that cannot be read");
        }
        for (const std::uint32_t kIndex : order) {
            if (kIndex >= vertices) {
                return badSource("an index past a primitive's vertices");
            }
        }
    } else {
        order.resize(vertices);
        for (std::size_t i = 0; i < vertices; ++i) {
            order[i] = static_cast<std::uint32_t>(i);
        }
    }
    std::vector<std::uint32_t> out;
    switch (primitive.type) {
    case cgltf_primitive_type_triangles:
        if (order.size() % 3 != 0) {
            return badSource("a triangle list whose count is not a multiple of three");
        }
        return order;
    case cgltf_primitive_type_triangle_strip:
        // glTF 2.0, section 3.7.2.1: every other triangle turns the other
        // way, so each still faces out.
        for (std::size_t i = 0; i + 2 < order.size(); ++i) {
            const std::size_t kOdd = i % 2;
            out.insert(out.end(), {order[i], order[i + 1 + kOdd], order[i + 2 - kOdd]});
        }
        return out;
    case cgltf_primitive_type_triangle_fan:
        for (std::size_t i = 0; i + 2 < order.size(); ++i) {
            out.insert(out.end(), {order[i + 1], order[i + 2], order[0]});
        }
        return out;
    default:
        return badSource("a primitive of points or lines; a mesh holds triangles");
    }
}

/// Where a node's vertices land: its world matrix, column-major, and the
/// matrix its normals take, the cofactors of its upper 3x3, signed so they
/// point the way the matrix's would.
struct Placement {
    std::array<double, 16> matrix{};
    std::array<std::array<double, 3>, 3> normal{};
    bool mirrors = false;
};

Placement placement(const cgltf_node& node) {
    std::array<float, 16> world{};
    cgltf_node_transform_world(&node, world.data());
    Placement out;
    for (std::size_t i = 0; i < world.size(); ++i) {
        out.matrix[i] = world[i];
    }
    const auto kA = [&](std::size_t row, std::size_t column) {
        return out.matrix[(column * 4) + row];
    };
    for (std::size_t row = 0; row < 3; ++row) {
        for (std::size_t column = 0; column < 3; ++column) {
            const std::size_t kR1 = (row + 1) % 3;
            const std::size_t kR2 = (row + 2) % 3;
            const std::size_t kC1 = (column + 1) % 3;
            const std::size_t kC2 = (column + 2) % 3;
            out.normal[row][column] = (kA(kR1, kC1) * kA(kR2, kC2)) - (kA(kR1, kC2) * kA(kR2, kC1));
        }
    }
    const double kDeterminant =
        (kA(0, 0) * out.normal[0][0]) + (kA(0, 1) * out.normal[0][1]) + (kA(0, 2) * out.normal[0][2]);
    out.mirrors = kDeterminant < 0.0;
    if (out.mirrors) {
        for (auto& row : out.normal) {
            for (double& value : row) {
                value = -value;
            }
        }
    }
    return out;
}

mesh::Vector3 place(const Placement& placement, const mesh::Vector3& point) noexcept {
    mesh::Vector3 out{};
    for (std::size_t row = 0; row < 3; ++row) {
        out[row] = static_cast<float>((placement.matrix[row] * point[0]) + (placement.matrix[4 + row] * point[1]) +
                                      (placement.matrix[8 + row] * point[2]) + placement.matrix[12 + row]);
    }
    return out;
}

mesh::Vector3 turn(const Placement& placement, const mesh::Vector3& normal) noexcept {
    std::array<double, 3> turned{};
    for (std::size_t row = 0; row < 3; ++row) {
        turned[row] = (placement.normal[row][0] * normal[0]) + (placement.normal[row][1] * normal[1]) +
                      (placement.normal[row][2] * normal[2]);
    }
    const double kLength = std::sqrt((turned[0] * turned[0]) + (turned[1] * turned[1]) + (turned[2] * turned[2]));
    if (kLength > 0.0) {
        for (double& value : turned) {
            value /= kLength;
        }
    }
    return {static_cast<float>(turned[0]), static_cast<float>(turned[1]), static_cast<float>(turned[2])};
}

/// The mesh being gathered, and whether every primitive so far had each
/// optional attribute.
struct Gathered {
    mesh::Mesh mesh;
    bool normals = true;
    bool uvs = true;
};

/// A skin's joints as the mesh's (D508): each names the bone the skeleton
/// imported from the same skin has, with the skin's inverse bind, or none.
result::Result<std::vector<mesh::Joint>> jointsOf(const cgltf_skin& skin, const ImportLimits& limits) {
    if (skin.joints_count == 0) {
        return badSource("a skin with no joints");
    }
    if (skin.joints_count > limits.mesh.maximumJoints) {
        return overLimit("a skin holds more joints than its limits allow");
    }
    std::map<const cgltf_node*, std::size_t> indices;
    for (cgltf_size i = 0; i < skin.joints_count; ++i) {
        indices.emplace(skin.joints[i], i);
    }
    std::vector<animation_import::SkinJoint> described;
    for (cgltf_size i = 0; i < skin.joints_count; ++i) {
        const cgltf_node* kJoint = skin.joints[i];
        const auto kParent = kJoint->parent != nullptr ? indices.find(kJoint->parent) : indices.end();
        described.push_back({.name = kJoint->name != nullptr ? std::string_view{kJoint->name} : std::string_view{},
                             .parent = kParent != indices.end() ? std::optional{kParent->second} : std::nullopt});
    }
    const auto kTargets = animation_import::jointTargets(described);
    if (!kTargets.has_value()) {
        return badSource(kTargets.error().description());
    }
    const cgltf_accessor* kBinds = skin.inverse_bind_matrices;
    if (kBinds != nullptr && (kBinds->type != cgltf_type_mat4 || kBinds->count != skin.joints_count)) {
        return badSource("a skin whose inverse binds are not one matrix per joint");
    }
    std::vector<mesh::Joint> joints(skin.joints_count);
    for (cgltf_size i = 0; i < skin.joints_count; ++i) {
        joints[i].bone = (*kTargets)[i];
        joints[i].inverseBind = {
            1.0F, 0.0F, 0.0F, 0.0F, 0.0F, 1.0F, 0.0F, 0.0F, 0.0F, 0.0F, 1.0F, 0.0F, 0.0F, 0.0F, 0.0F, 1.0F};
        if (kBinds != nullptr && !cgltf_accessor_read_float(kBinds, i, joints[i].inverseBind.data(), 16)) {
            return badSource("a skin's inverse bind that cannot be read");
        }
    }
    return joints;
}

/// A skinned primitive's influences and weights, its weights made to sum
/// to one.
result::Status
influencesOf(const cgltf_primitive& primitive, std::size_t vertices, std::size_t joints, mesh::Skin& skin) {
    const cgltf_accessor* kJoints = attribute(primitive, cgltf_attribute_type_joints);
    const cgltf_accessor* kWeights = attribute(primitive, cgltf_attribute_type_weights);
    if (kJoints == nullptr || kWeights == nullptr || kJoints->count != vertices || kWeights->count != vertices ||
        kJoints->type != cgltf_type_vec4) {
        return badSource("a skinned primitive without JOINTS_0 and WEIGHTS_0 for every vertex");
    }
    RAWFRAME_TRY_ASSIGN(auto weights, unpack<4>(*kWeights, cgltf_type_vec4));
    for (std::size_t vertex = 0; vertex < vertices; ++vertex) {
        std::array<cgltf_uint, 4> read{};
        if (!cgltf_accessor_read_uint(kJoints, vertex, read.data(), 4)) {
            return badSource("a skinned primitive's joints that cannot be read");
        }
        std::array<float, 4>& weight = weights[vertex];
        float sum = 0.0F;
        for (std::size_t each = 0; each < 4; ++each) {
            if (!std::isfinite(weight[each]) || weight[each] < 0.0F) {
                return badSource("a skinned primitive's weight that is negative or not finite");
            }
            // A joint with no weight may name anything; it is kept as the first.
            if (weight[each] == 0.0F) {
                read[each] = 0;
            } else if (read[each] >= joints) {
                return badSource("a skinned primitive's joint past the skin's");
            }
            sum += weight[each];
        }
        if (sum <= 0.0F) {
            return badSource("a skinned primitive's vertex weighted to no joint");
        }
        for (float& value : weight) {
            value /= sum;
        }
        skin.influences.push_back({static_cast<std::uint16_t>(read[0]),
                                   static_cast<std::uint16_t>(read[1]),
                                   static_cast<std::uint16_t>(read[2]),
                                   static_cast<std::uint16_t>(read[3])});
        skin.weights.push_back(weight);
    }
    return {};
}

result::Status gather(const cgltf_primitive& primitive,
                      const Placement& placement,
                      const ImportLimits& limits,
                      MaterialMaker& materials,
                      Gathered& gathered) {
    const cgltf_accessor* kPositions = attribute(primitive, cgltf_attribute_type_position);
    if (kPositions == nullptr) {
        return badSource("a primitive with no POSITION");
    }
    const std::size_t kVertices = kPositions->count;
    const std::size_t kBase = gathered.mesh.positions.size();
    if (kVertices > limits.mesh.maximumVertices - kBase) {
        return overLimit("a mesh holds more vertices than its limits allow");
    }
    RAWFRAME_TRY_ASSIGN(std::vector<std::uint32_t> order, triangles(primitive, kVertices));
    if (order.empty()) {
        return {};
    }
    const std::size_t kFirst = gathered.mesh.indices.size();
    if (order.size() > limits.mesh.maximumIndices - kFirst) {
        return overLimit("a mesh holds more indices than its limits allow");
    }
    if (gathered.mesh.parts.size() == limits.mesh.maximumParts) {
        return overLimit("a mesh holds more parts than its limits allow");
    }
    RAWFRAME_TRY_ASSIGN(const std::vector<mesh::Vector3> kPoints, unpack<3>(*kPositions, cgltf_type_vec3));
    for (const mesh::Vector3& kPoint : kPoints) {
        gathered.mesh.positions.push_back(place(placement, kPoint));
    }
    if (!gathered.mesh.skin.joints.empty()) {
        RAWFRAME_TRY(influencesOf(primitive, kVertices, gathered.mesh.skin.joints.size(), gathered.mesh.skin));
    }
    const cgltf_accessor* kNormals = attribute(primitive, cgltf_attribute_type_normal);
    gathered.normals = gathered.normals && kNormals != nullptr && kNormals->count == kVertices;
    if (gathered.normals) {
        RAWFRAME_TRY_ASSIGN(const std::vector<mesh::Vector3> kValues, unpack<3>(*kNormals, cgltf_type_vec3));
        for (const mesh::Vector3& kNormal : kValues) {
            gathered.mesh.normals.push_back(turn(placement, kNormal));
        }
    }
    const cgltf_accessor* kUvs = attribute(primitive, cgltf_attribute_type_texcoord);
    gathered.uvs = gathered.uvs && kUvs != nullptr && kUvs->count == kVertices;
    if (gathered.uvs) {
        RAWFRAME_TRY_ASSIGN(const std::vector<mesh::Vector2> kValues, unpack<2>(*kUvs, cgltf_type_vec2));
        gathered.mesh.uvs.insert(gathered.mesh.uvs.end(), kValues.begin(), kValues.end());
    }
    for (std::size_t i = 0; i < order.size(); i += 3) {
        const std::uint32_t kB = order[i + 1];
        const std::uint32_t kC = order[i + 2];
        order[i + 1] = placement.mirrors ? kC : kB;
        order[i + 2] = placement.mirrors ? kB : kC;
    }
    for (const std::uint32_t kIndex : order) {
        gathered.mesh.indices.push_back(static_cast<std::uint32_t>(kBase + kIndex));
    }
    RAWFRAME_TRY_ASSIGN(const std::uint64_t kMaterial, materials.identityOf(primitive.material));
    gathered.mesh.parts.push_back({.firstIndex = static_cast<std::uint32_t>(kFirst),
                                   .indexCount = static_cast<std::uint32_t>(order.size()),
                                   .material = kMaterial});
    return {};
}

result::Status requireSupported(const cgltf_data& data) {
    for (cgltf_size i = 0; i < data.extensions_required_count; ++i) {
        const std::string_view kName = data.extensions_required[i];
        bool supported = false;
        for (const std::string_view kSupported : kSupportedExtensions) {
            supported = supported || kName == kSupported;
        }
        if (!supported) {
            return result::fail(result::ErrorClass::Unsupported,
                                mesh::kMeshDomain,
                                mesh::code(MeshError::UnsupportedExtension),
                                "the glTF requires the extension " + std::string{kName} + ", which is not supported");
        }
    }
    return {};
}

} // namespace

std::span<const std::string_view> supportedExtensions() noexcept {
    return kSupportedExtensions;
}

result::Result<Imported> importGltf(std::span<const std::byte> source,
                                    const ReadFile& read,
                                    const Identify& identify,
                                    const ImportLimits& limits) {
    if (source.size() > limits.maximumBytes) {
        return overLimit("a glTF larger than its limits allow");
    }
    Buffers buffers{.read = &read, .refused = {}};
    cgltf_options options{};
    options.file.read = &readBuffer;
    options.file.release = &releaseBuffer;
    options.file.user_data = &buffers;
    cgltf_data* parsed = nullptr;
    if (cgltf_parse(&options, source.data(), source.size(), &parsed) != cgltf_result_success) {
        return badSource("not glTF 2.0 this importer reads");
    }
    const std::unique_ptr<cgltf_data, Free> kData{parsed};
    RAWFRAME_TRY(requireSupported(*kData));
    for (cgltf_size i = 0; i < kData->buffers_count; ++i) {
        if (kData->buffers[i].size > limits.maximumBytes) {
            return overLimit("a glTF buffer larger than its limits allow");
        }
    }
    if (cgltf_load_buffers(&options, kData.get(), kSourceName) != cgltf_result_success) {
        if (!buffers.refused.empty()) {
            return badSource("the glTF's buffer " + buffers.refused + " cannot be read whole");
        }
        return badSource("a glTF buffer that cannot be read");
    }
    if (cgltf_validate(kData.get()) != cgltf_result_success) {
        return badSource("a glTF whose accessors, views, or nodes are not consistent");
    }
    const cgltf_scene* kScene =
        kData->scene != nullptr ? kData->scene : (kData->scenes_count != 0 ? &kData->scenes[0] : nullptr);
    if (kScene == nullptr) {
        return badSource("a glTF with no scene places no mesh");
    }
    Gathered gathered;
    MaterialMaker materials{read, identify};
    // Depth first, children in order; validation has refused cycles, and
    // the visit count bounds a node reached twice.
    std::vector<const cgltf_node*> pending;
    for (cgltf_size i = kScene->nodes_count; i > 0; --i) {
        pending.push_back(kScene->nodes[i - 1]);
    }
    std::size_t visits = 0;
    // The skin the first node with a mesh follows, which every other
    // follows too, or none (D508).
    std::optional<const cgltf_skin*> skin;
    while (!pending.empty()) {
        const cgltf_node* kNode = pending.back();
        pending.pop_back();
        if (++visits > kData->nodes_count) {
            return badSource("a glTF scene that reaches a node twice");
        }
        if (kNode->mesh != nullptr) {
            if (!skin.has_value()) {
                skin = kNode->skin;
                if (kNode->skin != nullptr) {
                    RAWFRAME_TRY_ASSIGN(gathered.mesh.skin.joints, jointsOf(*kNode->skin, limits));
                }
            } else if (*skin != kNode->skin) {
                return badSource("a glTF whose meshes follow different skins, or some none; a mesh follows one");
            }
            // A skinned mesh's node places nothing: its joints do, as glTF
            // has it.
            const Placement kPlacement =
                kNode->skin != nullptr
                    ? Placement{.matrix =
                                    {1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0},
                                .normal = {{{1.0, 0.0, 0.0}, {0.0, 1.0, 0.0}, {0.0, 0.0, 1.0}}}}
                    : placement(*kNode);
            for (cgltf_size i = 0; i < kNode->mesh->primitives_count; ++i) {
                RAWFRAME_TRY(gather(kNode->mesh->primitives[i], kPlacement, limits, materials, gathered));
            }
        }
        for (cgltf_size i = kNode->children_count; i > 0; --i) {
            pending.push_back(kNode->children[i - 1]);
        }
    }
    if (gathered.mesh.parts.empty()) {
        return badSource("a glTF whose scene places no triangles");
    }
    if (!gathered.normals) {
        gathered.mesh.normals.clear();
    }
    if (!gathered.uvs) {
        gathered.mesh.uvs.clear();
    }
    if (materials.samples() && gathered.mesh.uvs.empty()) {
        return result::fail(result::ErrorClass::Unsupported,
                            mesh::kMeshDomain,
                            mesh::code(MeshError::UnsupportedMaterial),
                            "textured materials on a mesh without TEXCOORD_0 on every primitive");
    }
    RAWFRAME_TRY(mesh::validate(gathered.mesh, limits.mesh));
    return Imported{
        .mesh = std::move(gathered.mesh), .materials = materials.takeMaterials(), .textures = materials.takeTextures()};
}

} // namespace rawframe::mesh_import
