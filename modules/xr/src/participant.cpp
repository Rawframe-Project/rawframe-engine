#include "rawframe/composition/composition.h"
#include "rawframe/composition/configuration.h"
#include "rawframe/render/headset.h"
#include "rawframe/view/headset.h"
#include "rawframe/xr/registrar.h"
#include "rawframe/xr/runtime.h"
#include "rawframe/xr/session.h"

#include <array>
#include <memory>
#include <optional>
#include <string>

namespace rawframe::xr {

namespace {

constexpr diagnostics::EventIdentity kReady{"xr", "xr_ready"};
constexpr diagnostics::EventIdentity kUnavailable{"xr", "xr_unavailable"};
constexpr diagnostics::EventIdentity kState{"xr", "xr_session_state"};
constexpr diagnostics::EventIdentity kFailed{"xr", "xr_failed"};
constexpr diagnostics::EventIdentity kSummary{"xr", "xr_summary"};
constexpr std::string_view kProvided[] = {render::kHeadset.name, view::kHeadsetEyes.name};
constexpr std::uint32_t kServer = composition::only(composition::TargetRole::DedicatedServer);

constexpr std::array<std::string_view, 9> kStateNames = {
    "unknown", "idle", "ready", "synchronized", "visible", "focused", "stopping", "loss_pending", "exiting"};

/// The headset, as the render module draws for it: the runtime's system,
/// opened at load so the device is made by it, and a session made on the
/// device the first time a frame is planned on it. Each Host iteration it
/// begins the session's frame in `presentation_extract`, before anything
/// draws, and tells the eyes the frame shows (D595); the frame is ended
/// once drawn, or undrawn in `frame_end`.
class HeadsetParticipant final : public composition::Participant, public render::Headset {
public:
    result::Status load(composition::ParticipantContext& context) {
        const std::optional<std::string_view> kWanted = context.configuration().text("xr.headset");
        if (!kWanted.has_value() || *kWanted == "false") {
            return {};
        }
        if (*kWanted != "true") {
            return std::unexpected<result::Error>{result::fail(result::ErrorClass::InvalidArgument,
                                                               composition::kCompositionDomain,
                                                               code(composition::CompositionError::BadConfiguration),
                                                               "xr.headset is true or false")
                                                      .error()};
        }
        asked_ = true;
        auto runtime = Runtime::open({});
        if (!runtime.has_value()) {
            // Played flat: the headset is not present.
            unavailable_ = std::string{runtime.error().description()};
            return {};
        }
        runtime_ = std::move(*runtime);
        return {};
    }

    result::Status start(composition::ParticipantContext& context) noexcept override {
        emitter_ = context.emitter();
        if (runtime_ != nullptr) {
            const SystemDescription& kSystem = runtime_->system();
            emitter_.log(diagnostics::Severity::Info,
                         kReady,
                         "a runtime's headset answered: the game is shown on it",
                         {diagnostics::field("system", kSystem.name),
                          diagnostics::field("runtime", kSystem.runtime),
                          diagnostics::field("views", kSystem.views.size()),
                          diagnostics::field("width", kSystem.views.empty() ? 0U : kSystem.views[0].width),
                          diagnostics::field("height", kSystem.views.empty() ? 0U : kSystem.views[0].height)});
        } else if (asked_) {
            emitter_.log(diagnostics::Severity::Warning,
                         kUnavailable,
                         "no runtime's headset answered: the game is played flat",
                         {diagnostics::field("reason", unavailable_)});
        }
        return {};
    }

    void stop() noexcept override {
        close();
        if (runtime_ == nullptr) {
            return;
        }
        emitter_.log(diagnostics::Severity::Info,
                     kSummary,
                     "what the headset showed",
                     {diagnostics::field("sessions", sessions_),
                      diagnostics::field("framesEnded", totals_.framesEnded),
                      diagnostics::field("framesShown", totals_.framesShown),
                      diagnostics::field("framesLocated", totals_.framesLocated),
                      diagnostics::field("framesTracked", totals_.framesTracked),
                      diagnostics::field("framesSubmitted", totals_.framesSubmitted),
                      diagnostics::field("failed", failed_)});
    }

    composition::CapabilityObject provide(std::string_view capability) noexcept override {
        if (capability == render::kHeadset.name) {
            return composition::provideAs<render::Headset>(*this);
        }
        if (capability == view::kHeadsetEyes.name) {
            return composition::provideAs(eyes_);
        }
        return {};
    }

    void runHostPhase(composition::HostPhase phase, const composition::HostFrame& /*frame*/) noexcept override {
        if (phase == composition::HostPhase::FrameEnd) {
            end(false);
            return;
        }
        if (phase == composition::HostPhase::PresentationExtract) {
            begin();
        }
    }

    bool present() const noexcept override {
        return runtime_ != nullptr;
    }

    result::Result<void*> instance(const void* createInfo, void* getInstanceProcAddr) override {
        return runtime_->instance(createInfo, getInstanceProcAddr);
    }

    result::Result<void*> physicalDevice(void* instance) override {
        return runtime_->physicalDevice(instance);
    }

    result::Result<void*> device(void* physicalDevice, const void* createInfo, void* getInstanceProcAddr) override {
        return runtime_->device(physicalDevice, createInfo, getInstanceProcAddr);
    }

    std::optional<Views> views(render::Device& device) noexcept override {
        if (runtime_ == nullptr || failed_ || over_) {
            return std::nullopt;
        }
        if (session_ == nullptr) {
            auto made = Session::create(*runtime_, device);
            if (!made.has_value()) {
                fail(made.error());
                return std::nullopt;
            }
            session_ = std::move(*made);
            ++sessions_;
            return std::nullopt;
        }
        if (!open_ || session_->images().empty()) {
            return std::nullopt;
        }
        return Views{
            .images = images_, .width = session_->images().front().width, .height = session_->images().front().height};
    }

    void end(bool drawn) noexcept override {
        if (session_ == nullptr || !open_) {
            return;
        }
        open_ = false;
        images_.clear();
        if (const result::Status kEnded = session_->end(drawn); !kEnded.has_value()) {
            fail(kEnded.error());
        }
    }

    void close() noexcept override {
        eyes_.tell({});
        if (session_ == nullptr) {
            return;
        }
        end(false);
        const SessionStatistics& kSession = session_->statistics();
        totals_.framesEnded += kSession.framesEnded;
        totals_.framesShown += kSession.framesShown;
        totals_.framesLocated += kSession.framesLocated;
        totals_.framesTracked += kSession.framesTracked;
        totals_.framesSubmitted += kSession.framesSubmitted;
        session_.reset();
        told_ = SessionState::Unknown;
    }

private:
    /// This iteration's frame begun, as the runtime paces it, its views'
    /// images acquired and its eyes told; or none, and no eyes.
    void begin() noexcept {
        eyes_.tell({});
        if (session_ == nullptr || failed_ || over_ || open_) {
            return;
        }
        auto frame = session_->begin();
        told();
        if (!frame.has_value()) {
            fail(frame.error());
            return;
        }
        if (session_->over()) {
            // The runtime ended it: shown no more, the window plays on.
            close();
            over_ = true;
            return;
        }
        if (!frame->begun) {
            return;
        }
        if (!frame->shown || frame->views.size() != session_->images().size()) {
            if (const result::Status kEnded = session_->end(false); !kEnded.has_value()) {
                fail(kEnded.error());
            }
            return;
        }
        open_ = true;
        images_ = std::move(frame->images);
        std::vector<view::HeadsetEye> eyes;
        for (std::size_t at = 0; at < frame->views.size(); ++at) {
            const ViewPose& kPose = frame->views[at];
            eyes.push_back({.position = kPose.position,
                            .orientation = kPose.orientation,
                            .angleLeft = kPose.angleLeft,
                            .angleRight = kPose.angleRight,
                            .angleUp = kPose.angleUp,
                            .angleDown = kPose.angleDown,
                            .width = session_->images()[at].width,
                            .height = session_->images()[at].height});
        }
        eyes_.tell(eyes);
    }

    /// The session's state, said when it changed.
    void told() noexcept {
        if (session_ == nullptr || session_->state() == told_) {
            return;
        }
        told_ = session_->state();
        emitter_.log(diagnostics::Severity::Info,
                     kState,
                     "the headset's session changed state",
                     {diagnostics::field("state", std::string{kStateNames.at(static_cast<std::size_t>(told_))})});
    }

    /// The session failed: said once, and shown no more.
    void fail(const result::Error& error) noexcept {
        close();
        failed_ = true;
        emitter_.log(diagnostics::Severity::Error,
                     kFailed,
                     "the headset's session failed: it shows nothing more",
                     {diagnostics::field("reason", std::string{error.description()})});
    }

    view::HeadsetEyes eyes_;
    /// This iteration's frame begun and not yet ended, and its images.
    bool open_ = false;
    std::vector<std::uint64_t> images_;
    bool asked_ = false;
    std::string unavailable_;
    std::unique_ptr<Runtime> runtime_;
    std::unique_ptr<Session> session_;
    SessionState told_ = SessionState::Unknown;
    SessionStatistics totals_;
    std::uint64_t sessions_ = 0;
    bool failed_ = false;
    bool over_ = false;
    diagnostics::Emitter emitter_;
};

result::Result<composition::ParticipantOwner> make(composition::ParticipantContext& context) noexcept {
    auto participant = std::make_unique<HeadsetParticipant>();
    RAWFRAME_TRY(participant->load(context));
    return composition::ParticipantOwner{participant.release()};
}

} // namespace

void registerParticipants(composition::ParticipantRegistrar& registrar) noexcept {
    registrar.submit(composition::ParticipantDeclaration{
        .identity = "rawframe.xr.headset",
        .factory = &make,
        .scope = composition::LifetimeScope::Runtime,
        .providedCapabilities = kProvided,
        .eligibility = {.roles = ~kServer},
        .lifecycle = {.stopBudget = execution::MonotonicDuration::fromMilliseconds(10)},
        .observabilityIdentity = "xr.headset",
        .budgetOwner = "xr",
        .hostPhases =
            static_cast<std::uint16_t>(composition::hostPhaseBit(composition::HostPhase::PresentationExtract) |
                                       composition::hostPhaseBit(composition::HostPhase::FrameEnd)),
    });
}

} // namespace rawframe::xr
