#include "frames.h"

#include "rawframe/base/platform.h"
#include "rawframe/composition/composition.h"
#include "rawframe/composition/configuration.h"
#include "rawframe/render/capture.h"
#include "rawframe/render/frame.h"
#include "rawframe/render/registrar.h"

#include <algorithm>
#include <memory>
#include <optional>
#include <string>
#include <unordered_set>
#include <vector>

#if RAWFRAME_FILE_SYSTEM
#include <fstream>
#endif

namespace rawframe::render {

namespace {

constexpr diagnostics::EventIdentity kFrameSummary{"render", "frame_summary"};
constexpr diagnostics::EventIdentity kFailed{"render", "frame_failed"};
constexpr std::string_view kProvided[] = {kFrames.name};
/// Whether a picture of `width` by `height` fits what the device keeps for
/// readbacks: 8-bit RGBA rows at a 256-byte pitch, the whole at a 512-byte
/// boundary (Maul RHI's `readbackBytes`).
bool readable(std::uint32_t width, std::uint32_t height) noexcept {
    const std::uint64_t kPitch = (std::uint64_t{width} * 4 + 255) / 256 * 256;
    return (kPitch * height + 511) / 512 * 512 <= kReadbackBytes;
}

/// A frame's budget where `render.frame_rate` sets none (D533).
constexpr execution::MonotonicDuration kSixtieth{1'000'000'000 / 60};
constexpr std::string_view kMaybe[] = {kDevice.name, window::kSurfaces.name};
constexpr std::uint32_t kServer = composition::only(composition::TargetRole::DedicatedServer);
/// How long stopping waits for the last frame: lavapipe draws a view in
/// milliseconds.
constexpr std::uint64_t kFinishNanoseconds = 100'000'000;
/// Colors counted in a picture at most: a lit scene has many shades, an
/// empty picture one.
constexpr std::size_t kMostColors = 4096;

/// The pixels of a read-back picture drawn over: those not the black it
/// was cleared to.
std::uint64_t coveredOf(const std::vector<std::byte>& pixels) noexcept {
    std::uint64_t covered = 0;
    for (std::size_t at = 0; at + 3 < pixels.size(); at += 4) {
        if (pixels[at] != std::byte{0} || pixels[at + 1] != std::byte{0} || pixels[at + 2] != std::byte{0}) {
            ++covered;
        }
    }
    return covered;
}

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

/// Owns the frames (D285): at the start of each `present` it plans the
/// Host iteration's frame, at the first window's size where the process
/// has windows, else offscreen when asked; the bridges that joined prepare
/// their parts and say they are ready, and the last makes the frame. Some
/// are read back.
class FrameParticipant final : public composition::Participant, public Frames {
public:
    result::Status load(composition::ParticipantContext& context) {
        const composition::Configuration& configuration = context.configuration();
        const std::optional<std::string_view> kOffscreen = configuration.text("render.offscreen");
        if (kOffscreen.has_value() && *kOffscreen != "true" && *kOffscreen != "false") {
            return badConfiguration("render.offscreen is true or false");
        }
        RAWFRAME_TRY_ASSIGN(const std::uint64_t kWidth, configuration.unsignedInteger("render.width", 1280));
        RAWFRAME_TRY_ASSIGN(const std::uint64_t kHeight, configuration.unsignedInteger("render.height", 720));
        if (kWidth == 0 || kHeight == 0 || kWidth > 8192 || kHeight > 8192) {
            return badConfiguration("render.width and render.height are 1 to 8192 pixels");
        }
        width_ = static_cast<std::uint32_t>(kWidth);
        height_ = static_cast<std::uint32_t>(kHeight);
        RAWFRAME_TRY_ASSIGN(readEvery_, configuration.unsignedInteger("render.read_every", 60));
        RAWFRAME_TRY_ASSIGN(const std::uint64_t kFrameRate, configuration.unsignedInteger("render.frame_rate", 0));
        if (kFrameRate > 1000) {
            return badConfiguration("render.frame_rate is 0 for no limit, or 1 to 1000 frames a second");
        }
        if (kFrameRate != 0) {
            framePeriod_ = execution::MonotonicDuration{static_cast<std::int64_t>(1'000'000'000 / kFrameRate)};
        }
        capture_ = configuration.path("render.capture");
#if !RAWFRAME_FILE_SYSTEM
        if (capture_.has_value()) {
            // A capture is a file, and there are none here.
            return std::unexpected<result::Error>{result::fail(result::ErrorClass::FailedPrecondition,
                                                               composition::kCompositionDomain,
                                                               code(composition::CompositionError::BadConfiguration),
                                                               "render.capture names a file, and there are none here")
                                                      .error()};
        }
#endif
        if (!context.has(kDevice.name)) {
            return {};
        }
        if (context.has(window::kSurfaces.name)) {
            RAWFRAME_TRY_ASSIGN(windows_, context.capability(window::kSurfaces));
        } else if (kOffscreen != "true") {
            return {};
        }
        RAWFRAME_TRY_ASSIGN(devices_, context.capability(kDevice));
        return {};
    }

    result::Status start(composition::ParticipantContext& context) noexcept override {
        emitter_ = context.emitter();
        return {};
    }

    void runHostPhase(composition::HostPhase /*phase*/, const composition::HostFrame& frame) noexcept override {
        // The last iteration's plan, if a recorder never said it was ready.
        if (planned_.has_value() && !made_) {
            ++framesIncomplete_;
        }
        planned_.reset();
        made_ = false;
        ready_.clear();
        unchanged_.clear();
        now_ = frame.now;
        if (devices_ == nullptr || failed_ || joined_.empty()) {
            return;
        }
        // How the last frame keeps up with the device, seen in every
        // iteration, the frame limit's too (D533).
        watchPace();
        // Not sooner than the frame limit allows after the last (D495).
        if (framePeriod_.has_value() && madeAt_.has_value() && now_ - *madeAt_ < *framePeriod_) {
            ++framesLimited_;
            return;
        }
        Device* device = devices_->ready();
        if (device == nullptr) {
            ++framesWithoutDevice_;
            return;
        }
        if (framer_ == nullptr) {
            auto made = Framer::create(*device);
            if (!made.has_value()) {
                fail(made.error());
                return;
            }
            framer_ = std::move(*made);
        }
        const auto kDone = framer_->done();
        if (!kDone.has_value()) {
            fail(kDone.error());
            return;
        }
        if (!*kDone) {
            ++framesBusy_;
            return;
        }
        takePixels();
        target_ = FrameTarget{.width = width_,
                              .height = height_,
                              .readBack = readEvery_ != 0 && (submitted_ + 1) % readEvery_ == 0,
                              .surface = std::nullopt};
        if (windows_ != nullptr) {
            // Shown on the window, drawn at its size; a window that shows
            // nothing this frame is not drawn for.
            const auto kPrepared =
                windows_->states().empty() ? std::nullopt : devices_->prepare(windows_->states()[0].window);
            if (!kPrepared.has_value() || !kPrepared->second.drawable) {
                ++framesHidden_;
                return;
            }
            target_.width = kPrepared->second.size.width;
            target_.height = kPrepared->second.size.height;
            target_.surface = kPrepared->first;
        }
        // A picture past what the device keeps for readbacks is not read
        // back: asked anyway, it failed the frame, and no frame was made
        // again, so a window put on a 4K screen froze (D534).
        if (target_.readBack && !readable(target_.width, target_.height)) {
            target_.readBack = false;
            ++readBacksTooLarge_;
        }
        planned_ = std::pair{target_.width, target_.height};
    }

    void stop() noexcept override {
        if (devices_ == nullptr) {
            return;
        }
        FramerStatistics statistics;
        if (framer_ != nullptr) {
            if (framer_->finish(kFinishNanoseconds).has_value()) {
                takePixels();
            }
            statistics = framer_->statistics();
            // Before the device, which the Runtime holds past the World.
            framer_.reset();
        }
        bool captured = false;
        if (capture_.has_value() && last_.has_value()) {
#if RAWFRAME_FILE_SYSTEM
            const std::vector<std::byte> kImage = tgaOf(*last_, capturedWidth_, capturedHeight_);
            std::ofstream file{*capture_, std::ios::binary};
            file.write(reinterpret_cast<const char*>(kImage.data()), static_cast<std::streamsize>(kImage.size()));
            captured = static_cast<bool>(file);
#endif
        }
        emitter_.log(diagnostics::Severity::Info,
                     kFrameSummary,
                     "the frames made on the device",
                     {diagnostics::field("window", windows_ != nullptr),
                      diagnostics::field("width", lastWidth_),
                      diagnostics::field("height", lastHeight_),
                      diagnostics::field("frames", statistics.frames),
                      diagnostics::field("framesShown", statistics.framesShown),
                      diagnostics::field("framesNotShown", statistics.framesNotShown),
                      diagnostics::field("framesHidden", framesHidden_),
                      diagnostics::field("framesBusy", framesBusy_ + statistics.framesBusy),
                      diagnostics::field("framesWithoutDevice", framesWithoutDevice_),
                      diagnostics::field("framesIncomplete", framesIncomplete_),
                      diagnostics::field("framesUnchanged", framesUnchanged_),
                      diagnostics::field("framesLimited", framesLimited_),
                      diagnostics::field("readBacks", readBacks_),
                      diagnostics::field("coveredPixels", lastCovered_),
                      diagnostics::field("mostCovered", mostCovered_),
                      diagnostics::field("mostColors", mostColors_),
                      diagnostics::field("colors", lastColors_),
                      diagnostics::field("captured", captured),
                      diagnostics::field("readBacksTooLarge", readBacksTooLarge_)});
    }

    composition::CapabilityObject provide(std::string_view capability) noexcept override {
        if (capability == kFrames.name) {
            return composition::provideAs<Frames>(*this);
        }
        return {};
    }

    Device* device() noexcept override {
        return devices_ != nullptr && !failed_ ? devices_->ready() : nullptr;
    }

    std::optional<std::pair<std::uint32_t, std::uint32_t>> planned() const noexcept override {
        return made_ ? std::nullopt : planned_;
    }

    void join(FrameRecorder& recorder, std::uint32_t order) override {
        leave(recorder);
        joined_.insert(std::ranges::upper_bound(joined_, order, {}, &Joined::order),
                       Joined{.recorder = &recorder, .order = order});
    }

    void leave(FrameRecorder& recorder) noexcept override {
        std::erase_if(joined_, [&](const Joined& joined) {
            return joined.recorder == &recorder;
        });
        ready_.erase(&recorder);
        unchanged_.erase(&recorder);
        // What it drew is gone from the next frame.
        madeAt_.reset();
    }

    void unchanged(FrameRecorder& recorder) noexcept override {
        if (!planned_.has_value() || made_ || failed_) {
            return;
        }
        unchanged_.insert(&recorder);
        ready(recorder);
    }

    void ready(FrameRecorder& recorder) noexcept override {
        if (!planned_.has_value() || made_ || failed_) {
            return;
        }
        ready_.insert(&recorder);
        if (!std::ranges::all_of(joined_, [&](const Joined& joined) {
                return ready_.contains(joined.recorder);
            })) {
            return;
        }
        made_ = true;
        // What the last frame made shows still, and it is not old (D494).
        if (madeAt_.has_value() && now_ - *madeAt_ < kLongestUnchanged && !target_.readBack &&
            target_.width == lastWidth_ && target_.height == lastHeight_ &&
            std::ranges::all_of(
                joined_,
                [&](const Joined& joined) {
                    return unchanged_.contains(joined.recorder);
                })) {
            ++framesUnchanged_;
            return;
        }
        std::vector<FrameRecorder*> recorders;
        for (const Joined& joined : joined_) {
            recorders.push_back(joined.recorder);
        }
        const auto kMade = framer_->make(recorders, target_);
        if (!kMade.has_value()) {
            fail(kMade.error());
            return;
        }
        if (*kMade) {
            ++submitted_;
            madeAt_ = now_;
            submittedAt_ = clock_.now();
            runningAt_.reset();
            lastWidth_ = target_.width;
            lastHeight_ = target_.height;
            if (target_.readBack) {
                readWidth_ = target_.width;
                readHeight_ = target_.height;
            }
        }
    }

private:
    struct Joined {
        FrameRecorder* recorder = nullptr;
        std::uint32_t order = 0;
    };

    static result::Status badConfiguration(std::string_view why) {
        return std::unexpected<result::Error>{result::fail(result::ErrorClass::InvalidArgument,
                                                           composition::kCompositionDomain,
                                                           code(composition::CompositionError::BadConfiguration),
                                                           why)
                                                  .error()};
    }

    FramePace pace() const noexcept override {
        return pace_;
    }

    /// The last frame submitted, found running or done (D533): from its
    /// submission on the clock, not the iteration's start, so what the
    /// iteration did before it is not counted as the device's.
    void watchPace() noexcept {
        if (framer_ == nullptr || pace_.done == submitted_) {
            return;
        }
        // A failure is told where the frame is planned.
        const auto kDone = framer_->done();
        if (!kDone.has_value()) {
            return;
        }
        const execution::MonotonicInstant kNow = clock_.now();
        if (!*kDone) {
            runningAt_ = kNow;
            return;
        }
        pace_.done = submitted_;
        pace_.atLeast = runningAt_.has_value() ? *runningAt_ - submittedAt_ : execution::MonotonicDuration{};
        pace_.atMost = kNow - submittedAt_;
        pace_.budget = framePeriod_.value_or(kSixtieth);
    }

    void takePixels() {
        if (auto pixels = framer_->pixels()) {
            ++readBacks_;
            lastCovered_ = coveredOf(*pixels);
            mostCovered_ = std::max(mostCovered_, lastCovered_);
            lastColors_ = colorsOf(*pixels);
            mostColors_ = std::max(mostColors_, lastColors_);
            if (capture_.has_value()) {
                last_ = std::move(*pixels);
                capturedWidth_ = readWidth_;
                capturedHeight_ = readHeight_;
            }
        }
    }

    /// Reported once; no more frames are made.
    void fail(const result::Error& error) noexcept {
        failed_ = true;
        // The reason, and what it names (a device's outcome, a pipeline).
        std::string detail;
        for (const auto& each : error.context()) {
            detail += (detail.empty() ? "" : ", ") + std::string{each.key} + " " + std::string{each.value};
        }
        emitter_.log(
            diagnostics::Severity::Error,
            kFailed,
            "a frame could not be made: no more are",
            {diagnostics::field("reason", std::string{error.description()}), diagnostics::field("detail", detail)});
    }

    DeviceHolder* devices_ = nullptr;
    window::Surfaces* windows_ = nullptr;
    std::unique_ptr<Framer> framer_;
    /// In order.
    std::vector<Joined> joined_;
    std::unordered_set<FrameRecorder*> ready_;
    /// Those of `ready_` that draw what they drew in the last frame made.
    std::unordered_set<FrameRecorder*> unchanged_;
    execution::MonotonicInstant now_;
    /// When the last frame was submitted; none before one, or once a
    /// recorder left.
    std::optional<execution::MonotonicInstant> madeAt_;
    /// `render.frame_rate`'s period; none for no limit.
    std::optional<execution::MonotonicDuration> framePeriod_;
    std::optional<std::pair<std::uint32_t, std::uint32_t>> planned_;
    FrameTarget target_;
    bool made_ = false;
    bool failed_ = false;
    std::uint32_t width_ = 1280;
    std::uint32_t height_ = 720;
    std::uint64_t readEvery_ = 60;
    std::optional<std::string> capture_;
    /// The last frame read back, kept for the capture.
    std::optional<std::vector<std::byte>> last_;
    std::uint32_t lastWidth_ = 0;
    std::uint32_t lastHeight_ = 0;
    std::uint32_t readWidth_ = 0;
    std::uint32_t readHeight_ = 0;
    std::uint32_t capturedWidth_ = 0;
    std::uint32_t capturedHeight_ = 0;
    std::uint64_t submitted_ = 0;
    /// When the last frame was submitted and last found running, on the
    /// process's clock (D533).
    execution::SteadyClock clock_;
    execution::MonotonicInstant submittedAt_;
    std::optional<execution::MonotonicInstant> runningAt_;
    FramePace pace_;
    std::uint64_t framesHidden_ = 0;
    std::uint64_t framesBusy_ = 0;
    std::uint64_t framesWithoutDevice_ = 0;
    std::uint64_t framesIncomplete_ = 0;
    std::uint64_t framesUnchanged_ = 0;
    std::uint64_t framesLimited_ = 0;
    std::uint64_t readBacks_ = 0;
    std::uint64_t readBacksTooLarge_ = 0;
    std::uint64_t lastCovered_ = 0;
    std::uint64_t mostCovered_ = 0;
    std::uint64_t lastColors_ = 0;
    /// The most any read-back picture had, so a run whose last picture is
    /// a camera against a wall still says what it drew (D433a).
    std::uint64_t mostColors_ = 0;
    diagnostics::Emitter emitter_;
};

result::Result<composition::ParticipantOwner> make(composition::ParticipantContext& context) noexcept {
    auto participant = std::make_unique<FrameParticipant>();
    RAWFRAME_TRY(participant->load(context));
    return composition::ParticipantOwner{participant.release()};
}

} // namespace

void registerFrames(composition::ParticipantRegistrar& registrar) noexcept {
    registrar.submit(composition::ParticipantDeclaration{
        .identity = "rawframe.render.frame",
        .factory = &make,
        .scope = composition::LifetimeScope::World,
        .providedCapabilities = kProvided,
        .optionalCapabilities = kMaybe,
        .eligibility = {.roles = ~kServer},
        .lifecycle = {.stopBudget = execution::MonotonicDuration::fromMilliseconds(110)},
        .observabilityIdentity = "render.frame",
        .budgetOwner = "render",
        .hostPhases = composition::hostPhaseBit(composition::HostPhase::Present),
    });
}

} // namespace rawframe::render
