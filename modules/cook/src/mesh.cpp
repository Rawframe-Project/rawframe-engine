#include "rawframe/cook/mesh.h"

#include "rawframe/cook/errors.h"
#include "rawframe/document/record.h"
#include "rawframe/material/material.h"
#include "rawframe/mesh/mesh.h"
#include "rawframe/mesh_import/import.h"
#include "rawframe/texture/texture.h"
#include "rawframe/texture_import/import.h"

#include <algorithm>
#include <array>

namespace rawframe::cook {

namespace {

using document::Record;
using document::Value;

constexpr std::array<std::string_view, 1> kSettingsFields = {"exact"};

/// `exact=0|1`: whether the textures its materials sample are kept exact,
/// as a texture's own setting keeps it.
result::Result<std::string> normalize(const Value* settings) {
    bool exact = false;
    if (settings != nullptr) {
        RAWFRAME_TRY_ASSIGN(const Record kRecord, Record::of(*settings, kSettingsFields, "$.settings"));
        RAWFRAME_TRY_ASSIGN(exact, kRecord.truth("exact", false));
    }
    return std::string{"exact="} + (exact ? "1" : "0");
}

/// The mesh, and each material its primitives draw with and each image
/// those sample as a subasset (D314); the game knows a subasset by the first
/// half of the resource its sidecar maps it to.
result::Result<Artifact> cookMesh(std::span<const std::byte> source, std::string_view settings, Reads& reads) {
    const mesh_import::ReadFile kRead = [&reads](std::string_view path) {
        return reads.file(path);
    };
    const mesh_import::Identify kIdentify = [&reads](std::string_view key) -> result::Result<std::uint64_t> {
        RAWFRAME_TRY_ASSIGN(const content::ResourceId kId, reads.subasset(key));
        if (kId.value.high == 0) {
            return std::unexpected<result::Error>{
                result::fail(result::ErrorClass::InvalidArgument,
                             kCookDomain,
                             code(CookError::BadSidecar),
                             "a subasset's resource whose first half is nought names nothing in a game")
                    .error()
                    .withContext("subasset", std::string{key})};
        }
        return kId.value.high;
    };
    RAWFRAME_TRY_ASSIGN(const mesh_import::Imported kImported, mesh_import::importGltf(source, kRead, kIdentify));
    RAWFRAME_TRY_ASSIGN(std::vector<std::byte> bytes, mesh::encode(kImported.mesh));
    Artifact made{.type = content::ResourceTypeId{mesh::kMeshType},
                  .representation = *content::RepresentationId::parse(mesh::kMeshRepresentation),
                  .bytes = std::move(bytes)};
    for (const mesh_import::ImportedMaterial& kMaterial : kImported.materials) {
        made.subassets.push_back(
            Subasset{.key = kMaterial.key,
                     .type = content::ResourceTypeId{material::kMaterialType},
                     .representation = *content::RepresentationId::parse(material::kMaterialRepresentation),
                     .bytes = material::encode(kMaterial.made)});
    }
    for (const mesh_import::ImportedTexture& kTexture : kImported.textures) {
        RAWFRAME_TRY_ASSIGN(const texture_import::Image kImage, texture_import::decodeImage(kTexture.image));
        RAWFRAME_TRY_ASSIGN(
            const texture::Texture kCooked,
            texture_import::cookTexture(
                kImage, {.srgb = kTexture.color, .exact = settings.contains("exact=1"), .levels = true}));
        RAWFRAME_TRY_ASSIGN(std::vector<std::byte> texels, texture::encode(kCooked));
        made.subassets.push_back(
            Subasset{.key = kTexture.key,
                     .type = content::ResourceTypeId{texture::kTextureType},
                     .representation = *content::RepresentationId::parse(texture::kTextureRepresentation),
                     .bytes = std::move(texels)});
    }
    std::ranges::sort(made.subassets, {}, &Subasset::key);
    return made;
}

} // namespace

Importer meshImporter() noexcept {
    return Importer{.identity = "rawframe.mesh", .normalize = &normalize, .cook = &cookMesh};
}

} // namespace rawframe::cook
