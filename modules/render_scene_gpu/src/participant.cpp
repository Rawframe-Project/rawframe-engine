#include "rawframe/base/platform.h"
#include "rawframe/composition/composition.h"
#include "rawframe/composition/configuration.h"
#include "rawframe/render/capture.h"
#include "rawframe/render/device.h"
#include "rawframe/render_scene/frames.h"
#include "rawframe/render_scene_gpu/registrar.h"
#include "rawframe/render_scene_gpu/renderer.h"

#include <algorithm>
#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <unordered_set>
#include <vector>

#if RAWFRAME_FILE_SYSTEM
#include <fstream>
#endif

namespace rawframe::render_scene_gpu {

namespace {

constexpr diagnostics::EventIdentity kDrawingSummary{"scene", "scene_drawing_summary"};
constexpr diagnostics::EventIdentity kFailed{"scene", "scene_drawing_failed"};
constexpr std::string_view kMaybe[] = {render::kDevice.name, render_scene::kSceneFrames.name};
constexpr std::uint32_t kServer = composition::only(composition::TargetRole::DedicatedServer);
/// How long stopping waits for the last frame: lavapipe draws a view in
/// milliseconds.
constexpr std::uint64_t kFinishNanoseconds = 100'000'000;
/// Colors counted in a picture at most: a lit scene has many shades, an
/// empty one one.
constexpr std::size_t kMostColors = 4096;

/// The colors of a read-back picture, up to `kMostColors`.
std::uint64_t colorsOf(const std::vector<std::byte>& pixels) {
    std::unordered_set<std::uint32_t> colors;
    for (std::size_t at = 0; at + 3 < pixels.size() && colors.size() < kMostColors; at += 4) {
        colors.insert(std::to_integer<std::uint32_t>(pixels[at]) |
                      (std::to_integer<std::uint32_t>(pixels[at + 1]) << 8U) |
                      (std::to_integer<std::uint32_t>(pixels[at + 2]) << 16U));
    }
    return colors.size();
}

/// Draws the scene's frames on the one device offscreen, one on the GPU at
/// a time; some are read back.
class DrawingParticipant final : public composition::Participant {
public:
    result::Status load(composition::ParticipantContext& context) {
        const composition::Configuration& configuration = context.configuration();
        const std::optional<std::string_view> kOffscreen = configuration.text("scene.offscreen");
        if (kOffscreen.has_value() && *kOffscreen != "true" && *kOffscreen != "false") {
            return std::unexpected<result::Error>{result::fail(result::ErrorClass::InvalidArgument,
                                                               composition::kCompositionDomain,
                                                               code(composition::CompositionError::BadConfiguration),
                                                               "scene.offscreen is true or false")
                                                      .error()};
        }
        RAWFRAME_TRY_ASSIGN(readEvery_, configuration.unsignedInteger("scene.read_every", 60));
        capture_ = configuration.path("scene.capture");
#if !RAWFRAME_FILE_SYSTEM
        if (capture_.has_value()) {
            // A capture is a file, and there are none here.
            return std::unexpected<result::Error>{result::fail(result::ErrorClass::FailedPrecondition,
                                                               composition::kCompositionDomain,
                                                               code(composition::CompositionError::BadConfiguration),
                                                               "scene.capture names a file, and there are none here")
                                                      .error()};
        }
#endif
        if (kOffscreen != "true" || !context.has(render::kDevice.name) ||
            !context.has(render_scene::kSceneFrames.name)) {
            return {};
        }
        RAWFRAME_TRY_ASSIGN(devices_, context.capability(render::kDevice));
        RAWFRAME_TRY_ASSIGN(frames_, context.capability(render_scene::kSceneFrames));
        meshes_ = [frames = frames_](std::uint64_t id) {
            return frames->mesh(id);
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
        const render_scene::SceneFrame* frame = frames_->queued();
        if (frame == nullptr) {
            return;
        }
        render::Device* device = devices_->ready();
        if (device == nullptr) {
            ++framesWithoutDevice_;
            return;
        }
        if (renderer_ == nullptr) {
            auto made = SceneRenderer::create(*device);
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
        const SceneTarget kTarget{.width = frames_->width(), .height = frames_->height(), .readBack = kRead};
        const auto kDrawn = renderer_->render(*frame, meshes_, kTarget);
        if (!kDrawn.has_value()) {
            fail(kDrawn.error());
            return;
        }
        if (*kDrawn) {
            ++submitted_;
            drawing_ = true;
            if (kRead) {
                readWidth_ = kTarget.width;
                readHeight_ = kTarget.height;
            }
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
#if RAWFRAME_FILE_SYSTEM
            const std::vector<std::byte> kImage = render::tgaOf(*last_, capturedWidth_, capturedHeight_);
            std::ofstream file{*capture_, std::ios::binary};
            file.write(reinterpret_cast<const char*>(kImage.data()), static_cast<std::streamsize>(kImage.size()));
            captured = static_cast<bool>(file);
#endif
        }
        emitter_.log(diagnostics::Severity::Info,
                     kDrawingSummary,
                     "what the device drew of one client's scene",
                     {diagnostics::field("width", frames_->width()),
                      diagnostics::field("height", frames_->height()),
                      diagnostics::field("frames", statistics.frames),
                      diagnostics::field("framesWaiting", statistics.framesWaiting),
                      diagnostics::field("framesBusy", framesBusy_),
                      diagnostics::field("framesWithoutDevice", framesWithoutDevice_),
                      diagnostics::field("models", statistics.models),
                      diagnostics::field("drawCalls", statistics.drawCalls),
                      diagnostics::field("modelsLeftOut", statistics.modelsLeftOut),
                      diagnostics::field("meshesUploaded", statistics.meshesUploaded),
                      diagnostics::field("uploadBytes", statistics.uploadBytes),
                      diagnostics::field("uploadsDeferred", statistics.uploadsDeferred),
                      diagnostics::field("readBacks", readBacks_),
                      diagnostics::field("colors", lastColors_),
                      diagnostics::field("captured", captured)});
    }

private:
    void takePixels() noexcept {
        if (auto pixels = renderer_->pixels()) {
            ++readBacks_;
            lastColors_ = colorsOf(*pixels);
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
                     "the scene could not be drawn: nothing more is",
                     {diagnostics::field("reason", std::string{error.description()})});
    }

    render::DeviceHolder* devices_ = nullptr;
    render_scene::SceneFrames* frames_ = nullptr;
    MeshSource meshes_;
    std::uint32_t readWidth_ = 0;
    std::uint32_t readHeight_ = 0;
    std::uint32_t capturedWidth_ = 0;
    std::uint32_t capturedHeight_ = 0;
    std::uint64_t readEvery_ = 60;
    std::optional<std::string> capture_;
    /// The last frame read back, kept for the capture.
    std::optional<std::vector<std::byte>> last_;
    std::unique_ptr<SceneRenderer> renderer_;
    bool drawing_ = false;
    bool failed_ = false;
    std::uint64_t submitted_ = 0;
    std::uint64_t framesBusy_ = 0;
    std::uint64_t framesWithoutDevice_ = 0;
    std::uint64_t readBacks_ = 0;
    std::uint64_t lastColors_ = 0;
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
        .optionalCapabilities = kMaybe,
        .eligibility = {.roles = ~kServer},
        .lifecycle = {.stopBudget = execution::MonotonicDuration::fromMilliseconds(120)},
        .observabilityIdentity = "render_scene_gpu.drawing",
        .budgetOwner = "render",
        .hostPhases = composition::hostPhaseBit(composition::HostPhase::Present),
    });
}

} // namespace rawframe::render_scene_gpu
