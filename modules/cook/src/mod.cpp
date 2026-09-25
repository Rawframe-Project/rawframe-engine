#include "rawframe/cook/mod.h"

#include "rawframe/cook/errors.h"
#include "rawframe/world_kest/cooked_mod.h"
#include "rawframe/world_kest/mod.h"

namespace rawframe::cook {

namespace {

std::unexpected<result::Error> refuse(std::string_view why, std::string_view name) {
    return std::unexpected<result::Error>{
        result::fail(result::ErrorClass::InvalidArgument, kCookDomain, code(CookError::BadReference), why)
            .error()
            .withContext("name", name)};
}

std::string_view textOf(std::span<const std::byte> bytes) {
    return {reinterpret_cast<const char*>(bytes.data()), bytes.size()};
}

/// Takes no settings.
result::Result<std::string> normalize(const document::Value* settings) {
    if (settings != nullptr) {
        return std::unexpected<result::Error>{result::fail(result::ErrorClass::InvalidArgument,
                                                           kCookDomain,
                                                           code(CookError::BadSidecar),
                                                           "a mod description takes no settings")
                                                  .error()};
    }
    return std::string{};
}

result::Result<Artifact> cookMod(std::span<const std::byte> source, std::string_view, Reads& reads) {
    const std::string_view kText = textOf(source);
    RAWFRAME_TRY_ASSIGN(const world_kest::ModDescription kDescription, world_kest::parseMod(kText));
    world_kest::CookedMod mod{.text = std::string{kText}};
    // Each contributed scene: the resource its sidecar names, cooked by
    // rawframe.scene, once however many points it is given to.
    for (const world_kest::ModContribution& contribution : kDescription.contributions) {
        if (mod.scene(contribution.scene) != nullptr) {
            continue;
        }
        auto sidecarBytes = reads.file(contribution.scene + std::string{content::kSidecarSuffix});
        if (!sidecarBytes.has_value()) {
            return refuse("a scene a mod contributes has a sidecar", contribution.scene);
        }
        RAWFRAME_TRY_ASSIGN(const content::Sidecar kSidecar, content::readSidecar(textOf(*sidecarBytes)));
        if (kSidecar.importer != "rawframe.scene") {
            return refuse("a scene a mod contributes is cooked by rawframe.scene", contribution.scene);
        }
        mod.scenes.push_back(world_kest::CookedGameScene{.path = contribution.scene, .scene = kSidecar.id.value});
    }
    RAWFRAME_TRY_ASSIGN(const std::string kWritten, world_kest::writeCookedMod(mod));
    const auto kBytes = std::as_bytes(std::span{kWritten.data(), kWritten.size()});
    return Artifact{.type = content::ResourceTypeId{world_kest::kCookedModType},
                    .representation = *content::RepresentationId::parse(world_kest::kCookedModRepresentation),
                    .bytes = {kBytes.begin(), kBytes.end()}};
}

} // namespace

Importer modImporter() noexcept {
    return Importer{.identity = "rawframe.mod", .normalize = &normalize, .cook = &cookMod};
}

} // namespace rawframe::cook
