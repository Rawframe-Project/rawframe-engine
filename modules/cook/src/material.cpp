#include "rawframe/cook/material.h"

#include "rawframe/cook/errors.h"
#include "rawframe/material/material.h"

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

result::Result<Artifact> cookMaterial(std::span<const std::byte> source, std::string_view, Reads&) {
    const std::string_view kText{reinterpret_cast<const char*>(source.data()), source.size()};
    RAWFRAME_TRY_ASSIGN(const graph::Document kDocument, material::readMaterial(kText));
    RAWFRAME_TRY_ASSIGN(const material::Qualities kCompiled, material::compileQualities(kDocument));
    return Artifact{.type = content::ResourceTypeId{material::kMaterialType},
                    .representation = *content::RepresentationId::parse(material::kMaterialRepresentation),
                    .bytes = material::encode(kCompiled)};
}

} // namespace

Importer materialImporter() noexcept {
    return Importer{.identity = "rawframe.material", .normalize = &normalize, .cook = &cookMaterial};
}

} // namespace rawframe::cook
