#include "rawframe/xr/session.h"

#include "openxr.h"

#include <algorithm>
#include <utility>

namespace rawframe::xr {

struct Session::State {
    Runtime::State* runtime = nullptr;
    XrSession session = XR_NULL_HANDLE;
    XrSpace local = XR_NULL_HANDLE;
    SessionState state = SessionState::Unknown;
    bool running = false;
    bool over = false;
    SessionStatistics statistics;

    State() = default;
    State(const State&) = delete;
    State& operator=(const State&) = delete;
    ~State() {
        if (local != XR_NULL_HANDLE) {
            xrDestroySpace(local);
        }
        if (session != XR_NULL_HANDLE) {
            xrDestroySession(session);
        }
    }

    /// The runtime's events since the last frame: the session begun when
    /// ready, ended when stopping, and over once it exits or is being
    /// lost; the instance's loss is the session's.
    result::Status take() {
        XrEventDataBuffer event{XR_TYPE_EVENT_DATA_BUFFER};
        XrResult polled = XR_SUCCESS;
        while ((polled = xrPollEvent(runtime->instance, &event)) == XR_SUCCESS) {
            if (event.type == XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED) {
                const auto& kChanged = reinterpret_cast<const XrEventDataSessionStateChanged&>(event);
                RAWFRAME_TRY(became(kChanged.state));
            } else if (event.type == XR_TYPE_EVENT_DATA_INSTANCE_LOSS_PENDING) {
                over = true;
            }
            event = {XR_TYPE_EVENT_DATA_BUFFER};
        }
        if (XR_FAILED(polled)) {
            over = true;
            return failed("the runtime's events could not be read", polled);
        }
        return {};
    }

    result::Status became(XrSessionState next) {
        state = static_cast<SessionState>(std::clamp<int>(next, XR_SESSION_STATE_UNKNOWN, XR_SESSION_STATE_EXITING));
        if (next == XR_SESSION_STATE_READY && !running) {
            XrSessionBeginInfo begin{XR_TYPE_SESSION_BEGIN_INFO};
            begin.primaryViewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
            if (const XrResult kBegun = xrBeginSession(session, &begin); XR_FAILED(kBegun)) {
                return failed("the session could not begin", kBegun);
            }
            running = true;
        } else if (next == XR_SESSION_STATE_STOPPING && running) {
            running = false;
            if (const XrResult kEnded = xrEndSession(session); XR_FAILED(kEnded)) {
                return failed("the session could not end", kEnded);
            }
        } else if (next == XR_SESSION_STATE_EXITING || next == XR_SESSION_STATE_LOSS_PENDING) {
            over = true;
        }
        return {};
    }

    /// The views at `time`, in the local space.
    result::Result<std::vector<ViewPose>> located(XrTime time) {
        XrViewLocateInfo locate{XR_TYPE_VIEW_LOCATE_INFO};
        locate.viewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
        locate.displayTime = time;
        locate.space = local;
        XrViewState viewState{XR_TYPE_VIEW_STATE};
        std::vector<XrView> views(runtime->description.views.size(), {XR_TYPE_VIEW});
        std::uint32_t count = 0;
        if (const XrResult kLocated = xrLocateViews(
                session, &locate, &viewState, static_cast<std::uint32_t>(views.size()), &count, views.data());
            XR_FAILED(kLocated)) {
            return failed("the views could not be located", kLocated);
        }
        constexpr XrViewStateFlags kValid = XR_VIEW_STATE_POSITION_VALID_BIT | XR_VIEW_STATE_ORIENTATION_VALID_BIT;
        constexpr XrViewStateFlags kTracked =
            XR_VIEW_STATE_POSITION_TRACKED_BIT | XR_VIEW_STATE_ORIENTATION_TRACKED_BIT;
        const bool kAllValid = (viewState.viewStateFlags & kValid) == kValid;
        const bool kAllTracked = kAllValid && (viewState.viewStateFlags & kTracked) == kTracked;
        std::vector<ViewPose> poses;
        for (std::uint32_t at = 0; at < count; ++at) {
            const XrView& kView = views[at];
            poses.push_back({.position = {kView.pose.position.x, kView.pose.position.y, kView.pose.position.z},
                             .orientation = {kView.pose.orientation.x,
                                             kView.pose.orientation.y,
                                             kView.pose.orientation.z,
                                             kView.pose.orientation.w},
                             .angleLeft = kView.fov.angleLeft,
                             .angleRight = kView.fov.angleRight,
                             .angleUp = kView.fov.angleUp,
                             .angleDown = kView.fov.angleDown,
                             .located = kAllValid,
                             .tracked = kAllTracked});
        }
        return poses;
    }
};

Session::Session(std::unique_ptr<State> state) noexcept : state_(std::move(state)) {
}

Session::~Session() = default;

result::Result<std::unique_ptr<Session>> Session::create(Runtime& runtime, render::Device& device) {
    const std::optional<render::VulkanObjects> kVulkan = device.vulkan();
    if (!kVulkan.has_value()) {
        return refused(XrError::State, "the device is not ready on Vulkan");
    }
    auto state = std::make_unique<State>();
    state->runtime = runtime.state_.get();
    XrGraphicsBindingVulkan2KHR binding{XR_TYPE_GRAPHICS_BINDING_VULKAN2_KHR};
    binding.instance = static_cast<VkInstance>(kVulkan->instance);
    binding.physicalDevice = static_cast<VkPhysicalDevice>(kVulkan->physicalDevice);
    binding.device = static_cast<VkDevice>(kVulkan->device);
    binding.queueFamilyIndex = kVulkan->queueFamily;
    binding.queueIndex = kVulkan->queueIndex;
    XrSessionCreateInfo create{XR_TYPE_SESSION_CREATE_INFO};
    create.next = &binding;
    create.systemId = state->runtime->system;
    if (const XrResult kMade = xrCreateSession(state->runtime->instance, &create, &state->session); XR_FAILED(kMade)) {
        state->session = XR_NULL_HANDLE;
        return failed("the runtime refused a session", kMade);
    }
    XrReferenceSpaceCreateInfo space{XR_TYPE_REFERENCE_SPACE_CREATE_INFO};
    space.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_LOCAL;
    space.poseInReferenceSpace.orientation.w = 1;
    if (const XrResult kMade = xrCreateReferenceSpace(state->session, &space, &state->local); XR_FAILED(kMade)) {
        state->local = XR_NULL_HANDLE;
        return failed("the session's local space could not be made", kMade);
    }
    return std::unique_ptr<Session>{new Session{std::move(state)}};
}

result::Result<SessionFrame> Session::frame() {
    State& state = *state_;
    RAWFRAME_TRY(state.take());
    SessionFrame made;
    if (!state.running || state.over) {
        return made;
    }
    XrFrameState frame{XR_TYPE_FRAME_STATE};
    if (const XrResult kWaited = xrWaitFrame(state.session, nullptr, &frame); XR_FAILED(kWaited)) {
        return failed("the frame could not be waited for", kWaited);
    }
    if (const XrResult kBegun = xrBeginFrame(state.session, nullptr); XR_FAILED(kBegun)) {
        return failed("the frame could not begin", kBegun);
    }
    made.shown = frame.shouldRender == XR_TRUE;
    if (made.shown) {
        RAWFRAME_TRY_ASSIGN(made.views, state.located(frame.predictedDisplayTime));
    }
    XrFrameEndInfo end{XR_TYPE_FRAME_END_INFO};
    end.displayTime = frame.predictedDisplayTime;
    end.environmentBlendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
    if (const XrResult kEnded = xrEndFrame(state.session, &end); XR_FAILED(kEnded)) {
        return failed("the frame could not end", kEnded);
    }
    made.ended = true;
    ++state.statistics.framesEnded;
    if (made.shown) {
        ++state.statistics.framesShown;
        if (!made.views.empty() && std::ranges::all_of(made.views, &ViewPose::located)) {
            ++state.statistics.framesLocated;
        }
        if (!made.views.empty() && std::ranges::all_of(made.views, &ViewPose::tracked)) {
            ++state.statistics.framesTracked;
        }
    }
    return made;
}

result::Status Session::requestExit() {
    if (!state_->running) {
        return refused(XrError::State, "the session is not running");
    }
    if (const XrResult kAsked = xrRequestExitSession(state_->session); XR_FAILED(kAsked)) {
        return failed("the session's exit could not be asked for", kAsked);
    }
    return {};
}

SessionState Session::state() const noexcept {
    return state_->state;
}

bool Session::over() const noexcept {
    return state_->over;
}

const SessionStatistics& Session::statistics() const noexcept {
    return state_->statistics;
}

} // namespace rawframe::xr
