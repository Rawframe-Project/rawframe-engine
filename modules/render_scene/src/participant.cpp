#include "rawframe/composition/composition.h"
#include "rawframe/composition/configuration.h"
#include "rawframe/content/errors.h"
#include "rawframe/game_content/game_content.h"
#include "rawframe/game_textures/game_textures.h"
#include "rawframe/material/material.h"
#include "rawframe/physics3d/components.h"
#include "rawframe/render_scene/errors.h"
#include "rawframe/render_scene/frames.h"
#include "rawframe/render_scene/registrar.h"
#include "rawframe/render_scene/scene.h"
#include "rawframe/view/players.h"
#include "rawframe/world/column_query.h"
#include "rawframe/world_kest/game_files.h"
#include "rawframe/world_replication/client_worlds.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>

namespace rawframe::render_scene {

namespace {

constexpr diagnostics::EventIdentity kSceneSummary{"scene", "scene_summary"};
// The particles' apart: with them the summary would pass the 32 fields a
// record carries (D361).
constexpr diagnostics::EventIdentity kParticlesSummary{"scene", "scene_particles_summary"};
constexpr std::string_view kProvided[] = {kSceneFrames.name};
constexpr diagnostics::EventIdentity kMaterialUnread{"scene", "material_unread"};
constexpr diagnostics::EventIdentity kTextureUnknown{"scene", "material_texture_unknown"};
constexpr diagnostics::EventIdentity kTexturesUnavailable{"scene", "textures_unavailable"};
constexpr diagnostics::EventIdentity kTextureUnread{"scene", "texture_unavailable"};
constexpr diagnostics::EventIdentity kTextureReloaded{"scene", "texture_reloaded"};
constexpr diagnostics::EventIdentity kTexturesRead{"scene", "textures_read"};
constexpr diagnostics::EventIdentity kPictureUnknown{"scene", "picture_unknown"};
constexpr diagnostics::EventIdentity kPictureUnread{"scene", "picture_unavailable"};
constexpr diagnostics::EventIdentity kViewRefused{"scene", "view_refused"};
constexpr diagnostics::EventIdentity kViewsSummary{"scene", "scene_views_summary"};
/// The decoded levels the materials' textures may hold.
constexpr std::uint64_t kTextureBudgetBytes = std::uint64_t{256} * 1024 * 1024;
constexpr std::string_view kMaybe[] = {world_replication::kClientWorlds.name,
                                       world_kest::kGameFiles.name,
                                       game_content::kGameContent.name,
                                       view::kPlayerViews.name};

/// A material's texture as the scene binds it.
SceneTexture sceneTextureOf(const material::SampledTexture& texture) {
    return {.id = texture.id, .filter = texture.filter, .address = texture.address};
}

/// A material's cooked bytes of `type`, read and waited for (D303).
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

/// A surface material, decoded at `quality` (D303).
result::Result<material::Material>
readMaterial(content::ContentStore& store, base::Bits128 id, material::Quality quality) {
    RAWFRAME_TRY_ASSIGN(const content::VerifiedContent kRead, readCooked(store, id, material::kMaterialType));
    RAWFRAME_TRY_ASSIGN(const material::Qualities kQualities, material::decode(kRead.bytes()));
    return kQualities.at(static_cast<std::size_t>(quality));
}

/// Whether a read found no resource of that identity, or (`other`) one of
/// another type.
bool missing(const result::Error& error, content::ContentError why) {
    return error.domain() == content::kContentDomain && error.code() == content::code(why);
}

/// A client's view without a camera of its own: behind its player and
/// above, looking a little down, in a sunny day's exposure.
constexpr Camera kDefaultCamera{
    .offsetX = 0, .offsetY = 3, .offsetZ = 6, .yaw = 0, .pitch = -0.35F, .fovY = 1, .near = 0.1F, .exposure = 15};

/// Draws one client's mirrored World in 3D each frame through its player's
/// camera: the extract stage in `presentation_extract`, the queue stage in
/// `present`. Idle without a game that has models or without clients. The
/// frame it queued is lent to a device's recording
/// (`rawframe.render_scene.frames`).
class SceneParticipant final : public composition::Participant, public SceneFrames {
public:
    result::Status load(composition::ParticipantContext& context) {
        // Its keys are read first, so a scene left idle does not leave them
        // unread.
        const composition::Configuration& configuration = context.configuration();
        std::optional<std::size_t> client;
        if (configuration.text("scene.client").has_value()) {
            RAWFRAME_TRY_ASSIGN(const std::uint64_t kClient, configuration.unsignedInteger("scene.client", 0));
            client = static_cast<std::size_t>(kClient);
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
        width_ = static_cast<std::uint32_t>(kWidth);
        height_ = static_cast<std::uint32_t>(kHeight);
        // The sun's shadows (ADR-0051's typed cascade configuration, a
        // profile's values; D289).
        const ShadowSettings kShadowDefaults;
        RAWFRAME_TRY_ASSIGN(const std::uint64_t kCascades,
                            configuration.unsignedInteger("scene.shadow_cascades", kShadowDefaults.cascades));
        RAWFRAME_TRY_ASSIGN(const std::uint64_t kSide,
                            configuration.unsignedInteger("scene.shadow_side", kShadowDefaults.side));
        RAWFRAME_TRY_ASSIGN(const std::uint64_t kDistance,
                            configuration.unsignedInteger("scene.shadow_distance",
                                                          static_cast<std::uint64_t>(kShadowDefaults.distance)));
        if (kCascades > 4 || kSide < 64 || kSide > 4096 || kDistance < 1 || kDistance > 10000) {
            return std::unexpected<result::Error>{
                result::fail(result::ErrorClass::InvalidArgument,
                             composition::kCompositionDomain,
                             code(composition::CompositionError::BadConfiguration),
                             "scene.shadow_cascades is 0 to 4, scene.shadow_side 64 to 4096 texels, and "
                             "scene.shadow_distance 1 to 10000 meters")
                    .error()};
        }
        // Its filter, a class of ADR-0051's ladder (D330).
        const std::string_view kFilter = configuration.text("scene.shadow_filter").value_or("soft");
        if (kFilter != "hardware" && kFilter != "soft" && kFilter != "contact_hardening") {
            return std::unexpected<result::Error>{
                result::fail(result::ErrorClass::InvalidArgument,
                             composition::kCompositionDomain,
                             code(composition::CompositionError::BadConfiguration),
                             "scene.shadow_filter is hardware, soft, or contact_hardening")
                    .error()};
        }
        shadows_ = ShadowSettings{.cascades = static_cast<std::uint32_t>(kCascades),
                                  .distance = static_cast<float>(kDistance),
                                  .logarithmicBlend = kShadowDefaults.logarithmicBlend,
                                  .side = static_cast<std::uint32_t>(kSide),
                                  .filter = kFilter == "contact_hardening" ? ShadowFilter::ContactHardening
                                            : kFilter == "soft"            ? ShadowFilter::Soft
                                                                           : ShadowFilter::Hardware};
        // The punctual lights' shadow atlas (D292): its side, a power of two,
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
        lightShadows_ = LightShadowSettings{
            .side = static_cast<std::uint32_t>(kAtlas), .largest = kLargest, .smallest = kLargest / 4};
        // ADR-0051's anti-aliasing method (D291, D296, D343): temporal
        // unless another is named; multisampling's samples, two or four.
        const std::string_view kMethod = configuration.text("scene.anti_aliasing").value_or("taa");
        if (kMethod != "taa" && kMethod != "fxaa" && kMethod != "msaa" && kMethod != "off") {
            return std::unexpected<result::Error>{result::fail(result::ErrorClass::InvalidArgument,
                                                               composition::kCompositionDomain,
                                                               code(composition::CompositionError::BadConfiguration),
                                                               "scene.anti_aliasing is taa, fxaa, msaa, or off")
                                                      .error()};
        }
        antiAliasing_ = kMethod == "off"    ? AntiAliasing::Off
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
        multisamples_ = static_cast<std::uint32_t>(kSamples);
        // SPEC-0026's quality axis (D318): which of their qualities the
        // game's materials are drawn at, the high unless another is named.
        const std::string_view kQuality = configuration.text("scene.quality").value_or("high");
        if (kQuality != "low" && kQuality != "medium" && kQuality != "high") {
            return std::unexpected<result::Error>{result::fail(result::ErrorClass::InvalidArgument,
                                                               composition::kCompositionDomain,
                                                               code(composition::CompositionError::BadConfiguration),
                                                               "scene.quality is low, medium, or high")
                                                      .error()};
        }
        quality_ = kQuality == "low"      ? material::Quality::Low
                   : kQuality == "medium" ? material::Quality::Medium
                                          : material::Quality::High;
        if (!context.has(world_kest::kGameFiles.name) || !context.has(world_replication::kClientWorlds.name)) {
            return {};
        }
        RAWFRAME_TRY_ASSIGN(const world_kest::GameFiles* files, context.capability(world_kest::kGameFiles));
        if (!files->named()) {
            return {};
        }
        std::string report;
        auto program = files->compile(files->description().program, {}, &report);
        if (!program.has_value()) {
            return std::unexpected<result::Error>{std::move(program)
                                                      .error()
                                                      .withContext("program", files->description().program)
                                                      .withContext("report", report)};
        }
        auto game = loadGameScene(*files, **program);
        if (!game.has_value()) {
            // A game without models draws nothing in 3D, and needs nothing
            // drawn.
            if (game.error().domain() == kRenderSceneDomain &&
                game.error().code() == code(RenderSceneError::NoModels)) {
                return {};
            }
            return std::unexpected<result::Error>{std::move(game).error()};
        }
        RAWFRAME_TRY_ASSIGN(clients_, context.capability(world_replication::kClientWorlds));
        client_ = client;
        // The local players' views, told where the host lends them (D367);
        // a scene of a client named by configuration is no player's.
        if (!client.has_value() && context.has(view::kPlayerViews.name)) {
            RAWFRAME_TRY_ASSIGN(views_, context.capability(view::kPlayerViews));
        }
        // Split-screen (D362): the process's local players, each in its
        // region by the game's layout for their count, the first in the
        // first.
        if (const std::size_t kPlayers = clients_->localPlayers(); kPlayers > 1) {
            const auto& kLayouts = files->description().layouts;
            const auto kLayout = std::ranges::find(kLayouts, kPlayers, &world_kest::GameLayout::players);
            if (client.has_value() || kLayout == kLayouts.end()) {
                return std::unexpected<result::Error>{
                    result::fail(result::ErrorClass::InvalidArgument,
                                 composition::kCompositionDomain,
                                 code(composition::CompositionError::BadConfiguration),
                                 "local players are shown by the game's layout for their count, which it must have, "
                                 "and scene.client names none")
                        .error()};
            }
            client_ = 0;
            regions_ = kLayout->regions;
            for (std::size_t other = 1; other < kPlayers; ++other) {
                localPlayers_.push_back(LocalPlayer{.client = other});
            }
            regionFrames_.resize(regions_.size());
        }
        // A constrained aspect (D369): one player's view is placed in the
        // window as a split-screen player's is in its region.
        aspect_ = files->description().aspect;
        if (aspect_.has_value() && regions_.empty()) {
            regions_.push_back(world_kest::GameRegion{});
            regionFrames_.resize(1);
        }
        cameraComponents_ = game->cameras;
        viewComponent_ = game->view;
        for (const world_kest::GameRenderTexture& kTexture : files->description().renderTextures) {
            textureViews_.push_back(
                TextureView{.id = kTexture.id,
                            .width = kTexture.width,
                            .height = kTexture.height,
                            .onDemand = kTexture.update == world_kest::RenderTextureUpdate::OnDemand});
        }
        textureFrames_.resize(textureViews_.size());
        autoExposureComponent_ = game->autoExposure;
        gradingComponent_ = game->grading;
        occlusionComponent_ = game->occlusion;
        bloomComponent_ = game->bloom;
        reflectionsComponent_ = game->reflections;
        motionBlurComponent_ = game->motionBlur;
        depthOfFieldComponent_ = game->depthOfField;
        contactShadowsComponent_ = game->contactShadows;
        postProcessComponents_ = game->postProcesses;
        gameMeshes_ = game->meshes.size();
        // The game's materials from its cooked content, surfaces and post
        // processes (D349); one that cannot be read is drawn as none, and
        // said so when the scene starts.
        std::vector<SceneMaterial> materials;
        std::vector<ScenePostProcessMaterial> postProcesses;
        if (!files->materials().empty() && context.has(game_content::kGameContent.name)) {
            RAWFRAME_TRY_ASSIGN(game_content::GameContent * content, context.capability(game_content::kGameContent));
            if (content->held()) {
                const std::array<content::AdmittedRepresentation, 2> kAdmitted = {
                    content::AdmittedRepresentation{
                        .type = content::ResourceTypeId{material::kMaterialType},
                        .representation = *content::RepresentationId::parse(material::kMaterialRepresentation)},
                    content::AdmittedRepresentation{
                        .type = content::ResourceTypeId{material::kPostProcessType},
                        .representation = *content::RepresentationId::parse(material::kPostProcessRepresentation)}};
                RAWFRAME_TRY(content->admit(kAdmitted));
                for (const world_kest::GameMaterialResource& each : files->materials()) {
                    auto read = readMaterial(content->store(), each.material, quality_);
                    // One of another type may be a post process.
                    if (!read.has_value() && missing(read.error(), content::ContentError::ResourceTypeMismatch)) {
                        auto cooked = readCooked(content->store(), each.material, material::kPostProcessType);
                        auto process =
                            cooked.has_value()
                                ? material::decodePostProcess(cooked->bytes())
                                : result::Result<material::PostProcess>{std::unexpected{std::move(cooked).error()}};
                        if (process.has_value()) {
                            postProcesses.push_back({.id = each.id,
                                                     .insertion = process->insertion,
                                                     .blob = material::blobOf(*process),
                                                     .texture = sceneTextureOf(process->sampled)});
                            continue;
                        }
                        // One of a third type is a canvas material (D356),
                        // the canvas's to draw.
                        if (missing(process.error(), content::ContentError::ResourceTypeMismatch)) {
                            continue;
                        }
                        read = std::unexpected{std::move(process).error()};
                    }
                    if (read.has_value()) {
                        materials.push_back({.id = each.id,
                                             .blob = material::blobOf(*read),
                                             .translucent = read->blend == material::Blend::Translucent,
                                             .textures = {.base = sceneTextureOf(read->textures.base),
                                                          .packed = sceneTextureOf(read->textures.packed),
                                                          .emission = sceneTextureOf(read->textures.emission),
                                                          .normal = sceneTextureOf(read->textures.normal)}});
                    } else if (!each.subasset || !missing(read.error(), content::ContentError::ResourceNotFound)) {
                        // A mesh's subasset its source no longer has names
                        // no resource and is no material (D314).
                        unreadMaterials_.emplace_back(each.path, std::string{read.error().description()});
                    }
                }
            }
        }
        gameMaterials_ = materials.size();
        RAWFRAME_TRY(readTextures(context, *files, materials, postProcesses));
        settings_ = SceneSettings{.models = std::move(game->models),
                                  .sun = game->sun,
                                  .sky = game->sky,
                                  .points = std::move(game->points),
                                  .spots = std::move(game->spots),
                                  .probes = std::move(game->probes),
                                  .decals = std::move(game->decals),
                                  .emitters = std::move(game->emitters),
                                  .trails = std::move(game->trails),
                                  .beams = std::move(game->beams),
                                  .meshes = std::move(game->meshes),
                                  .materials = std::move(materials),
                                  .postProcesses = std::move(postProcesses),
                                  .shadows = shadows_,
                                  .lightShadows = lightShadows_,
                                  .antiAliasing = antiAliasing_,
                                  .multisamples = multisamples_};
        return {};
    }

    result::Status start(composition::ParticipantContext& context) noexcept override {
        emitter_ = context.emitter();
        for (const auto& [kPath, kReason] : unreadMaterials_) {
            emitter_.log(diagnostics::Severity::Warning,
                         kMaterialUnread,
                         "a material could not be read: its models are drawn with none",
                         {diagnostics::field("material", kPath), diagnostics::field("reason", kReason)});
        }
        for (const auto& [kPath, kTexture] : unknownTextures_) {
            emitter_.log(diagnostics::Severity::Warning,
                         kTextureUnknown,
                         "a material samples a texture the game does not declare: it is sampled as white",
                         {diagnostics::field("material", kPath), diagnostics::field("texture", kTexture)});
        }
        if (unreadTextures_.has_value()) {
            emitter_.log(diagnostics::Severity::Warning,
                         kTexturesUnavailable,
                         "the materials' textures could not be asked for: they are sampled as white",
                         {diagnostics::field("reason", *unreadTextures_)});
        }
        return {};
    }

    void runHostPhase(composition::HostPhase phase, const composition::HostFrame& frame) noexcept override {
        if (clients_ == nullptr) {
            return;
        }
        if (phase == composition::HostPhase::PresentationExtract) {
            queued_ = nullptr;
            for (TextureFrame& each : textureFrames_) {
                each.frame = nullptr;
            }
            for (RegionFrame& each : regionFrames_) {
                each.frame = nullptr;
            }
            ++tick_;
            updateTextures();
            extract();
        } else if (phase == composition::HostPhase::Present && extracted_) {
            extracted_ = false;
            camera_.aspect = static_cast<float>(width_) / static_cast<float>(height_);
            if (!regions_.empty()) {
                presentPlayers(frame.now);
            }
            // The seconds since the frame before, on the Host's timeline
            // (D293): nought for the first.
            camera_.elapsed =
                presented_.has_value() ? static_cast<float>((frame.now - *presented_).nanoseconds) / 1e9F : 0.0F;
            presented_ = frame.now;
            presentViews(frame.now);
            const SceneFrame& kFrame = scene_->queue(camera_);
            askPicture(kFrame.lights.environment);
            askPicture(kFrame.grading.table);
            for (const render_scene::SceneProbe& kProbe : kFrame.probes) {
                askPicture(kProbe.environment);
            }
            for (const render_scene::SceneDecal& kDecal : kFrame.decals) {
                askPicture(kDecal.texture);
                askPicture(kDecal.normal);
            }
            queued_ = &kFrame;
            if (!regionFrames_.empty() && regionFrames_[0].width != 0) {
                regionFrames_[0].frame = &kFrame;
            }
            tellViews();
            ++frames_;
            drawn_ += kFrame.drawn;
            culled_ += kFrame.culled;
            hidden_ += kFrame.hidden;
            malformed_ += kFrame.malformed;
            unknownMeshes_ += kFrame.unknownMeshes;
            unknownMaterials_ += kFrame.unknownMaterials;
            overLimit_ += kFrame.overLimit;
            mostDraws_ = std::max(mostDraws_, kFrame.draws.size());
            lightsLit_ += kFrame.lights3d.size();
            lightsCulled_ += kFrame.lightsCulled;
            lightsOverLimit_ += kFrame.lightsOverLimit;
            clusterOverflow_ += kFrame.clusterOverflow;
            shadowSquares_ += kFrame.lightShadows.slots.size();
            shadowsEvicted_ += kFrame.lightShadows.evicted;
            decalsDrawn_ += kFrame.decals.size();
            decalsCulled_ += kFrame.decalsCulled + kFrame.decalsOverLimit;
            particles_.add(kFrame.particles, emitter_);
        }
    }

    void stop() noexcept override {
        if (clients_ == nullptr) {
            return;
        }
        std::vector<diagnostics::Field> fields = {
            diagnostics::field("frames", frames_),
            diagnostics::field("framesViewed", viewed_),
            diagnostics::field("viewWidth", width_),
            diagnostics::field("viewHeight", height_),
            diagnostics::field("modelsDrawn", drawn_),
            diagnostics::field("culled", culled_),
            diagnostics::field("hidden", hidden_),
            diagnostics::field("malformed", malformed_),
            diagnostics::field("unknownMeshes", unknownMeshes_),
            diagnostics::field("overLimit", overLimit_),
            diagnostics::field("mostDraws", static_cast<std::uint64_t>(mostDraws_)),
            diagnostics::field("gameMeshes", static_cast<std::uint64_t>(gameMeshes_)),
            diagnostics::field("gameMaterials", static_cast<std::uint64_t>(gameMaterials_)),
            diagnostics::field("unknownMaterials", unknownMaterials_),
            diagnostics::field("materialTextures", static_cast<std::uint64_t>(sampled_)),
            diagnostics::field("texturesReady",
                               static_cast<std::uint64_t>(textures_ != nullptr ? textures_->counts().ready : 0)),
            diagnostics::field("pictures", picturesRead_),
            diagnostics::field("picturesReady", picturesReady()),
            diagnostics::field("lightsLit", lightsLit_),
            diagnostics::field("lightsCulled", lightsCulled_),
            diagnostics::field("lightsOverLimit", lightsOverLimit_),
            diagnostics::field("clusterOverflow", clusterOverflow_),
            diagnostics::field("shadowSquares", shadowSquares_),
            diagnostics::field("shadowsEvicted", shadowsEvicted_),
            diagnostics::field("decalsDrawn", decalsDrawn_),
            diagnostics::field("decalsCulled", decalsCulled_),
            diagnostics::field("players", static_cast<std::uint64_t>(localPlayers_.size() + 1)),
            diagnostics::field("playerFrames", playerFrames_)};
        emitter_.log(diagnostics::Severity::Info, kSceneSummary, "what one client's scene drew", fields);
        if (!textureViews_.empty()) {
            emitter_.log(diagnostics::Severity::Info,
                         kViewsSummary,
                         "what one client's render textures were drawn with",
                         {diagnostics::field("drawn", viewsDrawn_),
                          diagnostics::field("kept", viewsKept_),
                          diagnostics::field("missed", viewsMissed_),
                          diagnostics::field("leftOut", viewsLeftOut_),
                          diagnostics::field("refused", viewsRefused_)});
        }
        const auto kParticles = particles_.fields();
        emitter_.log(
            diagnostics::Severity::Info, kParticlesSummary, "what one client's scene's emitters drew", kParticles);
    }

    composition::CapabilityObject provide(std::string_view capability) noexcept override {
        if (capability == kSceneFrames.name) {
            return composition::provideAs<SceneFrames>(*this);
        }
        return {};
    }

    const SceneFrame* queued() const noexcept override {
        return queued_;
    }

    std::span<const TextureFrame> textureFrames() const noexcept override {
        return textureFrames_;
    }

    std::span<const RegionFrame> regionFrames() const noexcept override {
        return regionFrames_;
    }

    void missed(std::uint64_t id) noexcept override {
        const auto kView = std::ranges::find(textureViews_, id, &TextureView::id);
        if (kView != textureViews_.end() && kView->offered != nullptr) {
            kView->resend = true;
            ++viewsMissed_;
        }
    }

    std::array<std::uint8_t, 3> bars() const noexcept override {
        return aspect_.has_value() ? aspect_->bars : std::array<std::uint8_t, 3>{};
    }

    std::uint32_t width() const noexcept override {
        return width_;
    }

    std::uint32_t height() const noexcept override {
        return height_;
    }

    void resize(std::uint32_t width, std::uint32_t height) noexcept override {
        if (width == 0 || height == 0) {
            return;
        }
        width_ = width;
        height_ = height;
    }

    const SceneFrame* queueFrom(const SceneCamera& camera) noexcept override {
        if (scene_ == nullptr) {
            return nullptr;
        }
        queued_ = &scene_->queue(camera);
        return queued_;
    }

    std::span<const ProbeInstance> probes() const noexcept override {
        return scene_ != nullptr ? scene_->extractedProbes() : std::span<const ProbeInstance>{};
    }

    std::shared_ptr<const mesh::Mesh> mesh(std::uint64_t id) const override {
        return scene_ != nullptr ? scene_->mesh(id) : nullptr;
    }

    std::shared_ptr<const texture::Texture> texture(std::uint64_t id) const override {
        if (const auto kPicture = pictures_.find(id); kPicture != pictures_.end()) {
            return kPicture->second != nullptr ? kPicture->second->texture(id, tick_) : nullptr;
        }
        return textures_ != nullptr ? textures_->texture(id, tick_) : nullptr;
    }

private:
    /// Asks for the textures `materials` and `postProcesses` sample, of
    /// those the game declares, decoded on a CPU worker (D309); a material
    /// sampling one it does not declare samples white, and is said so at
    /// start.
    result::Status readTextures(composition::ParticipantContext& context,
                                const world_kest::GameFiles& files,
                                std::vector<SceneMaterial>& materials,
                                std::vector<ScenePostProcessMaterial>& postProcesses) {
        std::set<std::uint64_t> sampled;
        std::vector<std::pair<std::uint64_t, SceneTexture*>> named;
        for (SceneMaterial& each : materials) {
            for (SceneTexture* texture :
                 {&each.textures.base, &each.textures.packed, &each.textures.emission, &each.textures.normal}) {
                named.emplace_back(each.id, texture);
            }
        }
        for (ScenePostProcessMaterial& each : postProcesses) {
            named.emplace_back(each.id, &each.texture);
        }
        for (const auto& [kMaterial, texture] : named) {
            if (texture->id == 0) {
                continue;
            }
            // A render texture is drawn, not read (D361).
            if (std::ranges::contains(
                    files.description().renderTextures, texture->id, &world_kest::GameRenderTexture::id)) {
                continue;
            }
            if (std::ranges::find(files.textures(), texture->id, &world_kest::GameTextureResource::id) ==
                files.textures().end()) {
                const auto kPath =
                    std::ranges::find(files.materials(), kMaterial, &world_kest::GameMaterialResource::id);
                unknownTextures_.emplace_back(kPath != files.materials().end() ? kPath->path : std::string{},
                                              graph::nodeIdText(texture->id));
                texture->id = 0;
                continue;
            }
            sampled.insert(texture->id);
        }
        sampled_ = sampled.size();
        if (!context.has(game_content::kGameContent.name) || context.cpuExecutor() == nullptr) {
            return {};
        }
        RAWFRAME_TRY_ASSIGN(game_content::GameContent * content, context.capability(game_content::kGameContent));
        if (!content->held()) {
            return {};
        }
        RAWFRAME_TRY(content->admit(game_textures::textureRepresentations()));
        // What reading the sky's picture needs, when the World names one.
        reading_ = Reading{.store = &content->store(),
                           .cpu = context.cpuExecutor(),
                           .owner = context.owner(),
                           .scope = &context.scope(),
                           .clock = &context.clock()};
        declaredTextures_ = files.textures();
        if (sampled.empty()) {
            return {};
        }
        std::vector<world_kest::GameTextureResource> declared;
        for (const world_kest::GameTextureResource& each : files.textures()) {
            if (sampled.contains(each.id)) {
                declared.push_back(each);
            }
        }
        auto textures = game_textures::GameTextures::create(content->store(),
                                                            *context.cpuExecutor(),
                                                            context.owner(),
                                                            context.scope(),
                                                            context.clock(),
                                                            std::move(declared),
                                                            kTextureBudgetBytes);
        if (textures.has_value()) {
            textures_ = std::move(*textures);
        } else {
            unreadTextures_ = std::string{textures.error().description()};
        }
        return {};
    }

    /// Asks for a picture the World names, the sky's, a reflection
    /// probe's, a decal's or its normals' (D342), or a grading table
    /// (D344), on its first naming (D322, D325, D339): a texture the game
    /// declares, read alone and held, so a game's other textures are never
    /// read for it. One it does not declare, or that cannot be asked for,
    /// is none, and is said so once.
    void askPicture(std::uint64_t id) noexcept {
        if (id == 0 || pictures_.contains(id)) {
            return;
        }
        std::unique_ptr<game_textures::GameTextures>& reader = pictures_[id];
        const auto kDeclared = std::ranges::find(declaredTextures_, id, &world_kest::GameTextureResource::id);
        if (kDeclared == declaredTextures_.end()) {
            emitter_.log(diagnostics::Severity::Warning,
                         kPictureUnknown,
                         "the World names a picture the game does not declare: it has none",
                         {diagnostics::field("texture", graph::nodeIdText(id))});
            return;
        }
        if (!reading_.has_value()) {
            return;
        }
        ++picturesRead_;
        auto made = game_textures::GameTextures::create(*reading_->store,
                                                        *reading_->cpu,
                                                        reading_->owner,
                                                        *reading_->scope,
                                                        *reading_->clock,
                                                        {*kDeclared},
                                                        kTextureBudgetBytes);
        if (!made.has_value()) {
            emitter_.log(diagnostics::Severity::Warning,
                         kPictureUnread,
                         "a picture the World names could not be asked for: it has none",
                         {diagnostics::field("texture", graph::nodeIdText(id)),
                          diagnostics::field("reason", std::string{made.error().description()})});
            return;
        }
        reader = std::move(*made);
    }

    /// The pictures read and ready.
    [[nodiscard]] std::uint64_t picturesReady() const noexcept {
        std::uint64_t ready = 0;
        for (const auto& [kId, kReader] : pictures_) {
            ready += kReader != nullptr ? kReader->counts().ready : 0;
        }
        return ready;
    }

    /// Takes finished reads and reloads; a texture that fails is sampled
    /// as white, and an environment that fails is none.
    void updateTextures() noexcept {
        for (const auto& [kPicture, kReader] : pictures_) {
            if (kReader == nullptr) {
                continue;
            }
            for (const auto& [kId, kError] : kReader->update(tick_).failed) {
                emitter_.log(diagnostics::Severity::Warning,
                             kPictureUnread,
                             "a picture the World names could not be read: it has none",
                             {diagnostics::field("texture", graph::nodeIdText(kId)),
                              diagnostics::field("reason", std::string{kError.description()})});
            }
        }
        if (textures_ == nullptr) {
            return;
        }
        const game_textures::TextureChanges kChanges = textures_->update(tick_);
        for (const auto& [kId, kError] : kChanges.failed) {
            emitter_.log(diagnostics::Severity::Warning,
                         kTextureUnread,
                         "a material's texture could not be read: it is sampled as white",
                         {diagnostics::field("texture", graph::nodeIdText(kId)),
                          diagnostics::field("reason", std::string{kError.description()})});
        }
        for (const std::uint64_t kId : kChanges.reloaded) {
            emitter_.log(diagnostics::Severity::Info,
                         kTextureReloaded,
                         "a material's texture was replaced by its new revision",
                         {diagnostics::field("texture", graph::nodeIdText(kId))});
        }
        if (kChanges.read) {
            emitter_.log(diagnostics::Severity::Info,
                         kTexturesRead,
                         "the materials' textures were read",
                         {diagnostics::field("textures", static_cast<std::uint64_t>(textures_->counts().ready))});
        }
    }

    /// The client's World copied out, and the eye moved to its player,
    /// through the player's camera if it has one.
    void extract() noexcept {
        const std::size_t kClient = client_.value_or(clients_->playerClient().value_or(0));
        const world_replication::ClientView kView = clients_->client(kClient);
        if (kView.world == nullptr) {
            return;
        }
        if (scene_ == nullptr) {
            auto made = Scene::create(kView.world->registry(), settings_);
            if (!made.has_value()) {
                clients_ = nullptr;
                return;
            }
            scene_ = std::move(*made);
        }
        scene_->extract(*kView.world);
        extracted_ = true;
        if (kView.owned.isNull() || !kView.world->alive(kView.owned)) {
            return;
        }
        if (readCamera(*kView.world, kView.owned, camera_)) {
            ++viewed_;
        }
        extractViews(*kView.world);
        extractPlayers();
    }

    /// The other local players' Worlds (D362), each extracted into its own
    /// scene and its camera read from its player.
    void extractPlayers() {
        for (LocalPlayer& each : localPlayers_) {
            each.extracted = false;
            const world_replication::ClientView kView = clients_->client(each.client);
            if (kView.world == nullptr) {
                continue;
            }
            if (each.scene == nullptr) {
                auto made = Scene::create(kView.world->registry(), settings_);
                if (!made.has_value()) {
                    continue;
                }
                each.scene = std::move(*made);
            }
            each.scene->extract(*kView.world);
            if (!kView.owned.isNull() && kView.world->alive(kView.owned)) {
                static_cast<void>(readCamera(*kView.world, kView.owned, each.camera));
            }
            each.extracted = true;
        }
    }

    /// Each local player's region placed in the window as it is now, the
    /// first's camera given its aspect, the others' frames queued (D362).
    void presentPlayers(execution::MonotonicInstant now) {
        for (std::size_t at = 0; at < regions_.size(); ++at) {
            const world_kest::GameRegion& kRegion = regions_[at];
            const world_kest::RegionPixels kWhole = world_kest::pixelsOf(kRegion, width_, height_);
            const world_kest::RegionPixels kPixels =
                aspect_.has_value() ? world_kest::constrainedTo(kWhole, *aspect_) : kWhole;
            regionFrames_[at] =
                RegionFrame{.x = kPixels.x, .y = kPixels.y, .width = kPixels.width, .height = kPixels.height};
        }
        if (regionFrames_[0].width != 0 && regionFrames_[0].height != 0) {
            camera_.aspect = static_cast<float>(regionFrames_[0].width) / static_cast<float>(regionFrames_[0].height);
        }
        for (std::size_t at = 0; at < localPlayers_.size(); ++at) {
            LocalPlayer& each = localPlayers_[at];
            RegionFrame& region = regionFrames_[at + 1];
            if (!each.extracted || region.width == 0 || region.height == 0) {
                each.presented.reset();
                continue;
            }
            each.camera.aspect = static_cast<float>(region.width) / static_cast<float>(region.height);
            each.camera.elapsed =
                each.presented.has_value() ? static_cast<float>((now - *each.presented).nanoseconds) / 1e9F : 0.0F;
            each.presented = now;
            const SceneFrame& kFrame = each.scene->queue(each.camera);
            askPicture(kFrame.lights.environment);
            askPicture(kFrame.grading.table);
            for (const SceneProbe& kProbe : kFrame.probes) {
                askPicture(kProbe.environment);
            }
            for (const SceneDecal& kDecal : kFrame.decals) {
                askPicture(kDecal.texture);
                askPicture(kDecal.normal);
            }
            region.frame = &kFrame;
            ++playerFrames_;
        }
    }

    /// Each local player's view as this frame derived it (D367): the
    /// first's region and camera, then the others', a player not presented
    /// having none.
    void tellViews() {
        if (views_ == nullptr) {
            return;
        }
        const auto kRegionOf = [&](std::size_t at) {
            if (regionFrames_.empty()) {
                return view::Region{};
            }
            const RegionFrame& kRegion = regionFrames_[at];
            const auto kWidth = static_cast<float>(width_);
            const auto kHeight = static_cast<float>(height_);
            return view::Region{.left = static_cast<float>(kRegion.x) / kWidth,
                                .top = static_cast<float>(kRegion.y) / kHeight,
                                .width = static_cast<float>(kRegion.width) / kWidth,
                                .height = static_cast<float>(kRegion.height) / kHeight};
        };
        views_->tell(0, kRegionOf(0), perspectiveOf(camera_));
        for (std::size_t at = 0; at < localPlayers_.size(); ++at) {
            if (localPlayers_[at].presented.has_value()) {
                views_->tell(at + 1, kRegionOf(at + 1), perspectiveOf(localPlayers_[at].camera));
            } else {
                views_->forgetPerspective(at + 1);
            }
        }
    }

    /// The camera on `entity` and its effects read into `camera`, the eye
    /// placed by the entity's pose; whether it has a camera (else it looks
    /// as the default does).
    bool readCamera(world::World& world, world::EntityHandle entity, SceneCamera& camera) const {
        Camera view = kDefaultCamera;
        bool found = false;
        // The first of the game's camera components the entity has
        // (D361: a player's and its views' may differ).
        for (const schema::ComponentTypeId kComponent : cameraComponents_) {
            if (const auto kCamera = world.registry().find(kComponent)) {
                if (const auto* placed = static_cast<const Camera*>(world.getErased(entity, *kCamera))) {
                    view = *placed;
                    found = true;
                    break;
                }
            }
        }
        camera.yaw = view.yaw;
        camera.pitch = view.pitch;
        camera.fovY = view.fovY;
        camera.near = view.near;
        camera.exposure = view.exposure;
        camera.tonemapper = view.tonemapper;
        const auto kRead = [&world, entity]<typename T>(const std::optional<schema::ComponentTypeId>& component,
                                                        std::optional<T>& into) {
            into.reset();
            if (component.has_value()) {
                if (const auto kId = world.registry().find(*component)) {
                    if (const auto* asked = static_cast<const T*>(world.getErased(entity, *kId))) {
                        into = *asked;
                    }
                }
            }
        };
        kRead(gradingComponent_, camera.grading);
        kRead(reflectionsComponent_, camera.reflections);
        kRead(motionBlurComponent_, camera.motionBlur);
        kRead(depthOfFieldComponent_, camera.depthOfField);
        kRead(contactShadowsComponent_, camera.contactShadows);
        kRead(bloomComponent_, camera.bloom);
        kRead(occlusionComponent_, camera.occlusion);
        kRead(autoExposureComponent_, camera.metering);
        // Its post processes, in the game's order (D349).
        camera.postProcesses.clear();
        for (const schema::ComponentTypeId kComponent : postProcessComponents_) {
            if (const auto kProcess = world.registry().find(kComponent)) {
                if (const auto* asked = static_cast<const PostProcess*>(world.getErased(entity, *kProcess))) {
                    camera.postProcesses.push_back(*asked);
                }
            }
        }
        if (const auto kPose = world.registry().key<physics3d::Pose3D>()) {
            if (const auto* pose = world.get(entity, *kPose)) {
                camera.eye = {pose->x + view.offsetX, pose->y + view.offsetY, pose->z + view.offsetZ};
            }
        }
        return found;
    }

    /// Each render texture's view (D361): of the entities whose view names
    /// it, the one of the lowest order, its World extracted by the render
    /// texture's own scene and its camera read when it is due: every frame,
    /// or on demand when the texture shows another view or request. Two of
    /// the lowest order show none, and are told; the others are left out.
    void extractViews(world::World& world) {
        for (TextureView& each : textureViews_) {
            each.entity.reset();
            each.order = 0;
            each.request = 0;
            each.tied = false;
            each.due = false;
        }
        if (!viewComponent_.has_value() || textureViews_.empty()) {
            return;
        }
        const auto kView = world.registry().find(*viewComponent_);
        if (!kView.has_value()) {
            return;
        }
        if (!viewQuery_.has_value()) {
            const std::array<world::ColumnTerm, 1> kTerms = {world::ColumnTerm{*kView, world::Access::Read}};
            auto query = world::ColumnQuery::resolve(kTerms, world.registry());
            if (!query.has_value()) {
                return;
            }
            viewQuery_ = std::move(*query);
        }
        viewQuery_->forEachChunk(world, [&](const world::ColumnChunk& chunk) {
            for (std::size_t row = 0; row < chunk.entities.size(); ++row) {
                View asked;
                std::memcpy(&asked, chunk.columns[0] + (row * sizeof(View)), sizeof(View));
                const auto kTarget = std::ranges::find(textureViews_, asked.target, &TextureView::id);
                if (kTarget == textureViews_.end()) {
                    continue;
                }
                if (kTarget->entity.has_value()) {
                    ++viewsLeftOut_;
                    if (asked.order > kTarget->order) {
                        continue;
                    }
                    kTarget->tied = asked.order == kTarget->order;
                    if (kTarget->tied) {
                        continue;
                    }
                }
                kTarget->entity = chunk.entities[row];
                kTarget->order = asked.order;
                kTarget->request = asked.request;
                kTarget->tied = false;
            }
        });
        for (TextureView& each : textureViews_) {
            if (!each.entity.has_value()) {
                continue;
            }
            if (each.tied) {
                ++viewsRefused_;
                emitter_.log(diagnostics::Severity::Warning,
                             kViewRefused,
                             "two views of one order name a render texture: it shows neither",
                             {diagnostics::field("texture", graph::nodeIdText(each.id)),
                              diagnostics::field("order", static_cast<std::int64_t>(each.order))});
                each.entity.reset();
                continue;
            }
            each.due = !each.onDemand || !each.shown.has_value() || each.shown->entity != *each.entity ||
                       each.shown->request != each.request;
            if (!each.due) {
                ++viewsKept_;
                continue;
            }
            each.resend = false;
            if (each.scene == nullptr) {
                auto made = Scene::create(world.registry(), settings_);
                if (!made.has_value()) {
                    each.entity.reset();
                    continue;
                }
                each.scene = std::move(*made);
            }
            each.scene->extract(world);
            static_cast<void>(readCamera(world, *each.entity, each.camera));
        }
    }

    /// Each render texture's view queued when due, at its own aspect and
    /// on its own clock (D361); one drawn on demand taken as shown, and
    /// its frame offered again as it was when the device missed it.
    void presentViews(execution::MonotonicInstant now) {
        for (std::size_t at = 0; at < textureViews_.size(); ++at) {
            TextureView& each = textureViews_[at];
            textureFrames_[at] = TextureFrame{.id = each.id, .width = each.width, .height = each.height};
            if (!each.entity.has_value() || each.scene == nullptr) {
                each.presented.reset();
                each.shown.reset();
                each.offered = nullptr;
                each.resend = false;
                continue;
            }
            if (!each.due) {
                // A frame the device missed offered again as it was.
                if (each.resend) {
                    textureFrames_[at].frame = each.offered;
                    each.resend = false;
                }
                continue;
            }
            each.shown = Shown{.entity = *each.entity, .request = each.request};
            each.camera.aspect = static_cast<float>(each.width) / static_cast<float>(each.height);
            each.camera.elapsed =
                each.presented.has_value() ? static_cast<float>((now - *each.presented).nanoseconds) / 1e9F : 0.0F;
            each.presented = now;
            const SceneFrame& kFrame = each.scene->queue(each.camera);
            askPicture(kFrame.lights.environment);
            askPicture(kFrame.grading.table);
            textureFrames_[at].frame = &kFrame;
            each.offered = &kFrame;
            ++viewsDrawn_;
        }
    }

    world_replication::ClientWorlds* clients_ = nullptr;
    std::optional<std::size_t> client_;
    SceneSettings settings_;
    ShadowSettings shadows_;
    SceneCamera camera_;
    std::vector<schema::ComponentTypeId> cameraComponents_;
    /// Split-screen (D362): the layout's regions, the local players after
    /// the first (whose are `scene_` and `camera_`), each region's frame,
    /// and the frames queued for the others.
    struct LocalPlayer {
        std::size_t client = 0;
        std::unique_ptr<Scene> scene;
        SceneCamera camera;
        std::optional<execution::MonotonicInstant> presented;
        bool extracted = false;
    };
    std::vector<world_kest::GameRegion> regions_;
    /// The game's constrained aspect, each region's view centered between
    /// bars (D369); none fills.
    std::optional<world_kest::GameAspect> aspect_;
    std::vector<LocalPlayer> localPlayers_;
    std::vector<RegionFrame> regionFrames_;
    std::uint64_t playerFrames_ = 0;
    /// The views into render textures (D361): the game's view component,
    /// its query, each render texture's view and scene, the frames they
    /// queued, and the views drawn, kept (on demand, nothing asked), missed
    /// by the device, left out, and refused. A texture drawn on demand
    /// holds the view and request it last showed.
    struct Shown {
        world::EntityHandle entity;
        std::uint32_t request = 0;
    };
    struct TextureView {
        std::uint64_t id = 0;
        std::uint32_t width = 0;
        std::uint32_t height = 0;
        bool onDemand = false;
        std::optional<world::EntityHandle> entity;
        std::int32_t order = 0;
        std::uint32_t request = 0;
        bool tied = false;
        bool due = false;
        std::optional<Shown> shown;
        /// The frame last queued, kept by its scene until it queues
        /// another, and whether the device missed it.
        const SceneFrame* offered = nullptr;
        bool resend = false;
        std::unique_ptr<Scene> scene;
        SceneCamera camera;
        std::optional<execution::MonotonicInstant> presented;
    };
    std::optional<schema::ComponentTypeId> viewComponent_;
    std::optional<world::ColumnQuery> viewQuery_;
    std::vector<TextureView> textureViews_;
    std::vector<TextureFrame> textureFrames_;
    std::uint64_t viewsDrawn_ = 0;
    std::uint64_t viewsKept_ = 0;
    std::uint64_t viewsMissed_ = 0;
    std::uint64_t viewsLeftOut_ = 0;
    std::uint64_t viewsRefused_ = 0;
    std::optional<schema::ComponentTypeId> autoExposureComponent_;
    std::optional<schema::ComponentTypeId> gradingComponent_;
    std::optional<schema::ComponentTypeId> occlusionComponent_;
    std::optional<schema::ComponentTypeId> bloomComponent_;
    std::optional<schema::ComponentTypeId> reflectionsComponent_;
    std::optional<schema::ComponentTypeId> motionBlurComponent_;
    std::optional<schema::ComponentTypeId> depthOfFieldComponent_;
    std::optional<schema::ComponentTypeId> contactShadowsComponent_;
    std::vector<schema::ComponentTypeId> postProcessComponents_;
    std::optional<execution::MonotonicInstant> presented_;
    std::size_t gameMeshes_ = 0;
    /// Frames seen through the player's own camera.
    std::uint64_t viewed_ = 0;
    std::unique_ptr<Scene> scene_;
    bool extracted_ = false;
    const SceneFrame* queued_ = nullptr;
    std::uint32_t width_ = 1280;
    std::uint32_t height_ = 720;
    /// The local players' views, told each frame (D367).
    view::PlayerViews* views_ = nullptr;
    std::uint64_t frames_ = 0;
    std::uint64_t drawn_ = 0;
    std::uint64_t culled_ = 0;
    std::uint64_t hidden_ = 0;
    std::uint64_t malformed_ = 0;
    std::uint64_t unknownMeshes_ = 0;
    std::uint64_t unknownMaterials_ = 0;
    /// The particles' frames, summed (D360).
    rawframe::particles::Tally particles_;
    std::size_t gameMaterials_ = 0;
    /// Materials that could not be read, and why, said at start.
    std::vector<std::pair<std::string, std::string>> unreadMaterials_;
    /// The textures the materials sample (D309): held, the materials
    /// naming one the game does not declare, and why they could not be
    /// asked for.
    std::unique_ptr<game_textures::GameTextures> textures_;
    std::size_t sampled_ = 0;
    std::uint64_t tick_ = 0;
    std::vector<std::pair<std::string, std::string>> unknownTextures_;
    std::optional<std::string> unreadTextures_;
    /// What reading a texture needs, kept from load; the textures the game
    /// declares; and the sky's picture (D322), read on its own when the
    /// World names it: its identity, and the pictures asked for.
    struct Reading {
        content::ContentStore* store = nullptr;
        execution::Executor* cpu = nullptr;
        execution::OwnerId owner;
        execution::CancellationScope* scope = nullptr;
        const execution::MonotonicSource* clock = nullptr;
    };
    std::optional<Reading> reading_;
    std::vector<world_kest::GameTextureResource> declaredTextures_;
    /// The environments asked for, by texture; none for one that has none.
    std::map<std::uint64_t, std::unique_ptr<game_textures::GameTextures>> pictures_;
    std::uint64_t picturesRead_ = 0;
    std::uint64_t overLimit_ = 0;
    std::size_t mostDraws_ = 0;
    /// The point and spot lights each frame lit with, culled, and left out
    /// at the limit; a cluster's lights past its limit (D290).
    AntiAliasing antiAliasing_ = AntiAliasing::Taa;
    std::uint32_t multisamples_ = kDefaultMultisamples;
    material::Quality quality_ = material::Quality::High;
    LightShadowSettings lightShadows_;
    std::uint64_t lightsLit_ = 0;
    std::uint64_t lightsCulled_ = 0;
    std::uint64_t lightsOverLimit_ = 0;
    std::uint64_t clusterOverflow_ = 0;
    /// The punctual shadows' squares each frame drew, and the lights that
    /// asked for shadows past the budget (D292).
    std::uint64_t shadowSquares_ = 0;
    std::uint64_t shadowsEvicted_ = 0;
    /// The decals drawn, and those culled or past the limit (D339).
    std::uint64_t decalsDrawn_ = 0;
    std::uint64_t decalsCulled_ = 0;
    diagnostics::Emitter emitter_;
};

result::Result<composition::ParticipantOwner> make(composition::ParticipantContext& context) noexcept {
    auto participant = std::make_unique<SceneParticipant>();
    RAWFRAME_TRY(participant->load(context));
    return composition::ParticipantOwner{participant.release()};
}

} // namespace

void registerParticipants(composition::ParticipantRegistrar& registrar) noexcept {
    registrar.submit(composition::ParticipantDeclaration{
        .identity = "rawframe.render_scene.scene",
        .factory = &make,
        .scope = composition::LifetimeScope::World,
        .providedCapabilities = kProvided,
        .optionalCapabilities = kMaybe,
        // The materials' textures are decoded on the CPU executor (D309).
        .executor = {.cpu = true, .quota = {.maximumPendingTasks = 64}},
        .lifecycle = {.stopBudget = execution::MonotonicDuration::fromMilliseconds(10)},
        .observabilityIdentity = "render_scene.scene",
        .budgetOwner = "render",
        .hostPhases =
            static_cast<std::uint16_t>(composition::hostPhaseBit(composition::HostPhase::PresentationExtract) |
                                       composition::hostPhaseBit(composition::HostPhase::Present)),
    });
}

} // namespace rawframe::render_scene
