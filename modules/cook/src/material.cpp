#include "rawframe/cook/material.h"

#include "rawframe/cook/errors.h"
#include "rawframe/material/material.h"
#include "rawframe/material/post_process.h"

#include <algorithm>
#include <cstdint>
#include <string_view>

namespace rawframe::cook {

namespace {

/// Takes no settings.
result::Result<std::string> normalize(const document::Value* settings) {
    if (settings != nullptr) {
        return std::unexpected<result::Error>{result::fail(result::ErrorClass::InvalidArgument,
                                                           kCookDomain,
                                                           code(CookError::BadSidecar),
                                                           "a material takes no settings")
                                                  .error()};
    }
    return std::string{};
}

/// SPEC-0026's `variants_per_material`: the quality axis's three, the one
/// axis generation 1 has (D319).
constexpr std::int64_t kVariantsPerMaterial = 3;

/// SPEC-0026's variant report (D319): the variants, the axes that make
/// them and the quality axis's cardinality, the variants the quality axis
/// adds, and the headroom under the ceiling. A variant is a distinct
/// material: generation 1 draws every one with the one program.
std::vector<std::pair<std::string, std::int64_t>> reportOf(const material::Qualities& made) {
    std::int64_t variants = 1;
    for (std::size_t at = 1; at < made.size(); ++at) {
        const bool kSeen =
            std::ranges::find(made.begin(), made.begin() + static_cast<std::ptrdiff_t>(at), made.at(at)) !=
            made.begin() + static_cast<std::ptrdiff_t>(at);
        variants += kSeen ? 0 : 1;
    }
    return {{"variants", variants},
            {"axes", variants > 1 ? 1 : 0},
            {"qualityCardinality", static_cast<std::int64_t>(made.size())},
            {"qualityVariants", variants - 1},
            {"variantsHeadroom", kVariantsPerMaterial - variants}};
}

result::Result<Artifact> cookMaterial(std::span<const std::byte> source, std::string_view, Reads&) {
    const std::string_view kText{reinterpret_cast<const char*>(source.data()), source.size()};
    RAWFRAME_TRY_ASSIGN(const graph::Document kDocument, material::readMaterial(kText));
    RAWFRAME_TRY_ASSIGN(const material::Qualities kCompiled, material::compileQualities(kDocument));
    return Artifact{.type = content::ResourceTypeId{material::kMaterialType},
                    .representation = *content::RepresentationId::parse(material::kMaterialRepresentation),
                    .bytes = material::encode(kCompiled),
                    .subassets = {},
                    .report = reportOf(kCompiled)};
}

/// A post process folds to one form (D348): one variant.
result::Result<Artifact> cookPostProcess(std::span<const std::byte> source, std::string_view, Reads&) {
    const std::string_view kText{reinterpret_cast<const char*>(source.data()), source.size()};
    RAWFRAME_TRY_ASSIGN(const graph::Document kDocument, material::readPostProcess(kText));
    RAWFRAME_TRY_ASSIGN(const material::PostProcess kCompiled, material::compilePostProcess(kDocument));
    return Artifact{.type = content::ResourceTypeId{material::kPostProcessType},
                    .representation = *content::RepresentationId::parse(material::kPostProcessRepresentation),
                    .bytes = material::encodePostProcess(kCompiled),
                    .subassets = {},
                    .report = {{"variants", 1}, {"axes", 0}, {"variantsHeadroom", kVariantsPerMaterial - 1}}};
}

} // namespace

Importer materialImporter() noexcept {
    return Importer{.identity = "rawframe.material", .normalize = &normalize, .cook = &cookMaterial};
}

Importer postProcessImporter() noexcept {
    return Importer{.identity = "rawframe.postprocess", .normalize = &normalize, .cook = &cookPostProcess};
}

} // namespace rawframe::cook
