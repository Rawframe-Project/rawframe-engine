#include "rawframe/cook/mesh.h"

#include "rawframe/cook/errors.h"
#include "rawframe/mesh/mesh.h"
#include "rawframe/mesh_import/import.h"

namespace rawframe::cook {

namespace {

/// Takes no settings.
result::Result<std::string> normalize(const document::Value* settings) {
    if (settings != nullptr) {
        return std::unexpected<result::Error>{result::fail(result::ErrorClass::InvalidArgument,
                                                           kCookDomain,
                                                           code(CookError::BadSidecar),
                                                           "a mesh takes no settings")
                                                  .error()};
    }
    return std::string{};
}

result::Result<Artifact> cookMesh(std::span<const std::byte> source, std::string_view, Reads& reads) {
    const mesh_import::ReadFile kRead = [&reads](std::string_view path) {
        return reads.file(path);
    };
    RAWFRAME_TRY_ASSIGN(const mesh::Mesh kMesh, mesh_import::importGltf(source, kRead));
    RAWFRAME_TRY_ASSIGN(std::vector<std::byte> bytes, mesh::encode(kMesh));
    return Artifact{.type = content::ResourceTypeId{mesh::kMeshType},
                    .representation = *content::RepresentationId::parse(mesh::kMeshRepresentation),
                    .bytes = std::move(bytes)};
}

} // namespace

Importer meshImporter() noexcept {
    return Importer{.identity = "rawframe.mesh", .normalize = &normalize, .cook = &cookMesh};
}

} // namespace rawframe::cook
