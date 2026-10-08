#include "rawframe/composition/composition.h"
#include "rawframe/render/frame.h"
#include "rawframe/render_scene/frames.h"
#include "rawframe/render_scene_gpu/captures.h"
#include "rawframe/render_scene_gpu/registrar.h"
#include "rawframe/render_scene_gpu/renderer.h"

#include <algorithm>
#include <cmath>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace rawframe::render_scene_gpu {

namespace {

constexpr diagnostics::EventIdentity kDrawingSummary{"scene", "scene_drawing_summary"};
constexpr diagnostics::EventIdentity kFailed{"scene", "scene_drawing_failed"};
constexpr std::string_view kMaybe[] = {render::kFrames.name, render_scene::kSceneFrames.name};
constexpr std::string_view kProvided[] = {kSceneCaptures.name};
constexpr std::uint32_t kServer = composition::only(composition::TargetRole::DedicatedServer);
/// The scene's place in a frame: first (SPEC-0024); and its post
/// processes over the composed picture's, after the canvas (D351).
constexpr std::uint32_t kOrder = 0;
constexpr std::uint32_t kComposedOrder = 2;

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
                // The reason, and what it names: the device's outcome, the
                // pipeline, Maul RHI's diagnostic.
                std::string detail;
                for (const auto& each : made.error().context()) {
                    detail += (detail.empty() ? "" : ", ") + std::string{each.key} + " " + std::string{each.value};
                }
                emitter_.log(diagnostics::Severity::Error,
                             kFailed,
                             "the scene could not be drawn: nothing more is",
                             {diagnostics::field("reason", std::string{made.error().description()}),
                              diagnostics::field("detail", detail)});
                return;
            }
            renderer_ = std::move(*made);
            // The render textures' views first, so the scene samples what
            // they drew in the same frame (D361).
            for (const render_scene::TextureFrame& kTexture : scene_->textureFrames()) {
                auto view = TextureView::create(*device, *renderer_, kTexture.id, kTexture.width, kTexture.height);
                if (!view.has_value()) {
                    failed_ = true;
                    emitter_.log(diagnostics::Severity::Error,
                                 kFailed,
                                 "a render texture could not be drawn: nothing more is",
                                 {diagnostics::field("reason", std::string{view.error().description()})});
                    return;
                }
                frames_->join(**view, kOrder);
                views_.push_back(std::move(*view));
                viewPointers_.push_back(views_.back().get());
                // What the scene queued for it before the device could draw
                // is drawn again.
                scene_->missed(kTexture.id);
            }
            // In split-screen, each local player's view, in the players'
            // order, after the render textures (D362).
            for (const render_scene::RegionFrame& kRegion : scene_->regionFrames()) {
                auto view = TextureView::create(*device, *renderer_, 0, scaled(kRegion.width), scaled(kRegion.height));
                if (!view.has_value()) {
                    failed_ = true;
                    emitter_.log(diagnostics::Severity::Error,
                                 kFailed,
                                 "a local player's view could not be drawn: nothing more is",
                                 {diagnostics::field("reason", std::string{view.error().description()})});
                    return;
                }
                frames_->join(**view, kOrder);
                regions_.push_back(std::move(*view));
            }
            frames_->join(*renderer_, kOrder);
            frames_->join(renderer_->composed(), kComposedOrder);
        }
        const auto kPlanned = frames_->planned();
        if (!kPlanned.has_value()) {
            dropViews();
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
            // In split-screen the players' views draw the scene.
            renderer_->prepare(regions_.empty() ? scene_->queued() : nullptr, meshes_, textures_, viewPointers_);
        }
        const std::span<const render_scene::TextureFrame> kTextures = scene_->textureFrames();
        for (std::size_t at = 0; at < views_.size(); ++at) {
            views_[at]->prepare(at < kTextures.size() ? kTextures[at].frame : nullptr, meshes_, textures_);
            if (views_[at]->missed()) {
                scene_->missed(views_[at]->id());
            }
            frames_->ready(*views_[at]);
        }
        const std::span<const render_scene::RegionFrame> kRegions = scene_->regionFrames();
        for (std::size_t at = 0; at < regions_.size() && at < kRegions.size(); ++at) {
            const render_scene::RegionFrame& kRegion = kRegions[at];
            const bool kShown = kRegion.width != 0 && kRegion.height != 0;
            if (kShown) {
                if (auto resized = regions_[at]->resize(scaled(kRegion.width), scaled(kRegion.height));
                    !resized.has_value()) {
                    failed_ = true;
                    emitter_.log(diagnostics::Severity::Error,
                                 kFailed,
                                 "a local player's view could not be drawn: nothing more is",
                                 {diagnostics::field("reason", std::string{resized.error().description()})});
                    return;
                }
            }
            regions_[at]->prepare(kShown ? kRegion.frame : nullptr,
                                  meshes_,
                                  textures_,
                                  viewPointers_,
                                  kShown ? std::optional{Placement{.x = kRegion.x,
                                                                   .y = kRegion.y,
                                                                   .width = kRegion.width,
                                                                   .height = kRegion.height,
                                                                   .bars = scene_->bars()}}
                                         : std::nullopt);
            frames_->ready(*regions_[at]);
        }
        frames_->ready(*renderer_);
        frames_->ready(renderer_->composed());
    }

    /// A local player's region side in the pixels its view is drawn at
    /// (D373): its render scale's share, at least one.
    [[nodiscard]] std::uint32_t scaled(std::uint32_t side) const noexcept {
        return std::max(1U, static_cast<std::uint32_t>(std::lround(static_cast<float>(side) * scene_->renderScale())));
    }

    /// The render textures' frames queued in an iteration no frame draws,
    /// once the device draws, told as missed (D361).
    void dropViews() noexcept {
        if (scene_ == nullptr) {
            return;
        }
        for (const render_scene::TextureFrame& kTexture : scene_->textureFrames()) {
            if (kTexture.frame != nullptr) {
                scene_->missed(kTexture.id);
            }
        }
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
        // In split-screen, the first player's view's (D362).
        const bool kSplit = !regions_.empty();
        std::uint64_t regionFrames = 0;
        for (const std::unique_ptr<TextureView>& region : regions_) {
            regionFrames += region->statistics().frames;
        }
        if (kSplit) {
            statistics = regions_.front()->statistics();
        }
        for (const std::unique_ptr<TextureView>& region : regions_) {
            frames_->leave(*region);
        }
        regions_.clear();
        if (renderer_ != nullptr) {
            if (!kSplit) {
                statistics = renderer_->statistics();
            }
            frames_->leave(renderer_->composed());
            frames_->leave(*renderer_);
            renderer_.reset();
        }
        std::uint64_t viewFrames = 0;
        for (const std::unique_ptr<TextureView>& view : views_) {
            viewFrames += view->statistics().frames;
            frames_->leave(*view);
        }
        views_.clear();
        viewPointers_.clear();
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
                      diagnostics::field("framesReflected", statistics.framesReflected),
                      diagnostics::field("framesMotionBlurred", statistics.framesMotionBlurred),
                      diagnostics::field("framesFocused", statistics.framesFocused),
                      diagnostics::field("framesContactShadowed", statistics.framesContactShadowed),
                      diagnostics::field("decalsDrawn", statistics.decalsDrawn),
                      diagnostics::field("probesDrawn", statistics.probesDrawn),
                      diagnostics::field("framesMultisampled", statistics.framesMultisampled),
                      diagnostics::field("postProcessesRun", statistics.postProcessesRun),
                      diagnostics::field("postProcessesLeftOut", statistics.postProcessesLeftOut),
                      diagnostics::field("emittersDrawn", statistics.emittersDrawn),
                      diagnostics::field("emittersLeftOut", statistics.emittersLeftOut),
                      diagnostics::field("particlesSpawned", statistics.particlesSpawned),
                      diagnostics::field("ribbonsDrawn", statistics.ribbonsDrawn),
                      diagnostics::field("viewFrames", viewFrames),
                      diagnostics::field("regionFrames", regionFrames),
                      diagnostics::field("modelsSkinned", statistics.modelsSkinned)});
    }

private:
    render::Frames* frames_ = nullptr;
    render_scene::SceneFrames* scene_ = nullptr;
    MeshSource meshes_;
    TextureSource textures_;
    std::unique_ptr<SceneRenderer> renderer_;
    /// The render textures' views (D361), drawn before the scene.
    std::vector<std::unique_ptr<TextureView>> views_;
    std::vector<TextureView*> viewPointers_;
    /// In split-screen, the local players' views (D362), drawn after them.
    std::vector<std::unique_ptr<TextureView>> regions_;
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
