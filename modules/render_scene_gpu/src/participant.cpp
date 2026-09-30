#include "rawframe/composition/composition.h"
#include "rawframe/render/frame.h"
#include "rawframe/render_scene/frames.h"
#include "rawframe/render_scene_gpu/captures.h"
#include "rawframe/render_scene_gpu/registrar.h"
#include "rawframe/render_scene_gpu/renderer.h"

#include <memory>
#include <optional>
#include <string>
#include <utility>

namespace rawframe::render_scene_gpu {

namespace {

constexpr diagnostics::EventIdentity kDrawingSummary{"scene", "scene_drawing_summary"};
constexpr diagnostics::EventIdentity kFailed{"scene", "scene_drawing_failed"};
constexpr std::string_view kMaybe[] = {render::kFrames.name, render_scene::kSceneFrames.name};
constexpr std::string_view kProvided[] = {kSceneCaptures.name};
constexpr std::uint32_t kServer = composition::only(composition::TargetRole::DedicatedServer);
/// The scene's place in a frame: first (SPEC-0024).
constexpr std::uint32_t kOrder = 0;

/// Records the scene's frames into the frames `render` makes (D285): in
/// each `present` that plans a frame, the view takes the frame's size and
/// the frame the scene queued is prepared for it, or the frame a tool asks
/// to capture (D326).
class DrawingParticipant final : public composition::Participant, public SceneCaptures {
public:
    result::Status load(composition::ParticipantContext& context) {
        if (!context.has(render::kFrames.name) || !context.has(render_scene::kSceneFrames.name)) {
            return {};
        }
        RAWFRAME_TRY_ASSIGN(frames_, context.capability(render::kFrames));
        RAWFRAME_TRY_ASSIGN(scene_, context.capability(render_scene::kSceneFrames));
        meshes_ = [scene = scene_](std::uint64_t id) {
            return scene->mesh(id);
        };
        textures_ = [scene = scene_](std::uint64_t id) {
            return scene->texture(id);
        };
        return {};
    }

    result::Status start(composition::ParticipantContext& context) noexcept override {
        emitter_ = context.emitter();
        return {};
    }

    void runHostPhase(composition::HostPhase /*phase*/, const composition::HostFrame& /*frame*/) noexcept override {
        if (frames_ == nullptr || failed_) {
            return;
        }
        // Joined once the device is ready and there is something to draw,
        // drawing from the next frame planned.
        if (renderer_ == nullptr) {
            render::Device* device = frames_->device();
            if (device == nullptr || scene_->queued() == nullptr) {
                return;
            }
            auto made = SceneRenderer::create(*device);
            if (!made.has_value()) {
                failed_ = true;
                emitter_.log(diagnostics::Severity::Error,
                             kFailed,
                             "the scene could not be drawn: nothing more is",
                             {diagnostics::field("reason", std::string{made.error().description()})});
                return;
            }
            renderer_ = std::move(*made);
            frames_->join(*renderer_, kOrder);
        }
        const auto kPlanned = frames_->planned();
        if (!kPlanned.has_value()) {
            return;
        }
        // The view follows the frame from the next one.
        scene_->resize(kPlanned->first, kPlanned->second);
        if (capturing_.has_value()) {
            renderer_->prepare(&*capturing_, meshes_, textures_);
            if (!asked_) {
                renderer_->capture();
                asked_ = true;
            }
        } else {
            renderer_->prepare(scene_->queued(), meshes_, textures_);
        }
        frames_->ready(*renderer_);
    }

    composition::CapabilityObject provide(std::string_view capability) noexcept override {
        if (capability == kSceneCaptures.name) {
            return composition::provideAs<SceneCaptures>(*this);
        }
        return {};
    }

    bool capture(render_scene::SceneFrame frame) override {
        if (renderer_ == nullptr || capturing_.has_value()) {
            return false;
        }
        capturing_ = std::move(frame);
        asked_ = false;
        return true;
    }

    std::optional<LightCapture> captured() override {
        if (renderer_ == nullptr || !capturing_.has_value() || !asked_) {
            return std::nullopt;
        }
        std::optional<LightCapture> taken = renderer_->captured();
        if (taken.has_value()) {
            capturing_.reset();
            asked_ = false;
        }
        return taken;
    }

    void stop() noexcept override {
        if (frames_ == nullptr) {
            return;
        }
        RendererStatistics statistics;
        if (renderer_ != nullptr) {
            statistics = renderer_->statistics();
            frames_->leave(*renderer_);
            renderer_.reset();
        }
        emitter_.log(diagnostics::Severity::Info,
                     kDrawingSummary,
                     "what the device drew of one client's scene",
                     {diagnostics::field("frames", statistics.frames),
                      diagnostics::field("framesWaiting", statistics.framesWaiting),
                      diagnostics::field("models", statistics.models),
                      diagnostics::field("drawCalls", statistics.drawCalls),
                      diagnostics::field("modelsLeftOut", statistics.modelsLeftOut),
                      diagnostics::field("meshesUploaded", statistics.meshesUploaded),
                      diagnostics::field("texturesUploaded", statistics.texturesUploaded),
                      diagnostics::field("uploadBytes", statistics.uploadBytes),
                      diagnostics::field("uploadsDeferred", statistics.uploadsDeferred),
                      diagnostics::field("framesResolved", statistics.framesResolved),
                      diagnostics::field("historyReused", statistics.historyReused),
                      diagnostics::field("framesMetered", statistics.framesMetered),
                      diagnostics::field("framesSmoothed", statistics.framesSmoothed),
                      diagnostics::field("framesOccluded", statistics.framesOccluded),
                      diagnostics::field("framesBloomed", statistics.framesBloomed),
                      diagnostics::field("framesReflected", statistics.framesReflected)});
    }

private:
    render::Frames* frames_ = nullptr;
    render_scene::SceneFrames* scene_ = nullptr;
    MeshSource meshes_;
    TextureSource textures_;
    std::unique_ptr<SceneRenderer> renderer_;
    /// The frame a tool asked to capture, and whether the renderer was
    /// asked to read it.
    std::optional<render_scene::SceneFrame> capturing_;
    bool asked_ = false;
    bool failed_ = false;
    diagnostics::Emitter emitter_;
};

result::Result<composition::ParticipantOwner> make(composition::ParticipantContext& context) noexcept {
    auto participant = std::make_unique<DrawingParticipant>();
    RAWFRAME_TRY(participant->load(context));
    return composition::ParticipantOwner{participant.release()};
}

} // namespace

void registerParticipants(composition::ParticipantRegistrar& registrar) noexcept {
    registrar.submit(composition::ParticipantDeclaration{
        .identity = "rawframe.render_scene_gpu.drawing",
        .factory = &make,
        .scope = composition::LifetimeScope::World,
        .providedCapabilities = kProvided,
        .optionalCapabilities = kMaybe,
        .eligibility = {.roles = ~kServer},
        // Stopping waits for nothing: the frames' owner waits for the last.
        .lifecycle = {.stopBudget = execution::MonotonicDuration::fromMilliseconds(10)},
        .observabilityIdentity = "render_scene_gpu.drawing",
        .budgetOwner = "render",
        .hostPhases = composition::hostPhaseBit(composition::HostPhase::Present),
    });
}

} // namespace rawframe::render_scene_gpu
