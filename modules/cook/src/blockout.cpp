#include "rawframe/cook/blockout.h"

#include "rawframe/blockout/blockout.h"
#include "rawframe/cook/errors.h"
#include "rawframe/mesh/mesh.h"

namespace rawframe::cook {

namespace {

/// Takes no settings.
result::Result<std::string> normalize(const document::Value* settings) {
    if (settings != nullptr) {
        return result::fail(result::ErrorClass::InvalidArgument,
                            kCookDomain,
                            code(CookError::BadSidecar),
                            "a blockout takes no settings");
    }
    return std::string{};
}

result::Result<Artifact> cookBlockout(std::span<const std::byte> source, std::string_view, Reads&) {
    const std::string_view kText{reinterpret_cast<const char*>(source.data()), source.size()};
    RAWFRAME_TRY_ASSIGN(const blockout::Blockout kRead, blockout::parseBlockout(kText));
    RAWFRAME_TRY_ASSIGN(const mesh::Mesh kMade, blockout::meshOf(kRead));
    RAWFRAME_TRY_ASSIGN(std::vector<std::byte> bytes, mesh::encode(kMade));
    return Artifact{.type = content::ResourceTypeId{mesh::kMeshType},
                    .representation = *content::RepresentationId::parse(mesh::kMeshRepresentation),
                    .bytes = std::move(bytes)};
}

} // namespace

Importer blockoutImporter() noexcept {
    return Importer{.identity = "rawframe.blockout", .normalize = &normalize, .cook = &cookBlockout};
}

} // namespace rawframe::cook
