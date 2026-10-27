#include "loading.h"

#include "rawframe/composition/composition.h"
#include "rawframe/content/errors.h"
#include "rawframe/game_content/game_content.h"
#include "rawframe/material/instance.h"
#include "rawframe/render_scene/errors.h"
#include "rawframe/render_scene/materials.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <utility>

namespace rawframe::render_scene {

MaterialBlob sceneBlobOf(const std::array<float, material::kBlobFloats>& blob,
                         const std::array<float, material::kParameterFloats>& parameters) noexcept {
    MaterialBlob made{};
    std::ranges::copy(blob, made.begin());
    std::ranges::copy(parameters, made.begin() + material::kBlobFloats);
    return made;
}

MaterialBlob noMaterial() noexcept {
    material::Material plain;
    plain.surface.baseColor = {1, 1, 1};
    return sceneBlobOf(material::blobOf(plain));
}

namespace {

/// A material's cooked bytes of `type`, read and waited for.
result::Result<content::VerifiedContent>
readCooked(content::ContentStore& store, base::Bits128 id, base::Bits128 type) {
    RAWFRAME_TRY_ASSIGN(
        execution::AsyncHandle<content::VerifiedContent> read,
        store.read(content::ResourceRef{.id = content::ResourceId{id}, .type = content::ResourceTypeId{type}}));
    return execution::toResult(read.wait(),
                               execution::CancellationMapping{.errorClass = result::ErrorClass::Unavailable,
                                                              .domain = kRenderSceneDomain,
                                                              .code = code(RenderSceneError::MaterialUnreadable),
                                                              .description = "a material's read was cancelled"});
}

/// A surface material as the scene draws it, read from `store`:
/// what `sceneMaterialOf` makes of its cooked bytes, an instance's base
/// read from the same store.
result::Result<SceneMaterial> readMaterial(content::ContentStore& store,
                                           std::uint64_t identity,
                                           base::Bits128 id,
                                           material::Quality quality,
                                           Programs& programs) {
    const CookedMaterials kRead = [&store](base::Bits128 at) -> result::Result<CookedMaterial> {
        RAWFRAME_TRY_ASSIGN(const content::VerifiedContent kCooked, readCooked(store, at, material::kMaterialType));
        return CookedMaterial{.representation = std::string{kCooked.descriptor().representation.text()},
                              .revision = kCooked.descriptor().digest,
                              .bytes = {kCooked.bytes().begin(), kCooked.bytes().end()}};
    };
    RAWFRAME_TRY_ASSIGN(const CookedMaterial kCooked, kRead(id));
    return sceneMaterialOf(identity, id, kCooked, quality, programs, kRead);
}

/// Whether a read found no resource of that identity, or (`other`) one of
/// another type.
bool missing(const result::Error& error, content::ContentError why) {
    return error.domain() == content::kContentDomain && error.code() == content::code(why);
}

} // namespace

result::Result<SceneConfiguration> readConfiguration(const composition::Configuration& configuration) {
    SceneConfiguration read;
    if (configuration.text("scene.client").has_value()) {
        RAWFRAME_TRY_ASSIGN(const std::uint64_t kClient, configuration.unsignedInteger("scene.client", 0));
        read.client = static_cast<std::size_t>(kClient);
    }
    RAWFRAME_TRY_ASSIGN(const std::uint64_t kWidth, configuration.unsignedInteger("scene.width", 1280));
    RAWFRAME_TRY_ASSIGN(const std::uint64_t kHeight, configuration.unsignedInteger("scene.height", 720));
    if (kWidth == 0 || kHeight == 0 || kWidth > 1U << 16U || kHeight > 1U << 16U) {
        return std::unexpected<result::Error>{result::fail(result::ErrorClass::InvalidArgument,
                                                           composition::kCompositionDomain,
                                                           code(composition::CompositionError::BadConfiguration),
                                                           "scene.width and scene.height are 1 to 65536 pixels")
                                                  .error()};
    }
    read.width = static_cast<std::uint32_t>(kWidth);
    read.height = static_cast<std::uint32_t>(kHeight);
    // The sun's shadows (the typed cascade configuration, a
    // profile's values).
    const ShadowSettings kShadowDefaults;
    RAWFRAME_TRY_ASSIGN(const std::uint64_t kCascades,
                        configuration.unsignedInteger("scene.shadow_cascades", kShadowDefaults.cascades));
    RAWFRAME_TRY_ASSIGN(const std::uint64_t kSide,
                        configuration.unsignedInteger("scene.shadow_side", kShadowDefaults.side));
    RAWFRAME_TRY_ASSIGN(
        const std::uint64_t kDistance,
        configuration.unsignedInteger("scene.shadow_distance", static_cast<std::uint64_t>(kShadowDefaults.distance)));
    if (kCascades > 4 || kSide < 64 || kSide > 4096 || kDistance < 1 || kDistance > 10000) {
        return std::unexpected<result::Error>{
            result::fail(result::ErrorClass::InvalidArgument,
                         composition::kCompositionDomain,
                         code(composition::CompositionError::BadConfiguration),
                         "scene.shadow_cascades is 0 to 4, scene.shadow_side 64 to 4096 texels, and "
                         "scene.shadow_distance 1 to 10000 meters")
                .error()};
    }
    // Its filter, a class of the ladder.
    const std::string_view kFilter = configuration.text("scene.shadow_filter").value_or("soft");
    if (kFilter != "hardware" && kFilter != "soft" && kFilter != "contact_hardening") {
        return std::unexpected<result::Error>{
            result::fail(result::ErrorClass::InvalidArgument,
                         composition::kCompositionDomain,
                         code(composition::CompositionError::BadConfiguration),
                         "scene.shadow_filter is hardware, soft, or contact_hardening")
                .error()};
    }
    read.shadows = ShadowSettings{.cascades = static_cast<std::uint32_t>(kCascades),
                                  .distance = static_cast<float>(kDistance),
                                  .logarithmicBlend = kShadowDefaults.logarithmicBlend,
                                  .side = static_cast<std::uint32_t>(kSide),
                                  .filter = kFilter == "contact_hardening" ? ShadowFilter::ContactHardening
                                            : kFilter == "soft"            ? ShadowFilter::Soft
                                                                           : ShadowFilter::Hardware};
    // The punctual lights' shadow atlas: its side, a power of two,
    // or nought for none; its squares from a quarter of it, at most 512
    // texels, down to a quarter of that.
    RAWFRAME_TRY_ASSIGN(const std::uint64_t kAtlas,
                        configuration.unsignedInteger("scene.light_shadow_side", LightShadowSettings{}.side));
    if (kAtlas != 0 && (kAtlas < 64 || kAtlas > 8192 || (kAtlas & (kAtlas - 1)) != 0)) {
        return std::unexpected<result::Error>{
            result::fail(result::ErrorClass::InvalidArgument,
                         composition::kCompositionDomain,
                         code(composition::CompositionError::BadConfiguration),
                         "scene.light_shadow_side is nought or a power of two from 64 to 8192 texels")
                .error()};
    }
    const auto kLargest = static_cast<std::uint32_t>(std::min<std::uint64_t>(512, kAtlas / 4));
    read.lightShadows =
        LightShadowSettings{.side = static_cast<std::uint32_t>(kAtlas), .largest = kLargest, .smallest = kLargest / 4};
    // The anti-aliasing method: temporal
    // unless another is named; multisampling's samples, two or four.
    const std::string_view kMethod = configuration.text("scene.anti_aliasing").value_or("taa");
    if (kMethod != "taa" && kMethod != "fxaa" && kMethod != "msaa" && kMethod != "off") {
        return std::unexpected<result::Error>{result::fail(result::ErrorClass::InvalidArgument,
                                                           composition::kCompositionDomain,
                                                           code(composition::CompositionError::BadConfiguration),
                                                           "scene.anti_aliasing is taa, fxaa, msaa, or off")
                                                  .error()};
    }
    read.antiAliasing = kMethod == "off"    ? AntiAliasing::Off
                        : kMethod == "fxaa" ? AntiAliasing::Fxaa
                        : kMethod == "msaa" ? AntiAliasing::Msaa
                                            : AntiAliasing::Taa;
    RAWFRAME_TRY_ASSIGN(const std::uint64_t kSamples,
                        configuration.unsignedInteger("scene.msaa_samples", kDefaultMultisamples));
    if (kSamples != 2 && kSamples != 4) {
        return std::unexpected<result::Error>{result::fail(result::ErrorClass::InvalidArgument,
                                                           composition::kCompositionDomain,
                                                           code(composition::CompositionError::BadConfiguration),
                                                           "scene.msaa_samples is 2 or 4")
                                                  .error()};
    }
    read.multisamples = static_cast<std::uint32_t>(kSamples);
    // The quality axis: which of their qualities the
    // game's materials are drawn at, the high unless another is named.
    const std::string_view kQuality = configuration.text("scene.quality").value_or("high");
    if (kQuality != "low" && kQuality != "medium" && kQuality != "high") {
        return std::unexpected<result::Error>{result::fail(result::ErrorClass::InvalidArgument,
                                                           composition::kCompositionDomain,
                                                           code(composition::CompositionError::BadConfiguration),
                                                           "scene.quality is low, medium, or high")
                                                  .error()};
    }
    read.quality = kQuality == "low"      ? material::Quality::Low
                   : kQuality == "medium" ? material::Quality::Medium
                                          : material::Quality::High;
    // The render scale: each local player's view drawn at
    // this many hundredths of its region's pixels each way, and scaled
    // to its region; every one unless asked.
    RAWFRAME_TRY_ASSIGN(const std::uint64_t kPercent, configuration.unsignedInteger("scene.render_scale_percent", 100));
    if (kPercent < 25 || kPercent > 100) {
        return std::unexpected<result::Error>{result::fail(result::ErrorClass::InvalidArgument,
                                                           composition::kCompositionDomain,
                                                           code(composition::CompositionError::BadConfiguration),
                                                           "scene.render_scale_percent is 25 to 100")
                                                  .error()};
    }
    read.renderScalePercent = static_cast<std::uint32_t>(kPercent);
    // Where a client allows fewer, the scale follows how the frames keep up
    // with the device, down to this many.
    RAWFRAME_TRY_ASSIGN(const std::uint64_t kLeast,
                        configuration.unsignedInteger("scene.render_scale_least_percent", kPercent));
    if (kLeast < 25 || kLeast > kPercent) {
        return std::unexpected<result::Error>{
            result::fail(result::ErrorClass::InvalidArgument,
                         composition::kCompositionDomain,
                         code(composition::CompositionError::BadConfiguration),
                         "scene.render_scale_least_percent is 25 to scene.render_scale_percent")
                .error()};
    }
    read.leastRenderScalePercent = static_cast<std::uint32_t>(kLeast);
    return read;
}

void readMaterials(content::ContentStore& store,
                   const world_kest::GameFiles& files,
                   material::Quality quality,
                   Programs& programs,
                   std::vector<SceneMaterial>& materials,
                   std::vector<ScenePostProcessMaterial>& postProcesses,
                   std::vector<std::pair<std::string, std::string>>& unread) {
    for (const world_kest::GameMaterialResource& each : files.materials()) {
        auto read = readMaterial(store, each.id, each.material, quality, programs);
        // One of another type may be a post process.
        if (!read.has_value() && missing(read.error(), content::ContentError::ResourceTypeMismatch)) {
            auto cooked = readCooked(store, each.material, material::kPostProcessType);
            auto process = cooked.has_value()
                               ? material::decodePostProcess(cooked->bytes())
                               : result::Result<material::PostProcess>{std::unexpected{std::move(cooked).error()}};
            if (process.has_value()) {
                postProcesses.push_back({.id = each.id,
                                         .insertion = process->insertion,
                                         .blob = material::blobOf(*process),
                                         .texture = sceneTextureOf(process->sampled)});
                continue;
            }
            // One of a third type is a canvas material,
            // the canvas's to draw.
            if (missing(process.error(), content::ContentError::ResourceTypeMismatch)) {
                continue;
            }
            read = std::unexpected{std::move(process).error()};
        }
        if (read.has_value()) {
            materials.push_back(std::move(*read));
        } else if (!each.subasset || !missing(read.error(), content::ContentError::ResourceNotFound)) {
            // A mesh's subasset its source no longer has names
            // no resource and is no material.
            unread.emplace_back(each.path, std::string{read.error().description()});
        }
    }
}

std::vector<content::ContentDigest> materialRevisions(const content::ContentStore& store,
                                                      const world_kest::GameFiles& files) {
    const std::shared_ptr<const content::ContentCatalog> kCatalog = store.catalog();
    std::vector<content::ContentDigest> made;
    for (const world_kest::GameMaterialResource& kEach : files.materials()) {
        const auto kFound = kCatalog != nullptr ? kCatalog->lookup(content::ResourceId{kEach.material})
                                                : result::Result<const content::ContentDescriptor*>{nullptr};
        made.push_back(kFound.has_value() && *kFound != nullptr ? (*kFound)->digest : content::ContentDigest{});
    }
    return made;
}

result::Status readGameMaterials(composition::ParticipantContext& context,
                                 const world_kest::GameFiles& files,
                                 material::Quality quality,
                                 Programs& programs,
                                 std::vector<SceneMaterial>& materials,
                                 std::vector<ScenePostProcessMaterial>& postProcesses,
                                 std::vector<std::pair<std::string, std::string>>& unread) {
    if (!files.materials().empty() && context.has(game_content::kGameContent.name)) {
        RAWFRAME_TRY_ASSIGN(game_content::GameContent * content, context.capability(game_content::kGameContent));
        if (content->held()) {
            const std::array<content::AdmittedRepresentation, 4> kAdmitted = {
                content::AdmittedRepresentation{
                    .type = content::ResourceTypeId{material::kMaterialType},
                    .representation = *content::RepresentationId::parse(material::kMaterialRepresentation)},
                content::AdmittedRepresentation{
                    .type = content::ResourceTypeId{material::kMaterialType},
                    .representation = *content::RepresentationId::parse(material::kInstanceRepresentation)},
                content::AdmittedRepresentation{
                    .type = content::ResourceTypeId{material::kMaterialType},
                    .representation = *content::RepresentationId::parse(material::kProgramRepresentation)},
                content::AdmittedRepresentation{
                    .type = content::ResourceTypeId{material::kPostProcessType},
                    .representation = *content::RepresentationId::parse(material::kPostProcessRepresentation)}};
            RAWFRAME_TRY(content->admit(kAdmitted));
            readMaterials(content->store(), files, quality, programs, materials, postProcesses, unread);
        }
    }
    return {};
}

} // namespace rawframe::render_scene
