#include "rawframe/mesh_import/import.h"

#include "rawframe/mesh/errors.h"

#include <array>
#include <cgltf.h>
#include <cmath>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace rawframe::mesh_import {

namespace {

using mesh::MeshError;

constexpr std::array<std::string_view, 1> kSupportedExtensions = {"KHR_mesh_quantization"};

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

result::Status
gather(const cgltf_primitive& primitive, const Placement& placement, const ImportLimits& limits, Gathered& gathered) {
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
    gathered.mesh.parts.push_back(
        {.firstIndex = static_cast<std::uint32_t>(kFirst), .indexCount = static_cast<std::uint32_t>(order.size())});
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

result::Result<mesh::Mesh>
importGltf(std::span<const std::byte> source, const ReadFile& read, const ImportLimits& limits) {
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
    // Depth first, children in order; validation has refused cycles, and
    // the visit count bounds a node reached twice.
    std::vector<const cgltf_node*> pending;
    for (cgltf_size i = kScene->nodes_count; i > 0; --i) {
        pending.push_back(kScene->nodes[i - 1]);
    }
    std::size_t visits = 0;
    while (!pending.empty()) {
        const cgltf_node* kNode = pending.back();
        pending.pop_back();
        if (++visits > kData->nodes_count) {
            return badSource("a glTF scene that reaches a node twice");
        }
        if (kNode->mesh != nullptr) {
            const Placement kPlacement = placement(*kNode);
            for (cgltf_size i = 0; i < kNode->mesh->primitives_count; ++i) {
                RAWFRAME_TRY(gather(kNode->mesh->primitives[i], kPlacement, limits, gathered));
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
    RAWFRAME_TRY(mesh::validate(gathered.mesh, limits.mesh));
    return std::move(gathered.mesh);
}

} // namespace rawframe::mesh_import
