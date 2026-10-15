#include "camera.h"
#include "cameras.h"
#include "loading.h"
#include "pictures.h"
#include "rawframe/composition/composition.h"
#include "rawframe/game_content/game_content.h"
#include "rawframe/material/material.h"
#include "rawframe/physics3d/components.h"
#include "rawframe/render_scene/errors.h"
#include "rawframe/render_scene/frames.h"
#include "rawframe/render_scene/registrar.h"
#include "rawframe/render_scene/render_scale.h"
#include "rawframe/render_scene/scene.h"
#include "rawframe/view/headset.h"
#include "rawframe/view/players.h"
#include "rawframe/view/preview.h"
#include "rawframe/world/column_query.h"
#include "rawframe/world_kest/game_files.h"
#include "rawframe/world_replication/client_worlds.h"
#include "views.h"

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
constexpr diagnostics::EventIdentity kMaterialsReloaded{"scene", "materials_reloaded"};
constexpr diagnostics::EventIdentity kRenderScaleChanged{"scene", "render_scale_changed"};
constexpr diagnostics::EventIdentity kEyesSummary{"scene", "scene_eyes_summary"};
constexpr diagnostics::EventIdentity kPlacedSummary{"scene", "scene_placed_summary"};
constexpr std::string_view kMaybe[] = {world_replication::kClientWorlds.name,
                                       world_kest::kGameFiles.name,
                                       game_content::kGameContent.name,
                                       view::kPlayerViews.name,
                                       view::kPreviewCamera.name,
                                       world_animation::kPresentedPoses.name,
                                       view::kHeadsetEyes.name,
                                       view::kHeadsetHands.name};

/// The most looks a split preview draws beside its own (D797). A device
/// drawing four region views at once stalled on lavapipe (its cause not
/// yet found; three draw), so a preview splits at most three ways.
constexpr std::size_t kMostOthers = 2;

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
        renderScale_ = RenderScale{kConfigured.renderScalePercent, kConfigured.leastRenderScalePercent};
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
        // A headset's eyes, the player's view drawn for each (D595).
        if (!client.has_value() && context.has(view::kHeadsetEyes.name)) {
            RAWFRAME_TRY_ASSIGN(eyes_, context.capability(view::kHeadsetEyes));
        }
        // And its hands, which what the player holds follows (D676).
        if (!client.has_value() && context.has(view::kHeadsetHands.name)) {
            RAWFRAME_TRY_ASSIGN(held_, context.capability(view::kHeadsetHands));
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
        // A constrained aspect (D369), or a render scale (D373), one that
        // may change too (D533): one player's view is placed in the window
        // as a split-screen player's is in its region.
        aspect_ = files->description().aspect;
        if ((aspect_.has_value() || renderScale_.scale() < 1 || renderScale_.follows()) && regions_.empty()) {
            regions_.push_back(world_kest::GameRegion{});
            regionFrames_.resize(1);
        }
        // A preview may be split as many ways as its window allows (D797),
        // which an editor playing it asks for: its regions are placed from
        // the start, shown only while it is, as a device joins its regions'
        // views once. A client that holds idle region views draws its window
        // unlit, so no other client is given them.
        previewViews_ = preview_ != nullptr ? std::clamp<std::size_t>(preview_->views(), 1, kMostOthers + 1) : 1;
        if (previewViews_ > 1 && regionFrames_.size() < previewViews_) {
            regionFrames_.resize(previewViews_);
        }
        cameras_ = GameCameras(*game);
        textureViews_ = TextureViews(game->view, files->description().renderTextures);
        gameMeshes_ = game->meshes.size();
        // The game's materials from its cooked content, surfaces and post
        // processes (D349).
        std::vector<SceneMaterial> materials;
        std::vector<ScenePostProcessMaterial> postProcesses;
        RAWFRAME_TRY(
            readGameMaterials(context, *files, quality_, programs_, materials, postProcesses, unreadMaterials_));
        gameMaterials_ = materials.size();
        // Followed as its content is (D734): the catalog read in now and
        // what it says each material's revision is.
        if (!files->materials().empty() && context.has(game_content::kGameContent.name)) {
            RAWFRAME_TRY_ASSIGN(game_content::GameContent * content, context.capability(game_content::kGameContent));
            if (content->held()) {
                content_ = content;
                files_ = files;
                catalog_ = content->store().catalog();
                revisions_ = materialRevisions(content->store(), *files);
            }
        }
        RAWFRAME_TRY(pictures_.read(context, *files, materials, postProcesses));
        settings_ = SceneSettings{.models = std::move(game->models),
                                  .placements = std::move(game->placements),
                                  .tracking = std::move(game->tracking),
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
            textureViews_.forget();
            for (RegionFrame& each : regionFrames_) {
                each.frame = nullptr;
            }
            eyeFrames_.clear();
            ++tick_;
            pictures_.update(tick_);
            followMaterials();
            extract();
        } else if (phase == composition::HostPhase::Present && extracted_) {
            extracted_ = false;
            camera_.aspect = static_cast<float>(width_) / static_cast<float>(height_);
            if (splits_ > 0) {
                presentSplits(frame.now);
            } else {
                // A split ended: its regions show nothing, as a game's own
                // regions are placed again.
                for (std::size_t at = regions_.size(); at < regionFrames_.size(); ++at) {
                    regionFrames_[at] = RegionFrame{};
                }
                if (regions_.empty() && !regionFrames_.empty()) {
                    regionFrames_[0] = RegionFrame{};
                }
                if (!regions_.empty()) {
                    presentPlayers(frame.now);
                }
            }
            // The seconds since the frame before, on the Host's timeline
            // (D293): nought for the first.
            camera_.elapsed =
                presented_.has_value() ? static_cast<float>((frame.now - *presented_).nanoseconds) / 1e9F : 0.0F;
            presented_ = frame.now;
            textureViews_.present(frame.now, pictures_);
            const SceneFrame& kFrame = scene_->queue(camera_);
            pictures_.askAll(kFrame);
            queued_ = &kFrame;
            if (!regionFrames_.empty() && regionFrames_[0].width != 0) {
                regionFrames_[0].frame = &kFrame;
            }
            presentEyes();
            tellViews();
            ++frames_;
            drawn_ += kFrame.drawn;
            placed_ += kFrame.placed;
            tracked_ += kFrame.tracked;
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
            diagnostics::field("marksLit", marksLit_),
            // Where the player's view last looked, about +Y (D521).
            diagnostics::field("yaw", camera_.yaw)};
        emitter_.log(diagnostics::Severity::Info, kSceneSummary, "what one client's scene drew", fields);
        if (eyeFramesQueued_ != 0) {
            emitter_.log(diagnostics::Severity::Info,
                         kEyesSummary,
                         "what one client's scene queued for a headset's eyes",
                         {diagnostics::field("eyes", static_cast<std::uint64_t>(eyeScenes_.size())),
                          diagnostics::field("eyeFrames", eyeFramesQueued_),
                          diagnostics::field("width", eyeWidth_),
                          diagnostics::field("height", eyeHeight_)});
        }
        // Apart from the scene's summary, which holds as many fields as a
        // record may (D361).
        const std::uint64_t kHidden = scene_ != nullptr ? scene_->modelsHidden() : 0;
        if (placed_ != 0 || tracked_ != 0 || kHidden != 0 || splitFrames_ != 0) {
            emitter_.log(diagnostics::Severity::Info,
                         kPlacedSummary,
                         "what one client's scene drew where it was placed, where a headset's hand was, and what "
                         "an editor's preview left out or split its window to show",
                         {diagnostics::field("modelsPlaced", placed_),
                          diagnostics::field("modelsTracked", tracked_),
                          diagnostics::field("modelsHidden", kHidden),
                          diagnostics::field("splitFrames", splitFrames_)});
        }
        textureViews_.summarize(emitter_);
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
        return textureViews_.frames();
    }

    std::span<const RegionFrame> regionFrames() const noexcept override {
        return regionFrames_;
    }

    std::span<const EyeFrame> eyeFrames() const noexcept override {
        return eyeFrames_;
    }

    void missed(std::uint64_t id) noexcept override {
        textureViews_.missed(id);
    }

    float renderScale() const noexcept override {
        return renderScale_.scale();
    }

    void paced(execution::MonotonicDuration atLeast,
               execution::MonotonicDuration atMost,
               execution::MonotonicDuration budget) noexcept override {
        const std::uint32_t kWas = renderScale_.percent();
        if (renderScale_.paced(atLeast, atMost, budget)) {
            emitter_.log(diagnostics::Severity::Info,
                         kRenderScaleChanged,
                         "a local player's view is drawn at another render scale",
                         {diagnostics::field("from", static_cast<std::uint64_t>(kWas)),
                          diagnostics::field("to", static_cast<std::uint64_t>(renderScale_.percent())),
                          diagnostics::field("tookAtLeastMicroseconds", atLeast.nanoseconds / 1000),
                          diagnostics::field("budgetMicroseconds", budget.nanoseconds / 1000)});
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
        // The camera first: a headset's hands are placed in the World by
        // it before the models that follow them are extracted (D676).
        const bool kOwned = !kView.owned.isNull() && kView.world->alive(kView.owned);
        if (kOwned && cameras_.read(*kView.world, kView.owned, camera_)) {
            ++viewed_;
        }
        // Seen from where the preview looks, exposed as the player's camera
        // says (D432).
        // Lit, but where the preview draws otherwise while it looks (D794).
        camera_.mode = view::ViewMode::Lit;
        if (kOwned && preview_ != nullptr && preview_->looking().has_value()) {
            const view::Perspective& kLook = *preview_->looking();
            camera_.eye = kLook.eye;
            camera_.yaw = kLook.yaw;
            camera_.pitch = kLook.pitch;
            camera_.fovY = kLook.fovY;
            camera_.mode = preview_->mode();
            ++previewed_;
        }
        HandsInWorld hands;
        if (kOwned && held_ != nullptr) {
            for (std::size_t side = 0; side < hands.size(); ++side) {
                if (const auto& kHand = held_->hands()[side]; kHand.has_value()) {
                    hands[side] = inPlaySpace(camera_, kHand->position);
                }
            }
        }
        shown_ = kView.seconds.value_or(0);
        scene_->shownAt(shown_);
        // Without the layers an editor's preview hides (D791).
        scene_->hideLayers(preview_ != nullptr ? preview_->hidden() : std::vector<std::uint32_t>{});
        scene_->extract(*kView.world, posesOf(*kView.world), &hands);
        extracted_ = true;
        extractSplits(*kView.world, kOwned);
        extractEyes(*kView.world, hands);
        if (!kOwned) {
            return;
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
        textureViews_.extract(*kView.world, settings_, shown_, posesOf(*kView.world), cameras_, emitter_);
        extractPlayers();
    }

    /// The first player's World extracted again into each headset eye's
    /// scene (D595), each eye keeping its own frames' history.
    void extractEyes(world::World& world, const HandsInWorld& hands) {
        eyesSeen_ = eyes_ != nullptr ? eyes_->eyes().size() : 0;
        for (std::size_t at = 0; at < eyesSeen_; ++at) {
            if (at == eyeScenes_.size()) {
                auto made = Scene::create(world.registry(), settings_);
                if (!made.has_value()) {
                    eyesSeen_ = at;
                    return;
                }
                eyeScenes_.push_back(std::move(*made));
                eyeScenes_.back()->show(lines_);
            }
            eyeScenes_[at]->shownAt(shown_);
            eyeScenes_[at]->extract(world, posesOf(world), &hands);
        }
    }

    /// Each eye's frame queued (D595), as `eyeViewOf` sees it from the
    /// first player's camera.
    void presentEyes() {
        if (eyes_ == nullptr || eyes_->eyes().size() != eyesSeen_) {
            return;
        }
        for (const view::HeadsetEye& kEye : eyes_->eyes()) {
            const std::optional<EyeView> kView = eyeViewOf(camera_, kEye);
            if (!kView.has_value()) {
                eyeFrames_.clear();
                return;
            }
            const SceneFrame& kFrame = eyeScenes_[eyeFrames_.size()]->queue(kView->camera);
            pictures_.askAll(kFrame);
            eyeFrames_.push_back(EyeFrame{
                .width = kView->width, .height = kView->height, .viewport = kView->viewport, .frame = &kFrame});
            ++eyeFramesQueued_;
            eyeWidth_ = kView->width;
            eyeHeight_ = kView->height;
        }
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
            each.scene->shownAt(kView.seconds.value_or(0));
            each.scene->extract(*kView.world, posesOf(*kView.world));
            if (!kView.owned.isNull() && kView.world->alive(kView.owned)) {
                static_cast<void>(cameras_.read(*kView.world, kView.owned, each.camera));
            }
            each.extracted = true;
        }
    }

    /// A split preview's other looks (D797), each extracted into a scene
    /// of its own and seen as the preview's look is but from where it
    /// looks; none while the preview is not split.
    void extractSplits(world::World& world, bool owned) {
        const bool kSplit = owned && preview_ != nullptr && preview_->looking().has_value();
        splits_ = kSplit ? std::min(preview_->others().size(), previewViews_ - 1) : 0;
        for (std::size_t at = 0; at < splits_; ++at) {
            if (at == splitScenes_.size()) {
                auto made = Scene::create(world.registry(), settings_);
                if (!made.has_value()) {
                    splits_ = at;
                    return;
                }
                splitScenes_.push_back(std::move(*made));
                splitCameras_.emplace_back();
            }
            splitScenes_[at]->shownAt(shown_);
            splitScenes_[at]->hideLayers(preview_->hidden());
            splitScenes_[at]->extract(world, posesOf(world));
            const view::Perspective& kOther = preview_->others()[at];
            SceneCamera& camera = splitCameras_[at];
            camera = camera_;
            camera.eye = kOther.eye;
            camera.yaw = kOther.yaw;
            camera.pitch = kOther.pitch;
            camera.fovY = kOther.fovY;
        }
    }

    /// The preview's look in the first of the split's regions and the
    /// others' frames in theirs (D797).
    void presentSplits(execution::MonotonicInstant now) {
        const std::size_t kCount = splits_ + 1;
        if (regionFrames_.size() < kCount) {
            regionFrames_.resize(kCount);
        }
        for (std::size_t at = 0; at < regionFrames_.size(); ++at) {
            if (at >= kCount) {
                regionFrames_[at] = RegionFrame{};
                continue;
            }
            const view::Region kRegion = view::previewRegion(kCount, at);
            regionFrames_[at] = RegionFrame{
                .x = static_cast<std::uint32_t>(std::lround(kRegion.left * static_cast<float>(width_))),
                .y = static_cast<std::uint32_t>(std::lround(kRegion.top * static_cast<float>(height_))),
                .width =
                    std::max(1U, static_cast<std::uint32_t>(std::lround(kRegion.width * static_cast<float>(width_)))),
                .height = std::max(
                    1U, static_cast<std::uint32_t>(std::lround(kRegion.height * static_cast<float>(height_))))};
        }
        camera_.aspect = static_cast<float>(regionFrames_[0].width) / static_cast<float>(regionFrames_[0].height);
        for (std::size_t at = 0; at < splits_; ++at) {
            RegionFrame& region = regionFrames_[at + 1];
            SceneCamera& camera = splitCameras_[at];
            camera.aspect = static_cast<float>(region.width) / static_cast<float>(region.height);
            camera.elapsed = presented_.has_value() ? static_cast<float>((now - *presented_).nanoseconds) / 1e9F : 0.0F;
            const SceneFrame& kFrame = splitScenes_[at]->queue(camera);
            pictures_.askAll(kFrame);
            region.frame = &kFrame;
            ++splitFrames_;
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

    /// A catalog the store published since the last (D734): the game's
    /// materials read again where any one's revision changed, and every
    /// scene given them, as the frames from the next draw them. A texture
    /// a material newly names is not read until the game is played again.
    void followMaterials() noexcept {
        if (content_ == nullptr || content_->store().catalog() == catalog_) {
            return;
        }
        catalog_ = content_->store().catalog();
        std::vector<content::ContentDigest> revisions = materialRevisions(content_->store(), *files_);
        if (revisions == revisions_) {
            return;
        }
        revisions_ = std::move(revisions);
        std::vector<SceneMaterial> materials;
        std::vector<ScenePostProcessMaterial> postProcesses;
        std::vector<std::pair<std::string, std::string>> unread;
        readMaterials(content_->store(), *files_, quality_, programs_, materials, postProcesses, unread);
        for (const auto& [kPath, kReason] : unread) {
            emitter_.log(diagnostics::Severity::Warning,
                         kMaterialUnread,
                         "a material could not be read: its models are drawn with none",
                         {diagnostics::field("material", kPath), diagnostics::field("reason", kReason)});
        }
        const auto kGiven = [&materials, &postProcesses](const std::unique_ptr<Scene>& scene) {
            if (scene != nullptr) {
                scene->replaceMaterials(materials, postProcesses);
            }
        };
        kGiven(scene_);
        for (LocalPlayer& each : localPlayers_) {
            kGiven(each.scene);
        }
        for (const std::unique_ptr<Scene>& each : eyeScenes_) {
            kGiven(each);
        }
        textureViews_.replaceMaterials(materials, postProcesses);
        pictures_.follow(*files_, materials, postProcesses);
        ++materialReloads_;
        emitter_.log(diagnostics::Severity::Info,
                     kMaterialsReloaded,
                     "the game's materials were read again from their new revisions",
                     {diagnostics::field("materials", static_cast<std::uint64_t>(materials.size())),
                      diagnostics::field("reloads", materialReloads_)});
        gameMaterials_ = materials.size();
        settings_.materials = std::move(materials);
        settings_.postProcesses = std::move(postProcesses);
    }

    world_replication::ClientWorlds* clients_ = nullptr;
    std::optional<std::size_t> client_;
    SceneSettings settings_;
    ShadowSettings shadows_;
    SceneCamera camera_;
    GameCameras cameras_;
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
    /// A split preview's other looks (D797): how many this frame, each's
    /// scene and camera, and the frames they drew.
    std::size_t previewViews_ = 1;
    std::size_t splits_ = 0;
    std::vector<std::unique_ptr<Scene>> splitScenes_;
    std::vector<SceneCamera> splitCameras_;
    std::uint64_t splitFrames_ = 0;
    std::vector<RegionFrame> regionFrames_;
    /// A headset's eyes (D595): where they are told, their scenes, the
    /// eyes extracted for, and their frames.
    view::HeadsetEyes* eyes_ = nullptr;
    /// Where its hands are each frame (D676).
    view::HeadsetHands* held_ = nullptr;
    std::vector<std::unique_ptr<Scene>> eyeScenes_;
    std::size_t eyesSeen_ = 0;
    std::vector<EyeFrame> eyeFrames_;
    std::uint64_t eyeFramesQueued_ = 0;
    std::uint32_t eyeWidth_ = 0;
    std::uint32_t eyeHeight_ = 0;
    std::uint64_t playerFrames_ = 0;
    /// The views into render textures (D361).
    TextureViews textureViews_;
    std::optional<execution::MonotonicInstant> presented_;
    std::size_t gameMeshes_ = 0;
    /// Frames seen through the player's own camera.
    std::uint64_t viewed_ = 0;
    /// Frames seen through a preview's camera (D432).
    std::uint64_t previewed_ = 0;
    std::unique_ptr<Scene> scene_;
    /// The player's World's time as it is shown, which its eyes and its
    /// views show too (D691).
    double shown_ = 0;
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
    RenderScale renderScale_{100, 100};
    std::uint64_t frames_ = 0;
    std::uint64_t drawn_ = 0;
    std::uint64_t placed_ = 0;
    std::uint64_t tracked_ = 0;
    std::uint64_t culled_ = 0;
    std::uint64_t hidden_ = 0;
    std::uint64_t malformed_ = 0;
    std::uint64_t unknownMeshes_ = 0;
    std::uint64_t unknownMaterials_ = 0;
    /// The particles' frames, summed (D360).
    rawframe::particles::Tally particles_;
    std::size_t gameMaterials_ = 0;
    /// What the materials are followed through (D734): the game's content,
    /// its files, the catalog last seen, the materials' revisions in it, and
    /// how often they were read again.
    game_content::GameContent* content_ = nullptr;
    const world_kest::GameFiles* files_ = nullptr;
    std::shared_ptr<const content::ContentCatalog> catalog_;
    std::vector<content::ContentDigest> revisions_;
    std::uint64_t materialReloads_ = 0;
    /// The program materials read, kept while their revisions are.
    Programs programs_;
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
