#include "rawframe/cook/scene.h"

#include "rawframe/cook/errors.h"
#include "rawframe/scene/scene.h"

namespace rawframe::cook {

namespace {

/// Takes no settings.
result::Result<std::string> normalize(const document::Value* settings) {
    if (settings != nullptr) {
        return std::unexpected<result::Error>{result::fail(result::ErrorClass::InvalidArgument,
                                                           kCookDomain,
                                                           code(CookError::BadSidecar),
                                                           "a scene takes no settings")
                                                  .error()};
    }
    return std::string{};
}

result::Result<Artifact> cookScene(std::span<const std::byte> source, std::string_view, Reads&) {
    const std::string_view kText{reinterpret_cast<const char*>(source.data()), source.size()};
    RAWFRAME_TRY(scene::readScene(kText));
    return Artifact{.type = content::ResourceTypeId{scene::kSceneType},
                    .representation = *content::RepresentationId::parse(scene::kSceneRepresentation),
                    .bytes = {source.begin(), source.end()}};
}

} // namespace

Importer sceneImporter() noexcept {
    return Importer{.identity = "rawframe.scene", .normalize = &normalize, .cook = &cookScene};
}

} // namespace rawframe::cook
