#include "rawframe/cook/scatter.h"

#include "rawframe/cook/errors.h"
#include "rawframe/scatter/scatter.h"
#include "rawframe/terrain/terrain.h"

#include <optional>

namespace rawframe::cook {

namespace {

/// Takes no settings.
result::Result<std::string> normalize(const document::Value* settings) {
    if (settings != nullptr) {
        return result::fail(result::ErrorClass::InvalidArgument,
                            kCookDomain,
                            code(CookError::BadSidecar),
                            "a scatter takes no settings");
    }
    return std::string{};
}

std::string_view textOf(std::span<const std::byte> bytes) {
    return {reinterpret_cast<const char*>(bytes.data()), bytes.size()};
}

result::Result<Artifact> cookScatter(std::span<const std::byte> source, std::string_view, Reads& reads) {
    RAWFRAME_TRY_ASSIGN(const scatter::Scatter kScatter, scatter::parseScatter(textOf(source)));
    // The terrain, beside the scatter as its line names it, is an input:
    // a ground shaped again places the copies again.
    std::optional<terrain::Terrain> ground;
    if (!kScatter.terrain.empty()) {
        RAWFRAME_TRY_ASSIGN(const std::span<const std::byte> kGround, reads.file(kScatter.terrain));
        RAWFRAME_TRY_ASSIGN(ground, terrain::parseTerrain(textOf(kGround)));
    }
    RAWFRAME_TRY_ASSIGN(std::vector<scatter::Placement> placed,
                        scatter::placements(kScatter, ground.has_value() ? &*ground : nullptr));
    const std::string kPlaced =
        scatter::writePlaced(scatter::Placed{.prefab = kScatter.prefab, .placements = std::move(placed)});
    const std::span<const std::byte> kBytes = std::as_bytes(std::span{kPlaced.data(), kPlaced.size()});
    return Artifact{.type = content::ResourceTypeId{scatter::kPlacedType},
                    .representation = *content::RepresentationId::parse(scatter::kPlacedRepresentation),
                    .bytes = {kBytes.begin(), kBytes.end()}};
}

} // namespace

Importer scatterImporter() noexcept {
    return Importer{.identity = "rawframe.scatter", .normalize = &normalize, .cook = &cookScatter};
}

} // namespace rawframe::cook
