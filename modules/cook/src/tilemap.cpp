#include "rawframe/cook/tilemap.h"

#include "rawframe/cook/errors.h"
#include "rawframe/tilemap/tilemap.h"

namespace rawframe::cook {

namespace {

/// Takes no settings.
result::Result<std::string> normalize(const document::Value* settings) {
    if (settings != nullptr) {
        return result::fail(result::ErrorClass::InvalidArgument,
                            kCookDomain,
                            code(CookError::BadSidecar),
                            "a tilemap takes no settings");
    }
    return std::string{};
}

result::Result<Artifact> cookTilemap(std::span<const std::byte> source, std::string_view, Reads&) {
    RAWFRAME_TRY(tilemap::parseTilemap({reinterpret_cast<const char*>(source.data()), source.size()}));
    return Artifact{.type = content::ResourceTypeId{tilemap::kTilemapType},
                    .representation = *content::RepresentationId::parse(tilemap::kTilemapRepresentation),
                    .bytes = {source.begin(), source.end()}};
}

} // namespace

Importer tilemapImporter() noexcept {
    return Importer{.identity = "rawframe.tilemap", .normalize = &normalize, .cook = &cookTilemap};
}

} // namespace rawframe::cook
