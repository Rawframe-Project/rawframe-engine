#include "rawframe/base/platform.h"
#include "rawframe/composition/composition.h"
#include "rawframe/composition/configuration.h"
#include "rawframe/render/capture.h"
#include "rawframe/render/device.h"
#include "rawframe/render/display.h"
#include "rawframe/render_canvas/frames.h"
#include "rawframe/render_canvas_gpu/registrar.h"
#include "rawframe/render_canvas_gpu/renderer.h"
#include "rawframe/window/surfaces.h"

#include <algorithm>
#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#if RAWFRAME_FILE_SYSTEM
#include <fstream>
#endif

namespace rawframe::render_canvas_gpu {

namespace {

constexpr diagnostics::EventIdentity kDrawingSummary{"canvas", "drawing_summary"};
constexpr diagnostics::EventIdentity kFailed{"canvas", "drawing_failed"};
constexpr std::string_view kMaybe[] = {render::kDevice.name, render_canvas::kCanvasFrames.name, window::kSurfaces.name};
constexpr std::uint32_t kServer = composition::only(composition::TargetRole::DedicatedServer);
/// How long stopping waits for the last frame: lavapipe draws a view in
/// milliseconds.
constexpr std::uint64_t kFinishNanoseconds = 100'000'000;

/// The pixels a read-back frame's sprites covered: those not the clear
/// black behind them.
std::uint64_t coveredOf(const std::vector<std::byte>& pixels) noexcept {
    std::uint64_t covered = 0;
    for (std::size_t at = 0; at + 3 < pixels.size(); at += 4) {
        if (pixels[at] != std::byte{0} || pixels[at + 1] != std::byte{0} || pixels[at + 2] != std::byte{0}) {
            ++covered;
        }
    }
    return covered;
}

/// Draws the canvas's frames on the one device, one on the GPU at a time:
/// shown on the process's window where it has one, else offscreen when
/// asked; some are read back.
class DrawingParticipant final : public composition::Participant {
public:
    result::Status load(composition::ParticipantContext& context) {
        const composition::Configuration& configuration = context.configuration();
        const std::optional<std::string_view> kOffscreen = configuration.text("canvas.offscreen");
        if (kOffscreen.has_value() && *kOffscreen != "true" && *kOffscreen != "false") {
            return std::unexpected<result::Error>{result::fail(result::ErrorClass::InvalidArgument,
                                                               composition::kCompositionDomain,
                                                               code(composition::CompositionError::BadConfiguration),
                                                               "canvas.offscreen is true or false")
                                                      .error()};
        }
        RAWFRAME_TRY_ASSIGN(readEvery_, configuration.unsignedInteger("canvas.read_every", 60));
        capture_ = configuration.path("canvas.capture");
#if !RAWFRAME_FILE_SYSTEM
        if (capture_.has_value()) {
            // A capture is a file, and there are none here.
            return std::unexpected<result::Error>{result::fail(result::ErrorClass::FailedPrecondition,
                                                               composition::kCompositionDomain,
                                                               code(composition::CompositionError::BadConfiguration),
                                                               "canvas.capture names a file, and there are none here")
                                                      .error()};
        }
#endif
        if (!context.has(render::kDevice.name) || !context.has(render_canvas::kCanvasFrames.name)) {
            return {};
        }
        if (context.has(window::kSurfaces.name)) {
            RAWFRAME_TRY_ASSIGN(windows_, context.capability(window::kSurfaces));
        } else if (kOffscreen != "true") {
            return {};
        }
        RAWFRAME_TRY_ASSIGN(devices_, context.capability(render::kDevice));
        RAWFRAME_TRY_ASSIGN(frames_, context.capability(render_canvas::kCanvasFrames));
        textures_ = [frames = frames_](std::uint64_t id) {
            return frames->texture(id);
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
        const render_canvas::CanvasFrame* frame = frames_->queued();
        if (frame == nullptr) {
            return;
        }
        render::Device* device = devices_->ready();
        if (device == nullptr) {
            ++framesWithoutDevice_;
            return;
        }
        if (renderer_ == nullptr) {
            auto made = CanvasRenderer::create(*device);
            if (!made.has_value()) {
                fail(made.error());
                return;
            }
            renderer_ = std::move(*made);
            if (windows_ != nullptr) {
                auto display = render::Display::create(*device);
                if (!display.has_value()) {
                    fail(display.error());
                    return;
                }
                display_ = std::move(*display);
            }
        }
        if (drawing_) {
            const auto kDone = renderer_->done();
            if (!kDone.has_value()) {
                fail(kDone.error());
                return;
            }
            if (!*kDone) {
                ++framesBusy_;
                return;
            }
            drawing_ = false;
            takePixels();
        }
        const bool kRead = readEvery_ != 0 && (submitted_ + 1) % readEvery_ == 0;
        OffscreenTarget target{.width = frames_->width(), .height = frames_->height(), .readBack = kRead};
        std::optional<ShownOn> shown;
        if (windows_ != nullptr) {
            // Shown on the window, drawn at its size; a window that shows
            // nothing this frame is not drawn for.
            const auto kPrepared =
                windows_->states().empty() ? std::nullopt : devices_->prepare(windows_->states()[0].window);
            if (!kPrepared.has_value() || !kPrepared->second.drawable) {
                ++framesHidden_;
                return;
            }
            target.width = kPrepared->second.size.width;
            target.height = kPrepared->second.size.height;
            // The view follows the window from the next frame.
            frames_->resize(target.width, target.height);
            shown = ShownOn{.display = display_.get(), .surface = kPrepared->first};
        }
        const auto kDrawn = shown.has_value() ? renderer_->render(*frame, textures_, target, *shown)
                                              : renderer_->render(*frame, textures_, target);
        if (!kDrawn.has_value()) {
            fail(kDrawn.error());
            return;
        }
        if (*kDrawn) {
            ++submitted_;
            drawing_ = true;
            if (kRead) {
                readWidth_ = target.width;
                readHeight_ = target.height;
            }
            lastWidth_ = target.width;
            lastHeight_ = target.height;
        }
    }

    void stop() noexcept override {
        if (frames_ == nullptr) {
            return;
        }
        RendererStatistics statistics;
        if (renderer_ != nullptr) {
            if (drawing_ && renderer_->finish(kFinishNanoseconds).has_value()) {
                takePixels();
            }
            statistics = renderer_->statistics();
            // Before the device, which the Runtime holds past the World.
            renderer_.reset();
            display_.reset();
        }
        bool captured = false;
        if (capture_.has_value() && last_.has_value()) {
#if RAWFRAME_FILE_SYSTEM
            const std::vector<std::byte> kImage = render::tgaOf(*last_, capturedWidth_, capturedHeight_);
            std::ofstream file{*capture_, std::ios::binary};
            file.write(reinterpret_cast<const char*>(kImage.data()), static_cast<std::streamsize>(kImage.size()));
            captured = static_cast<bool>(file);
#endif
        }
        emitter_.log(diagnostics::Severity::Info,
                     kDrawingSummary,
                     "what the device drew of one client's canvas",
                     {diagnostics::field("window", windows_ != nullptr),
                      diagnostics::field("width", lastWidth_),
                      diagnostics::field("height", lastHeight_),
                      diagnostics::field("frames", statistics.frames),
                      diagnostics::field("framesShown", statistics.framesShown),
                      diagnostics::field("framesNotShown", statistics.framesNotShown),
                      diagnostics::field("framesHidden", framesHidden_),
                      diagnostics::field("framesWaiting", statistics.framesWaiting),
                      diagnostics::field("framesBusy", framesBusy_),
                      diagnostics::field("framesWithoutDevice", framesWithoutDevice_),
                      diagnostics::field("draws", statistics.draws),
                      diagnostics::field("drawsLeftOut", statistics.drawsLeftOut),
                      diagnostics::field("texturesUploaded", statistics.texturesUploaded),
                      diagnostics::field("uploadBytes", statistics.uploadBytes),
                      diagnostics::field("uploadsDeferred", statistics.uploadsDeferred),
                      diagnostics::field("texturesReplaced", statistics.texturesReplaced),
                      diagnostics::field("readBacks", readBacks_),
                      diagnostics::field("coveredPixels", lastCovered_),
                      diagnostics::field("mostCovered", mostCovered_),
                      diagnostics::field("captured", captured)});
    }

private:
    void takePixels() noexcept {
        if (auto pixels = renderer_->pixels()) {
            ++readBacks_;
            lastCovered_ = coveredOf(*pixels);
            mostCovered_ = std::max(mostCovered_, lastCovered_);
            if (capture_.has_value()) {
                last_ = std::move(*pixels);
                capturedWidth_ = readWidth_;
                capturedHeight_ = readHeight_;
            }
        }
    }

    /// Reported once; nothing more is drawn.
    void fail(const result::Error& error) noexcept {
        failed_ = true;
        emitter_.log(diagnostics::Severity::Error,
                     kFailed,
                     "the canvas could not be drawn: nothing more is",
                     {diagnostics::field("reason", std::string{error.description()})});
    }

    render::DeviceHolder* devices_ = nullptr;
    window::Surfaces* windows_ = nullptr;
    std::unique_ptr<render::Display> display_;
    std::uint64_t framesHidden_ = 0;
    /// The last frame drawn's size, and that of the frames read back.
    std::uint32_t lastWidth_ = 0;
    std::uint32_t lastHeight_ = 0;
    std::uint32_t readWidth_ = 0;
    std::uint32_t readHeight_ = 0;
    std::uint32_t capturedWidth_ = 0;
    std::uint32_t capturedHeight_ = 0;
    render_canvas::CanvasFrames* frames_ = nullptr;
    TextureSource textures_;
    std::uint64_t readEvery_ = 60;
    std::optional<std::string> capture_;
    /// The last frame read back, kept for the capture.
    std::optional<std::vector<std::byte>> last_;
    std::unique_ptr<CanvasRenderer> renderer_;
    bool drawing_ = false;
    bool failed_ = false;
    std::uint64_t submitted_ = 0;
    std::uint64_t framesBusy_ = 0;
    std::uint64_t framesWithoutDevice_ = 0;
    std::uint64_t readBacks_ = 0;
    std::uint64_t lastCovered_ = 0;
    std::uint64_t mostCovered_ = 0;
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
        .lifecycle = {.stopBudget = execution::MonotonicDuration::fromMilliseconds(120)},
        .observabilityIdentity = "render_canvas_gpu.drawing",
        .budgetOwner = "render",
        .hostPhases = composition::hostPhaseBit(composition::HostPhase::Present),
    });
}

} // namespace rawframe::render_canvas_gpu
