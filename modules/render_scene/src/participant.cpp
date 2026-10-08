#include "loading.h"
#include "pictures.h"
#include "rawframe/composition/composition.h"
#include "rawframe/game_content/game_content.h"
#include "rawframe/material/material.h"
#include "rawframe/physics3d/components.h"
#include "rawframe/render_scene/errors.h"
#include "rawframe/render_scene/frames.h"
#include "rawframe/render_scene/registrar.h"
#include "rawframe/render_scene/scene.h"
#include "rawframe/view/players.h"
#include "rawframe/view/preview.h"
#include "rawframe/world/column_query.h"
#include "rawframe/world_kest/game_files.h"
#include "rawframe/world_replication/client_worlds.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <memory>
#include <numbers>
#include <optional>
#include <string>

namespace rawframe::render_scene {

namespace {

constexpr diagnostics::EventIdentity kSceneSummary{"scene", "scene_summary"};
// The particles' apart: with them the summary would pass the 32 fields a
// record carries (D361).
constexpr diagnostics::EventIdentity kParticlesSummary{"scene", "scene_particles_summary"};
constexpr std::string_view kProvided[] = {kSceneFrames.name};
constexpr diagnostics::EventIdentity kMaterialUnread{"scene", "material_unread"};
constexpr diagnostics::EventIdentity kViewRefused{"scene", "view_refused"};
constexpr diagnostics::EventIdentity kViewsSummary{"scene", "scene_views_summary"};
constexpr std::string_view kMaybe[] = {world_replication::kClientWorlds.name,
                                       world_kest::kGameFiles.name,
                                       game_content::kGameContent.name,
                                       view::kPlayerViews.name,
                                       view::kPreviewCamera.name,
                                       world_animation::kPresentedPoses.name};

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
        RAWFRAME_TRY_ASSIGN(const SceneConfiguration kConfigured, readConfiguration(context.configuration()));
        const std::optional<std::size_t> client = kConfigured.client;
        width_ = kConfigured.width;
        height_ = kConfigured.height;
        shadows_ = kConfigured.shadows;
        lightShadows_ = kConfigured.lightShadows;
        antiAliasing_ = kConfigured.antiAliasing;
        multisamples_ = kConfigured.multisamples;
        quality_ = kConfigured.quality;
        renderScale_ = kConfigured.renderScale;
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
        // A preview's camera, where an authoring client sets one (D432).
        if (!client.has_value() && context.has(view::kPreviewCamera.name)) {
            RAWFRAME_TRY_ASSIGN(preview_, context.capability(view::kPreviewCamera));
        }
        // The poses its skinned models are drawn in (D508).
        if (context.has(world_animation::kPresentedPoses.name)) {
            RAWFRAME_TRY_ASSIGN(poses_, context.capability(world_animation::kPresentedPoses));
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
        // A constrained aspect (D369), or a render scale (D373): one
        // player's view is placed in the window as a split-screen player's
        // is in its region.
        aspect_ = files->description().aspect;
        if ((aspect_.has_value() || renderScale_ < 1) && regions_.empty()) {
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
        // processes (D349).
        std::vector<SceneMaterial> materials;
        std::vector<ScenePostProcessMaterial> postProcesses;
        RAWFRAME_TRY(readGameMaterials(context, *files, quality_, materials, postProcesses, unreadMaterials_));
        gameMaterials_ = materials.size();
        RAWFRAME_TRY(pictures_.read(context, *files, materials, postProcesses));
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
        pictures_.start(emitter_);
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
            pictures_.update(tick_);
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
            pictures_.askAll(kFrame);
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
            diagnostics::field("materialTextures", pictures_.sampled()),
            diagnostics::field("texturesReady", pictures_.texturesReady()),
            diagnostics::field("pictures", pictures_.picturesRead()),
            diagnostics::field("picturesReady", pictures_.picturesReady()),
            diagnostics::field("lightsLit", lightsLit_),
            diagnostics::field("lightsCulled", lightsCulled_),
            diagnostics::field("lightsOverLimit", lightsOverLimit_),
            diagnostics::field("clusterOverflow", clusterOverflow_),
            diagnostics::field("shadowSquares", shadowSquares_),
            diagnostics::field("shadowsEvicted", shadowsEvicted_),
            diagnostics::field("decalsDrawn", decalsDrawn_),
            diagnostics::field("decalsCulled", decalsCulled_),
            diagnostics::field("players", static_cast<std::uint64_t>(localPlayers_.size() + 1)),
            diagnostics::field("playerFrames", playerFrames_),
            diagnostics::field("framesPreviewed", previewed_),
            diagnostics::field("marksShown", marksDrawn_),
            diagnostics::field("marksLit", marksLit_)};
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

    float renderScale() const noexcept override {
        return renderScale_;
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
        return pictures_.texture(id, tick_);
    }

private:
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
            scene_->show(lines_);
        }
        scene_->extract(*kView.world, posesOf(*kView.world));
        extracted_ = true;
        if (kView.owned.isNull() || !kView.world->alive(kView.owned)) {
            return;
        }
        if (readCamera(*kView.world, kView.owned, camera_)) {
            ++viewed_;
        }
        // Seen from where the preview looks, exposed as the player's camera
        // says (D432).
        if (preview_ != nullptr && preview_->looking().has_value()) {
            const view::Perspective& kLook = *preview_->looking();
            camera_.eye = kLook.eye;
            camera_.yaw = kLook.yaw;
            camera_.pitch = kLook.pitch;
            camera_.fovY = kLook.fovY;
            ++previewed_;
        }
        // What the preview marks, as three axes a meter long, red along X,
        // green along Y, blue along Z, and a ring about it, drawn over
        // everything at a width the view keeps (D464, D467).
        if (preview_ != nullptr && preview_->marks() != marksShown_) {
            marksShown_ = preview_->marks();
            lines_.clear();
            if (const auto& kAt = preview_->marked(); kAt.has_value()) {
                ++marksDrawn_;
                // The part under the pointer drawn white and wider (D468).
                const view::MarkPart kLit = preview_->lit();
                marksLit_ += kLit != view::MarkPart::None ? 1 : 0;
                const auto kWidth = [](bool lit) {
                    return lit ? 1.0F / 90 : 1.0F / 150;
                };
                for (std::size_t axis = 0; axis < 3; ++axis) {
                    std::array<double, 3> to = *kAt;
                    to[axis] += 1;
                    const bool kAxisLit = static_cast<std::size_t>(kLit) == axis + 1;
                    std::array<float, 4> color{0, 0, 0, 1};
                    color[axis] = 1;
                    if (kAxisLit) {
                        color = {1, 1, 1, 1};
                    }
                    // A hundred and fiftieth of the view wide, over
                    // everything: the mark stands out as a gizmo does.
                    lines_.push_back(SceneLine{.from = *kAt,
                                               .to = to,
                                               .color = color,
                                               .width = kWidth(kAxisLit),
                                               .ofView = true,
                                               .over = true});
                }
                // And a yellow ring about it on the level plane, its turn's
                // handle (D467).
                for (std::size_t piece = 0; piece < view::kMarkRingPieces; ++piece) {
                    const auto kOn = [&](std::size_t at) {
                        const double kAngle =
                            2 * std::numbers::pi * static_cast<double>(at) / static_cast<double>(view::kMarkRingPieces);
                        return std::array<double, 3>{(*kAt)[0] + (view::kMarkRingRadius * std::cos(kAngle)),
                                                     (*kAt)[1],
                                                     (*kAt)[2] + (view::kMarkRingRadius * std::sin(kAngle))};
                    };
                    lines_.push_back(SceneLine{.from = kOn(piece),
                                               .to = kOn(piece + 1),
                                               .color = kLit == view::MarkPart::Ring
                                                            ? std::array<float, 4>{1, 1, 1, 1}
                                                            : std::array<float, 4>{1, 0.8F, 0, 1},
                                               .width = kWidth(kLit == view::MarkPart::Ring),
                                               .ofView = true,
                                               .over = true});
                }
            }
            scene_->show(lines_);
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
            each.scene->extract(*kView.world, posesOf(*kView.world));
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
            pictures_.askAll(kFrame);
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
            each.scene->extract(world, posesOf(world));
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
            pictures_.ask(kFrame.lights.environment);
            pictures_.ask(kFrame.grading.table);
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
    /// Frames seen through a preview's camera (D432).
    std::uint64_t previewed_ = 0;
    std::unique_ptr<Scene> scene_;
    /// The lines the player's view shows (D464).
    std::vector<SceneLine> lines_;
    bool extracted_ = false;
    const SceneFrame* queued_ = nullptr;
    std::uint32_t width_ = 1280;
    std::uint32_t height_ = 720;
    /// The local players' views, told each frame (D367).
    view::PlayerViews* views_ = nullptr;
    view::PreviewCamera* preview_ = nullptr;
    const world_animation::PresentedPoses* poses_ = nullptr;

    /// The animation `world` was presented with, if any.
    const world_animation::AnimationQueries* posesOf(const world::World& world) const noexcept {
        return poses_ != nullptr ? poses_->posesOf(world) : nullptr;
    }
    std::uint64_t marksShown_ = 0;
    /// The preview's marks drawn, a point each (D464).
    std::uint64_t marksDrawn_ = 0;
    /// Those drawn with a part lit (D468).
    std::uint64_t marksLit_ = 0;
    /// Each local player's view drawn at this share of its region's pixels
    /// each way (D373).
    float renderScale_ = 1;
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
    /// The textures the materials sample and the pictures the World names.
    ScenePictures pictures_;
    std::uint64_t tick_ = 0;
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
