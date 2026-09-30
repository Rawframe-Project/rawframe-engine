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
#include "rawframe/world_kest/game_files.h"
#include "rawframe/world_replication/client_worlds.h"

#include <algorithm>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>

namespace rawframe::render_scene {

namespace {

constexpr diagnostics::EventIdentity kSceneSummary{"scene", "scene_summary"};
constexpr std::string_view kProvided[] = {kSceneFrames.name};
constexpr diagnostics::EventIdentity kMaterialUnread{"scene", "material_unread"};
constexpr diagnostics::EventIdentity kTextureUnknown{"scene", "material_texture_unknown"};
constexpr diagnostics::EventIdentity kTexturesUnavailable{"scene", "textures_unavailable"};
constexpr diagnostics::EventIdentity kTextureUnread{"scene", "texture_unavailable"};
constexpr diagnostics::EventIdentity kTextureReloaded{"scene", "texture_reloaded"};
constexpr diagnostics::EventIdentity kTexturesRead{"scene", "textures_read"};
constexpr diagnostics::EventIdentity kEnvironmentUnknown{"scene", "environment_unknown"};
constexpr diagnostics::EventIdentity kEnvironmentUnread{"scene", "environment_unavailable"};
/// The decoded levels the materials' textures may hold.
constexpr std::uint64_t kTextureBudgetBytes = std::uint64_t{256} * 1024 * 1024;
constexpr std::string_view kMaybe[] = {
    world_replication::kClientWorlds.name, world_kest::kGameFiles.name, game_content::kGameContent.name};

/// A material's texture as the scene binds it.
SceneTexture sceneTextureOf(const material::SampledTexture& texture) {
    return {.id = texture.id, .filter = texture.filter, .address = texture.address};
}

/// A material's cooked bytes, read and waited for, decoded (D303).
result::Result<material::Material>
readMaterial(content::ContentStore& store, base::Bits128 id, material::Quality quality) {
    RAWFRAME_TRY_ASSIGN(execution::AsyncHandle<content::VerifiedContent> read,
                        store.read(content::ResourceRef{.id = content::ResourceId{id},
                                                        .type = content::ResourceTypeId{material::kMaterialType}}));
    RAWFRAME_TRY_ASSIGN(
        const content::VerifiedContent kRead,
        execution::toResult(read.wait(),
                            execution::CancellationMapping{.errorClass = result::ErrorClass::Unavailable,
                                                           .domain = kRenderSceneDomain,
                                                           .code = code(RenderSceneError::MaterialUnreadable),
                                                           .description = "a material's read was cancelled"}));
    RAWFRAME_TRY_ASSIGN(const material::Qualities kQualities, material::decode(kRead.bytes()));
    return kQualities.at(static_cast<std::size_t>(quality));
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
        if (kFilter != "hardware" && kFilter != "soft") {
            return std::unexpected<result::Error>{result::fail(result::ErrorClass::InvalidArgument,
                                                               composition::kCompositionDomain,
                                                               code(composition::CompositionError::BadConfiguration),
                                                               "scene.shadow_filter is hardware or soft")
                                                      .error()};
        }
        shadows_ = ShadowSettings{.cascades = static_cast<std::uint32_t>(kCascades),
                                  .distance = static_cast<float>(kDistance),
                                  .logarithmicBlend = kShadowDefaults.logarithmicBlend,
                                  .side = static_cast<std::uint32_t>(kSide),
                                  .filter = kFilter == "soft" ? ShadowFilter::Soft : ShadowFilter::Hardware};
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
        // ADR-0051's anti-aliasing method (D291, D296): temporal unless
        // another is named; multisampling is not built yet.
        const std::string_view kMethod = configuration.text("scene.anti_aliasing").value_or("taa");
        if (kMethod != "taa" && kMethod != "fxaa" && kMethod != "off") {
            return std::unexpected<result::Error>{
                result::fail(result::ErrorClass::InvalidArgument,
                             composition::kCompositionDomain,
                             code(composition::CompositionError::BadConfiguration),
                             "scene.anti_aliasing is taa, fxaa, or off; msaa is not built yet")
                    .error()};
        }
        antiAliasing_ = kMethod == "off"    ? AntiAliasing::Off
                        : kMethod == "fxaa" ? AntiAliasing::Fxaa
                                            : AntiAliasing::Taa;
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
        cameraComponent_ = game->camera;
        autoExposureComponent_ = game->autoExposure;
        gradingComponent_ = game->grading;
        occlusionComponent_ = game->occlusion;
        bloomComponent_ = game->bloom;
        gameMeshes_ = game->meshes.size();
        // The game's materials from its cooked content; one that cannot be
        // read is drawn as none, and said so when the scene starts.
        std::vector<SceneMaterial> materials;
        if (!files->materials().empty() && context.has(game_content::kGameContent.name)) {
            RAWFRAME_TRY_ASSIGN(game_content::GameContent * content, context.capability(game_content::kGameContent));
            if (content->held()) {
                const std::array<content::AdmittedRepresentation, 1> kAdmitted = {content::AdmittedRepresentation{
                    .type = content::ResourceTypeId{material::kMaterialType},
                    .representation = *content::RepresentationId::parse(material::kMaterialRepresentation)}};
                RAWFRAME_TRY(content->admit(kAdmitted));
                for (const world_kest::GameMaterialResource& each : files->materials()) {
                    auto read = readMaterial(content->store(), each.material, quality_);
                    if (read.has_value()) {
                        materials.push_back({.id = each.id,
                                             .blob = material::blobOf(*read),
                                             .translucent = read->blend == material::Blend::Translucent,
                                             .textures = {.base = sceneTextureOf(read->textures.base),
                                                          .packed = sceneTextureOf(read->textures.packed),
                                                          .emission = sceneTextureOf(read->textures.emission),
                                                          .normal = sceneTextureOf(read->textures.normal)}});
                    } else if (!each.subasset || read.error().domain() != content::kContentDomain ||
                               read.error().code() != content::code(content::ContentError::ResourceNotFound)) {
                        // A mesh's subasset its source no longer has names
                        // no resource and is no material (D314).
                        unreadMaterials_.emplace_back(each.path, std::string{read.error().description()});
                    }
                }
            }
        }
        gameMaterials_ = materials.size();
        RAWFRAME_TRY(readTextures(context, *files, materials));
        settings_ = SceneSettings{.models = std::move(game->models),
                                  .sun = game->sun,
                                  .sky = game->sky,
                                  .points = std::move(game->points),
                                  .spots = std::move(game->spots),
                                  .probes = std::move(game->probes),
                                  .meshes = std::move(game->meshes),
                                  .materials = std::move(materials),
                                  .shadows = shadows_,
                                  .lightShadows = lightShadows_,
                                  .antiAliasing = antiAliasing_};
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
            ++tick_;
            updateTextures();
            extract();
        } else if (phase == composition::HostPhase::Present && extracted_) {
            extracted_ = false;
            camera_.aspect = static_cast<float>(width_) / static_cast<float>(height_);
            // The seconds since the frame before, on the Host's timeline
            // (D293): nought for the first.
            camera_.elapsed =
                presented_.has_value() ? static_cast<float>((frame.now - *presented_).nanoseconds) / 1e9F : 0.0F;
            presented_ = frame.now;
            const SceneFrame& kFrame = scene_->queue(camera_);
            askEnvironment(kFrame.lights.environment);
            for (const render_scene::SceneProbe& kProbe : kFrame.probes) {
                askEnvironment(kProbe.environment);
            }
            queued_ = &kFrame;
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
        }
    }

    void stop() noexcept override {
        if (clients_ == nullptr) {
            return;
        }
        emitter_.log(
            diagnostics::Severity::Info,
            kSceneSummary,
            "what one client's scene drew",
            {diagnostics::field("frames", frames_),
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
             diagnostics::field("environments", environmentsRead_),
             diagnostics::field("environmentsReady", environmentsReady()),
             diagnostics::field("lightsLit", lightsLit_),
             diagnostics::field("lightsCulled", lightsCulled_),
             diagnostics::field("lightsOverLimit", lightsOverLimit_),
             diagnostics::field("clusterOverflow", clusterOverflow_),
             diagnostics::field("shadowSquares", shadowSquares_),
             diagnostics::field("shadowsEvicted", shadowsEvicted_)});
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
        if (const auto kEnvironment = environments_.find(id); kEnvironment != environments_.end()) {
            return kEnvironment->second != nullptr ? kEnvironment->second->texture(id, tick_) : nullptr;
        }
        return textures_ != nullptr ? textures_->texture(id, tick_) : nullptr;
    }

private:
    /// Asks for the textures `materials` sample, of those the game
    /// declares, decoded on a CPU worker (D309); a material sampling one it
    /// does not declare samples white, and is said so at start.
    result::Status readTextures(composition::ParticipantContext& context,
                                const world_kest::GameFiles& files,
                                std::vector<SceneMaterial>& materials) {
        std::set<std::uint64_t> sampled;
        for (SceneMaterial& each : materials) {
            for (SceneTexture* texture :
                 {&each.textures.base, &each.textures.packed, &each.textures.emission, &each.textures.normal}) {
                if (texture->id == 0) {
                    continue;
                }
                if (std::ranges::find(files.textures(), texture->id, &world_kest::GameTextureResource::id) ==
                    files.textures().end()) {
                    const auto kPath =
                        std::ranges::find(files.materials(), each.id, &world_kest::GameMaterialResource::id);
                    unknownTextures_.emplace_back(kPath != files.materials().end() ? kPath->path : std::string{},
                                                  graph::nodeIdText(texture->id));
                    texture->id = 0;
                    continue;
                }
                sampled.insert(texture->id);
            }
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

    /// Asks for an environment the frame names, the sky's picture or a
    /// reflection probe's, on its first naming (D322, D325): a texture the
    /// game declares, read alone and held, so a game's other textures are
    /// never read for it. One it does not declare, or that cannot be asked
    /// for, is none, and is said so once.
    void askEnvironment(std::uint64_t id) noexcept {
        if (id == 0 || environments_.contains(id)) {
            return;
        }
        std::unique_ptr<game_textures::GameTextures>& reader = environments_[id];
        const auto kDeclared = std::ranges::find(declaredTextures_, id, &world_kest::GameTextureResource::id);
        if (kDeclared == declaredTextures_.end()) {
            emitter_.log(diagnostics::Severity::Warning,
                         kEnvironmentUnknown,
                         "an environment names a picture the game does not declare: it has none",
                         {diagnostics::field("texture", graph::nodeIdText(id))});
            return;
        }
        if (!reading_.has_value()) {
            return;
        }
        ++environmentsRead_;
        auto made = game_textures::GameTextures::create(*reading_->store,
                                                        *reading_->cpu,
                                                        reading_->owner,
                                                        *reading_->scope,
                                                        *reading_->clock,
                                                        {*kDeclared},
                                                        kTextureBudgetBytes);
        if (!made.has_value()) {
            emitter_.log(diagnostics::Severity::Warning,
                         kEnvironmentUnread,
                         "an environment's picture could not be asked for: it has none",
                         {diagnostics::field("texture", graph::nodeIdText(id)),
                          diagnostics::field("reason", std::string{made.error().description()})});
            return;
        }
        reader = std::move(*made);
    }

    /// The environments read and ready.
    [[nodiscard]] std::uint64_t environmentsReady() const noexcept {
        std::uint64_t ready = 0;
        for (const auto& [kId, kReader] : environments_) {
            ready += kReader != nullptr ? kReader->counts().ready : 0;
        }
        return ready;
    }

    /// Takes finished reads and reloads; a texture that fails is sampled
    /// as white, and an environment that fails is none.
    void updateTextures() noexcept {
        for (const auto& [kEnvironment, kReader] : environments_) {
            if (kReader == nullptr) {
                continue;
            }
            for (const auto& [kId, kError] : kReader->update(tick_).failed) {
                emitter_.log(diagnostics::Severity::Warning,
                             kEnvironmentUnread,
                             "an environment's picture could not be read: it has none",
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
        Camera view = kDefaultCamera;
        if (cameraComponent_.has_value()) {
            if (const auto kCamera = kView.world->registry().find(*cameraComponent_)) {
                if (const auto* placed = static_cast<const Camera*>(kView.world->getErased(kView.owned, *kCamera))) {
                    view = *placed;
                    ++viewed_;
                }
            }
        }
        camera_.yaw = view.yaw;
        camera_.pitch = view.pitch;
        camera_.fovY = view.fovY;
        camera_.near = view.near;
        camera_.exposure = view.exposure;
        camera_.tonemapper = view.tonemapper;
        camera_.metering.reset();
        camera_.grading.reset();
        if (gradingComponent_.has_value()) {
            if (const auto kGrading = kView.world->registry().find(*gradingComponent_)) {
                if (const auto* asked = static_cast<const Grading*>(kView.world->getErased(kView.owned, *kGrading))) {
                    camera_.grading = *asked;
                }
            }
        }
        camera_.bloom.reset();
        if (bloomComponent_.has_value()) {
            if (const auto kBloom = kView.world->registry().find(*bloomComponent_)) {
                if (const auto* asked = static_cast<const Bloom*>(kView.world->getErased(kView.owned, *kBloom))) {
                    camera_.bloom = *asked;
                }
            }
        }
        camera_.occlusion.reset();
        if (occlusionComponent_.has_value()) {
            if (const auto kOcclusion = kView.world->registry().find(*occlusionComponent_)) {
                if (const auto* asked =
                        static_cast<const AmbientOcclusion*>(kView.world->getErased(kView.owned, *kOcclusion))) {
                    camera_.occlusion = *asked;
                }
            }
        }
        if (autoExposureComponent_.has_value()) {
            if (const auto kMetering = kView.world->registry().find(*autoExposureComponent_)) {
                if (const auto* asked =
                        static_cast<const AutoExposure*>(kView.world->getErased(kView.owned, *kMetering))) {
                    camera_.metering = *asked;
                }
            }
        }
        if (const auto kPose = kView.world->registry().key<physics3d::Pose3D>()) {
            if (const auto* pose = kView.world->get(kView.owned, *kPose)) {
                camera_.eye = {pose->x + view.offsetX, pose->y + view.offsetY, pose->z + view.offsetZ};
            }
        }
    }

    world_replication::ClientWorlds* clients_ = nullptr;
    std::optional<std::size_t> client_;
    SceneSettings settings_;
    ShadowSettings shadows_;
    SceneCamera camera_;
    std::optional<schema::ComponentTypeId> cameraComponent_;
    std::optional<schema::ComponentTypeId> autoExposureComponent_;
    std::optional<schema::ComponentTypeId> gradingComponent_;
    std::optional<schema::ComponentTypeId> occlusionComponent_;
    std::optional<schema::ComponentTypeId> bloomComponent_;
    std::optional<execution::MonotonicInstant> presented_;
    std::size_t gameMeshes_ = 0;
    /// Frames seen through the player's own camera.
    std::uint64_t viewed_ = 0;
    std::unique_ptr<Scene> scene_;
    bool extracted_ = false;
    const SceneFrame* queued_ = nullptr;
    std::uint32_t width_ = 1280;
    std::uint32_t height_ = 720;
    std::uint64_t frames_ = 0;
    std::uint64_t drawn_ = 0;
    std::uint64_t culled_ = 0;
    std::uint64_t hidden_ = 0;
    std::uint64_t malformed_ = 0;
    std::uint64_t unknownMeshes_ = 0;
    std::uint64_t unknownMaterials_ = 0;
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
    std::map<std::uint64_t, std::unique_ptr<game_textures::GameTextures>> environments_;
    std::uint64_t environmentsRead_ = 0;
    std::uint64_t overLimit_ = 0;
    std::size_t mostDraws_ = 0;
    /// The point and spot lights each frame lit with, culled, and left out
    /// at the limit; a cluster's lights past its limit (D290).
    AntiAliasing antiAliasing_ = AntiAliasing::Taa;
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
