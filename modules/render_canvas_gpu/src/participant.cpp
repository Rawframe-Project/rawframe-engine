#include "rawframe/composition/composition.h"
#include "rawframe/composition/configuration.h"
#include "rawframe/render/device.h"
#include "rawframe/render_canvas/frames.h"
#include "rawframe/render_canvas_gpu/registrar.h"
#include "rawframe/render_canvas_gpu/renderer.h"

#include <algorithm>
#include <cstddef>
#include <fstream>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace rawframe::render_canvas_gpu {

namespace {

constexpr diagnostics::EventIdentity kOffscreenSummary{"canvas", "offscreen_summary"};
constexpr diagnostics::EventIdentity kFailed{"canvas", "offscreen_failed"};
constexpr std::string_view kMaybe[] = {render::kDevice.name, render_canvas::kCanvasFrames.name};
constexpr std::uint32_t kServer = composition::only(composition::TargetRole::DedicatedServer);
/// How long stopping waits for the last frame: lavapipe draws a view in
/// milliseconds.
constexpr std::uint64_t kFinishNanoseconds = 250'000'000;

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

/// A frame's pixels, RGBA8 rows top first, as an uncompressed TGA image:
/// the simplest file every image tool opens.
std::vector<std::byte> tgaOf(const std::vector<std::byte>& pixels, std::uint32_t width, std::uint32_t height) {
    std::vector<std::byte> image(18 + pixels.size());
    image[2] = std::byte{2};
    image[12] = static_cast<std::byte>(width & 0xFFU);
    image[13] = static_cast<std::byte>(width >> 8U);
    image[14] = static_cast<std::byte>(height & 0xFFU);
    image[15] = static_cast<std::byte>(height >> 8U);
    image[16] = std::byte{32};
    // Eight bits of alpha, rows top first.
    image[17] = std::byte{0x28};
    for (std::size_t at = 0; at + 3 < pixels.size(); at += 4) {
        image[18 + at] = pixels[at + 2];
        image[18 + at + 1] = pixels[at + 1];
        image[18 + at + 2] = pixels[at];
        image[18 + at + 3] = pixels[at + 3];
    }
    return image;
}

/// Draws the canvas's frames offscreen on the one device, one on the GPU
/// at a time, and reads some back.
class OffscreenParticipant final : public composition::Participant {
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
        RAWFRAME_TRY_ASSIGN(readEvery_, configuration.unsignedInteger("canvas.offscreen_read_every", 60));
        capture_ = configuration.path("canvas.offscreen_capture");
        if (kOffscreen != "true" || !context.has(render::kDevice.name) ||
            !context.has(render_canvas::kCanvasFrames.name)) {
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
        const auto kDrawn = renderer_->render(
            *frame,
            textures_,
            OffscreenTarget{.width = frames_->width(), .height = frames_->height(), .readBack = kRead});
        if (!kDrawn.has_value()) {
            fail(kDrawn.error());
            return;
        }
        if (*kDrawn) {
            ++submitted_;
            drawing_ = true;
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
        }
        bool captured = false;
        if (capture_.has_value() && last_.has_value()) {
            const std::vector<std::byte> kImage = tgaOf(*last_, frames_->width(), frames_->height());
            std::ofstream file{*capture_, std::ios::binary};
            file.write(reinterpret_cast<const char*>(kImage.data()), static_cast<std::streamsize>(kImage.size()));
            captured = static_cast<bool>(file);
        }
        emitter_.log(diagnostics::Severity::Info,
                     kOffscreenSummary,
                     "what the device drew of one client's canvas, offscreen",
                     {diagnostics::field("width", frames_->width()),
                      diagnostics::field("height", frames_->height()),
                      diagnostics::field("frames", statistics.frames),
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
    auto participant = std::make_unique<OffscreenParticipant>();
    RAWFRAME_TRY(participant->load(context));
    return composition::ParticipantOwner{participant.release()};
}

} // namespace

void registerParticipants(composition::ParticipantRegistrar& registrar) noexcept {
    registrar.submit(composition::ParticipantDeclaration{
        .identity = "rawframe.render_canvas_gpu.offscreen",
        .factory = &make,
        .scope = composition::LifetimeScope::World,
        .optionalCapabilities = kMaybe,
        .eligibility = {.roles = ~kServer},
        .lifecycle = {.stopBudget = execution::MonotonicDuration::fromMilliseconds(300)},
        .observabilityIdentity = "render_canvas_gpu.offscreen",
        .budgetOwner = "render",
        .hostPhases = composition::hostPhaseBit(composition::HostPhase::Present),
    });
}

} // namespace rawframe::render_canvas_gpu
