#include "rawframe/composition/composition.h"
#include "rawframe/composition/configuration.h"
#include "rawframe/physics3d/components.h"
#include "rawframe/render_scene/errors.h"
#include "rawframe/render_scene/frames.h"
#include "rawframe/render_scene/registrar.h"
#include "rawframe/render_scene/scene.h"
#include "rawframe/world_kest/game_files.h"
#include "rawframe/world_replication/client_worlds.h"

#include <algorithm>
#include <memory>
#include <optional>
#include <string>

namespace rawframe::render_scene {

namespace {

constexpr diagnostics::EventIdentity kSceneSummary{"scene", "scene_summary"};
constexpr std::string_view kProvided[] = {kSceneFrames.name};
constexpr std::string_view kMaybe[] = {world_replication::kClientWorlds.name, world_kest::kGameFiles.name};

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
        shadows_ = ShadowSettings{.cascades = static_cast<std::uint32_t>(kCascades),
                                  .distance = static_cast<float>(kDistance),
                                  .logarithmicBlend = kShadowDefaults.logarithmicBlend,
                                  .side = static_cast<std::uint32_t>(kSide)};
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
        gameMeshes_ = game->meshes.size();
        settings_ = SceneSettings{.models = std::move(game->models),
                                  .sun = game->sun,
                                  .sky = game->sky,
                                  .points = std::move(game->points),
                                  .spots = std::move(game->spots),
                                  .meshes = std::move(game->meshes),
                                  .shadows = shadows_};
        return {};
    }

    result::Status start(composition::ParticipantContext& context) noexcept override {
        emitter_ = context.emitter();
        return {};
    }

    void runHostPhase(composition::HostPhase phase, const composition::HostFrame& /*frame*/) noexcept override {
        if (clients_ == nullptr) {
            return;
        }
        if (phase == composition::HostPhase::PresentationExtract) {
            queued_ = nullptr;
            extract();
        } else if (phase == composition::HostPhase::Present && extracted_) {
            extracted_ = false;
            camera_.aspect = static_cast<float>(width_) / static_cast<float>(height_);
            const SceneFrame& kFrame = scene_->queue(camera_);
            queued_ = &kFrame;
            ++frames_;
            drawn_ += kFrame.drawn;
            culled_ += kFrame.culled;
            hidden_ += kFrame.hidden;
            malformed_ += kFrame.malformed;
            unknownMeshes_ += kFrame.unknownMeshes;
            overLimit_ += kFrame.overLimit;
            mostDraws_ = std::max(mostDraws_, kFrame.draws.size());
            lightsLit_ += kFrame.lights3d.size();
            lightsCulled_ += kFrame.lightsCulled;
            lightsOverLimit_ += kFrame.lightsOverLimit;
            clusterOverflow_ += kFrame.clusterOverflow;
        }
    }

    void stop() noexcept override {
        if (clients_ == nullptr) {
            return;
        }
        emitter_.log(diagnostics::Severity::Info,
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
                      diagnostics::field("lightsLit", lightsLit_),
                      diagnostics::field("lightsCulled", lightsCulled_),
                      diagnostics::field("lightsOverLimit", lightsOverLimit_),
                      diagnostics::field("clusterOverflow", clusterOverflow_)});
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

    std::shared_ptr<const mesh::Mesh> mesh(std::uint64_t id) const override {
        return scene_ != nullptr ? scene_->mesh(id) : nullptr;
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
    std::uint64_t overLimit_ = 0;
    std::size_t mostDraws_ = 0;
    /// The point and spot lights each frame lit with, culled, and left out
    /// at the limit; a cluster's lights past its limit (D290).
    std::uint64_t lightsLit_ = 0;
    std::uint64_t lightsCulled_ = 0;
    std::uint64_t lightsOverLimit_ = 0;
    std::uint64_t clusterOverflow_ = 0;
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
        .lifecycle = {.stopBudget = execution::MonotonicDuration::fromMilliseconds(10)},
        .observabilityIdentity = "render_scene.scene",
        .budgetOwner = "render",
        .hostPhases =
            static_cast<std::uint16_t>(composition::hostPhaseBit(composition::HostPhase::PresentationExtract) |
                                       composition::hostPhaseBit(composition::HostPhase::Present)),
    });
}

} // namespace rawframe::render_scene
