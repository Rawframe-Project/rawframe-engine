#include "rawframe/xr/session.h"

#include "openxr.h"

#include <algorithm>
#include <utility>

namespace rawframe::xr {

namespace {

/// A view's swapchain and its images, adopted on the device.
struct Chain {
    XrSwapchain swapchain = XR_NULL_HANDLE;
    ViewSize size;
    std::vector<std::uint64_t> images;
};

/// How long acquiring a view's image may wait for the runtime: a second,
/// far past any frame.
constexpr XrDuration kImageWait = 1'000'000'000;

} // namespace

struct Session::State {
    Runtime::State* runtime = nullptr;
    render::Device* device = nullptr;
    XrSession session = XR_NULL_HANDLE;
    XrSpace local = XR_NULL_HANDLE;
    std::vector<Chain> chains;
    std::vector<ViewSize> sizes;
    SessionState state = SessionState::Unknown;
    bool running = false;
    bool over = false;
    SessionStatistics statistics;
    /// The frame begun and not yet ended: its display time, its views as
    /// the runtime located them, and whether their images are acquired.
    bool begun = false;
    bool acquired = false;
    XrTime displayTime = 0;
    std::vector<XrView> located;

    State() = default;
    State(const State&) = delete;
    State& operator=(const State&) = delete;
    ~State() {
        // The images' textures, then the swapchains whose images they
        // were, then the space and the session.
        for (Chain& chain : chains) {
            for (const std::uint64_t kImage : chain.images) {
                device->abandon(kImage);
            }
            if (chain.swapchain != XR_NULL_HANDLE) {
                xrDestroySwapchain(chain.swapchain);
            }
        }
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

    /// A swapchain for each view, in the first format the runtime offers
    /// that the device adopts, its images adopted.
    result::Status makeChains() {
        std::uint32_t count = 0;
        if (const XrResult kCounted = xrEnumerateSwapchainFormats(session, 0, &count, nullptr); XR_FAILED(kCounted)) {
            return failed("the runtime's image formats could not be read", kCounted);
        }
        std::vector<std::int64_t> formats(count);
        if (const XrResult kRead = xrEnumerateSwapchainFormats(session, count, &count, formats.data());
            XR_FAILED(kRead)) {
            return failed("the runtime's image formats could not be read", kRead);
        }
        // The runtime lists its formats in its order of preference.
        const auto kFormat = std::ranges::find_if(formats, [](std::int64_t format) {
            return render::Device::adoptable(static_cast<std::uint32_t>(format));
        });
        if (kFormat == formats.end()) {
            return refused(XrError::Runtime, "the runtime offers no image format the device adopts");
        }
        for (const ViewSize& kView : runtime->description.views) {
            Chain& chain = chains.emplace_back();
            chain.size = {.width = kView.width, .height = kView.height, .samples = 1};
            XrSwapchainCreateInfo create{XR_TYPE_SWAPCHAIN_CREATE_INFO};
            create.usageFlags = XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT;
            create.format = *kFormat;
            create.sampleCount = 1;
            create.width = kView.width;
            create.height = kView.height;
            create.faceCount = 1;
            create.arraySize = 1;
            create.mipCount = 1;
            if (const XrResult kMade = xrCreateSwapchain(session, &create, &chain.swapchain); XR_FAILED(kMade)) {
                chain.swapchain = XR_NULL_HANDLE;
                return failed("a view's swapchain could not be made", kMade);
            }
            std::uint32_t images = 0;
            if (const XrResult kCounted = xrEnumerateSwapchainImages(chain.swapchain, 0, &images, nullptr);
                XR_FAILED(kCounted)) {
                return failed("a view's images could not be read", kCounted);
            }
            std::vector<XrSwapchainImageVulkan2KHR> made(images, {XR_TYPE_SWAPCHAIN_IMAGE_VULKAN2_KHR});
            if (const XrResult kRead = xrEnumerateSwapchainImages(
                    chain.swapchain, images, &images, reinterpret_cast<XrSwapchainImageBaseHeader*>(made.data()));
                XR_FAILED(kRead)) {
                return failed("a view's images could not be read", kRead);
            }
            for (const XrSwapchainImageVulkan2KHR& kImage : made) {
                RAWFRAME_TRY_ASSIGN(
                    const std::uint64_t kAdopted,
                    device->adopt(kImage.image, static_cast<std::uint32_t>(*kFormat), kView.width, kView.height));
                chain.images.push_back(kAdopted);
            }
            sizes.push_back(chain.size);
        }
        return {};
    }

    /// The views at `time`, in the local space.
    result::Result<std::vector<ViewPose>> locate(XrTime time) {
        XrViewLocateInfo info{XR_TYPE_VIEW_LOCATE_INFO};
        info.viewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
        info.displayTime = time;
        info.space = local;
        XrViewState viewState{XR_TYPE_VIEW_STATE};
        located.assign(chains.size(), {XR_TYPE_VIEW});
        std::uint32_t count = 0;
        if (const XrResult kLocated = xrLocateViews(
                session, &info, &viewState, static_cast<std::uint32_t>(located.size()), &count, located.data());
            XR_FAILED(kLocated)) {
            return failed("the views could not be located", kLocated);
        }
        located.resize(count);
        constexpr XrViewStateFlags kValid = XR_VIEW_STATE_POSITION_VALID_BIT | XR_VIEW_STATE_ORIENTATION_VALID_BIT;
        constexpr XrViewStateFlags kTracked =
            XR_VIEW_STATE_POSITION_TRACKED_BIT | XR_VIEW_STATE_ORIENTATION_TRACKED_BIT;
        const bool kAllValid = (viewState.viewStateFlags & kValid) == kValid;
        const bool kAllTracked = kAllValid && (viewState.viewStateFlags & kTracked) == kTracked;
        std::vector<ViewPose> poses;
        for (const XrView& kView : located) {
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

    /// Each view's next image, acquired and waited for.
    result::Result<std::vector<std::uint64_t>> acquire() {
        std::vector<std::uint64_t> images;
        for (const Chain& kChain : chains) {
            std::uint32_t index = 0;
            if (const XrResult kAcquired = xrAcquireSwapchainImage(kChain.swapchain, nullptr, &index);
                XR_FAILED(kAcquired)) {
                return failed("a view's image could not be acquired", kAcquired);
            }
            XrSwapchainImageWaitInfo wait{XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};
            wait.timeout = kImageWait;
            if (const XrResult kWaited = xrWaitSwapchainImage(kChain.swapchain, &wait); XR_FAILED(kWaited)) {
                return failed("a view's image could not be waited for", kWaited);
            }
            images.push_back(kChain.images.at(index));
        }
        return images;
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
    state->device = &device;
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
    RAWFRAME_TRY(state->makeChains());
    return std::unique_ptr<Session>{new Session{std::move(state)}};
}

result::Result<SessionFrame> Session::begin() {
    State& state = *state_;
    if (state.begun) {
        return refused(XrError::State, "a frame is already begun");
    }
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
    state.begun = true;
    state.displayTime = frame.predictedDisplayTime;
    made.begun = true;
    made.shown = frame.shouldRender == XR_TRUE;
    if (made.shown) {
        RAWFRAME_TRY_ASSIGN(made.views, state.locate(frame.predictedDisplayTime));
        RAWFRAME_TRY_ASSIGN(made.images, state.acquire());
        state.acquired = true;
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

result::Status Session::end(bool drawn) {
    State& state = *state_;
    if (!state.begun) {
        return refused(XrError::State, "no frame is begun");
    }
    state.begun = false;
    const bool kAcquired = std::exchange(state.acquired, false);
    if (kAcquired) {
        for (const Chain& kChain : state.chains) {
            XrSwapchainImageReleaseInfo release{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
            if (const XrResult kReleased = xrReleaseSwapchainImage(kChain.swapchain, &release); XR_FAILED(kReleased)) {
                return failed("a view's image could not be given back", kReleased);
            }
        }
    }
    // Each view's image whole, where the runtime located it.
    std::vector<XrCompositionLayerProjectionView> views;
    const bool kSubmitted = kAcquired && drawn && state.located.size() == state.chains.size();
    if (kSubmitted) {
        for (std::size_t at = 0; at < state.chains.size(); ++at) {
            XrCompositionLayerProjectionView& view = views.emplace_back();
            view.type = XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW;
            view.pose = state.located[at].pose;
            view.fov = state.located[at].fov;
            view.subImage.swapchain = state.chains[at].swapchain;
            view.subImage.imageRect.extent = {static_cast<std::int32_t>(state.chains[at].size.width),
                                              static_cast<std::int32_t>(state.chains[at].size.height)};
        }
    }
    XrCompositionLayerProjection projection{XR_TYPE_COMPOSITION_LAYER_PROJECTION};
    projection.space = state.local;
    projection.viewCount = static_cast<std::uint32_t>(views.size());
    projection.views = views.data();
    const auto* kLayer = reinterpret_cast<const XrCompositionLayerBaseHeader*>(&projection);
    XrFrameEndInfo end{XR_TYPE_FRAME_END_INFO};
    end.displayTime = state.displayTime;
    end.environmentBlendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
    end.layerCount = kSubmitted ? 1 : 0;
    end.layers = kSubmitted ? &kLayer : nullptr;
    if (const XrResult kEnded = xrEndFrame(state.session, &end); XR_FAILED(kEnded)) {
        return failed("the frame could not end", kEnded);
    }
    ++state.statistics.framesEnded;
    if (kSubmitted) {
        ++state.statistics.framesSubmitted;
    }
    return {};
}

const std::vector<ViewSize>& Session::images() const noexcept {
    return state_->sizes;
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
