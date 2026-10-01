#include "rawframe/composition/composition.h"
#include "rawframe/render/frame.h"
#include "rawframe/render_canvas/frames.h"
#include "rawframe/render_canvas_gpu/registrar.h"
#include "rawframe/render_canvas_gpu/renderer.h"

#include <memory>
#include <string>

namespace rawframe::render_canvas_gpu {

namespace {

constexpr diagnostics::EventIdentity kDrawingSummary{"canvas", "drawing_summary"};
constexpr diagnostics::EventIdentity kFailed{"canvas", "drawing_failed"};
constexpr std::string_view kMaybe[] = {render::kFrames.name, render_canvas::kCanvasFrames.name};
constexpr std::uint32_t kServer = composition::only(composition::TargetRole::DedicatedServer);
/// The canvas's place in a frame: after the scene (SPEC-0024).
constexpr std::uint32_t kOrder = 1;

/// Records the canvas's frames into the frames `render` makes (D285): in
/// each `present` that plans a frame, the view takes the frame's size and
/// the frame the canvas queued is prepared for it.
class DrawingParticipant final : public composition::Participant {
public:
    result::Status load(composition::ParticipantContext& context) {
        if (!context.has(render::kFrames.name) || !context.has(render_canvas::kCanvasFrames.name)) {
            return {};
        }
        RAWFRAME_TRY_ASSIGN(frames_, context.capability(render::kFrames));
        RAWFRAME_TRY_ASSIGN(canvas_, context.capability(render_canvas::kCanvasFrames));
        textures_ = [canvas = canvas_](std::uint64_t id) {
            return canvas->texture(id);
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
            if (device == nullptr || canvas_->queued() == nullptr) {
                return;
            }
            auto made = CanvasRenderer::create(*device);
            if (!made.has_value()) {
                failed_ = true;
                emitter_.log(diagnostics::Severity::Error,
                             kFailed,
                             "the canvas could not be drawn: nothing more is",
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
        canvas_->resize(kPlanned->first, kPlanned->second);
        renderer_->prepare(canvas_->queued(), textures_);
        frames_->ready(*renderer_);
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
                     "what the device drew of one client's canvas",
                     {diagnostics::field("frames", statistics.frames),
                      diagnostics::field("framesWaiting", statistics.framesWaiting),
                      diagnostics::field("draws", statistics.draws),
                      diagnostics::field("drawsLeftOut", statistics.drawsLeftOut),
                      diagnostics::field("texturesUploaded", statistics.texturesUploaded),
                      diagnostics::field("uploadBytes", statistics.uploadBytes),
                      diagnostics::field("uploadsDeferred", statistics.uploadsDeferred),
                      diagnostics::field("texturesReplaced", statistics.texturesReplaced),
                      diagnostics::field("emittersDrawn", statistics.emittersDrawn),
                      diagnostics::field("emittersLeftOut", statistics.emittersLeftOut),
                      diagnostics::field("particlesSpawned", statistics.particlesSpawned),
                      diagnostics::field("ribbonsDrawn", statistics.ribbonsDrawn)});
    }

private:
    render::Frames* frames_ = nullptr;
    render_canvas::CanvasFrames* canvas_ = nullptr;
    TextureSource textures_;
    std::unique_ptr<CanvasRenderer> renderer_;
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
        .identity = "rawframe.render_canvas_gpu.drawing",
        .factory = &make,
        .scope = composition::LifetimeScope::World,
        .optionalCapabilities = kMaybe,
        .eligibility = {.roles = ~kServer},
        // Stopping waits for nothing: the frames' owner waits for the last.
        .lifecycle = {.stopBudget = execution::MonotonicDuration::fromMilliseconds(10)},
        .observabilityIdentity = "render_canvas_gpu.drawing",
        .budgetOwner = "render",
        .hostPhases = composition::hostPhaseBit(composition::HostPhase::Present),
    });
}

} // namespace rawframe::render_canvas_gpu
