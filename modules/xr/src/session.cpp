#include "rawframe/xr/session.h"

#include "openxr.h"

#include <algorithm>
#include <string>
#include <tuple>
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

constexpr std::array<std::string_view, 2> kHandPaths = {"/user/hand/left", "/user/hand/right"};

/// `name` into one of OpenXR's fixed name fields, which stay terminated.
template <std::size_t N> void named(char (&into)[N], std::string_view name) {
    std::ranges::copy(name.substr(0, N - 1), into);
}

/// Where `space` is in `base` at `time`.
SpacePose where(XrSpace space, XrSpace base, XrTime time, XrResult& outcome) {
    XrSpaceLocation location{XR_TYPE_SPACE_LOCATION};
    outcome = xrLocateSpace(space, base, time, &location);
    constexpr XrSpaceLocationFlags kValid =
        XR_SPACE_LOCATION_POSITION_VALID_BIT | XR_SPACE_LOCATION_ORIENTATION_VALID_BIT;
    constexpr XrSpaceLocationFlags kTracked =
        XR_SPACE_LOCATION_POSITION_TRACKED_BIT | XR_SPACE_LOCATION_ORIENTATION_TRACKED_BIT;
    const bool kLocated = XR_SUCCEEDED(outcome) && (location.locationFlags & kValid) == kValid;
    if (!kLocated) {
        return {};
    }
    return {.position = {location.pose.position.x, location.pose.position.y, location.pose.position.z},
            .orientation = {location.pose.orientation.x,
                            location.pose.orientation.y,
                            location.pose.orientation.z,
                            location.pose.orientation.w},
            .located = true,
            .tracked = (location.locationFlags & kTracked) == kTracked};
}

} // namespace

struct Session::State {
    Runtime::State* runtime = nullptr;
    render::Device* device = nullptr;
    XrSession session = XR_NULL_HANDLE;
    XrSpace local = XR_NULL_HANDLE;
    std::vector<Chain> chains;
    std::vector<ViewSize> sizes;
    /// The panel's swapchain, where the session has one (D597), and where
    /// its quad is.
    std::optional<Chain> panel;
    PanelSettings panelSettings;
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
    /// The hands' actions (D596): their set, the hands' paths, and each
    /// hand's grip and aim spaces.
    XrActionSet actions = XR_NULL_HANDLE;
    XrAction select = XR_NULL_HANDLE;
    XrAction menu = XR_NULL_HANDLE;
    XrAction grip = XR_NULL_HANDLE;
    XrAction aim = XR_NULL_HANDLE;
    std::array<XrPath, 2> hands{};
    std::array<std::array<XrSpace, 2>, 2> spaces{};

    State() = default;
    State(const State&) = delete;
    State& operator=(const State&) = delete;
    ~State() {
        // The images' textures, then the swapchains whose images they
        // were, then the space and the session.
        if (panel.has_value()) {
            chains.push_back(std::move(*panel));
        }
        for (Chain& chain : chains) {
            for (const std::uint64_t kImage : chain.images) {
                device->abandon(kImage);
            }
            if (chain.swapchain != XR_NULL_HANDLE) {
                xrDestroySwapchain(chain.swapchain);
            }
        }
        for (const auto& kHand : spaces) {
            for (const XrSpace kSpace : kHand) {
                if (kSpace != XR_NULL_HANDLE) {
                    xrDestroySpace(kSpace);
                }
            }
        }
        if (local != XR_NULL_HANDLE) {
            xrDestroySpace(local);
        }
        if (session != XR_NULL_HANDLE) {
            xrDestroySession(session);
        }
        // The set's actions go with it.
        if (actions != XR_NULL_HANDLE) {
            xrDestroyActionSet(actions);
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

    /// A swapchain for each view, and the panel's, in the first format the
    /// runtime offers that the device adopts, their images adopted.
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
            RAWFRAME_TRY(makeChain(chain, *kFormat, kView.width, kView.height));
            sizes.push_back(chain.size);
        }
        if (panelSettings.width != 0 && panelSettings.height != 0) {
            panel.emplace();
            RAWFRAME_TRY(makeChain(*panel, *kFormat, panelSettings.width, panelSettings.height));
        }
        return {};
    }

    /// `chain`'s swapchain, of `width` by `height` in `format`, its images
    /// adopted.
    result::Status makeChain(Chain& chain, std::int64_t format, std::uint32_t width, std::uint32_t height) {
        chain.size = {.width = width, .height = height, .samples = 1};
        XrSwapchainCreateInfo create{XR_TYPE_SWAPCHAIN_CREATE_INFO};
        create.usageFlags = XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT;
        create.format = format;
        create.sampleCount = 1;
        create.width = width;
        create.height = height;
        create.faceCount = 1;
        create.arraySize = 1;
        create.mipCount = 1;
        if (const XrResult kMade = xrCreateSwapchain(session, &create, &chain.swapchain); XR_FAILED(kMade)) {
            chain.swapchain = XR_NULL_HANDLE;
            return failed("a swapchain could not be made", kMade);
        }
        std::uint32_t images = 0;
        if (const XrResult kCounted = xrEnumerateSwapchainImages(chain.swapchain, 0, &images, nullptr);
            XR_FAILED(kCounted)) {
            return failed("a swapchain's images could not be read", kCounted);
        }
        std::vector<XrSwapchainImageVulkan2KHR> made(images, {XR_TYPE_SWAPCHAIN_IMAGE_VULKAN2_KHR});
        if (const XrResult kRead = xrEnumerateSwapchainImages(
                chain.swapchain, images, &images, reinterpret_cast<XrSwapchainImageBaseHeader*>(made.data()));
            XR_FAILED(kRead)) {
            return failed("a swapchain's images could not be read", kRead);
        }
        for (const XrSwapchainImageVulkan2KHR& kImage : made) {
            RAWFRAME_TRY_ASSIGN(const std::uint64_t kAdopted,
                                device->adopt(kImage.image, static_cast<std::uint32_t>(format), width, height));
            chain.images.push_back(kAdopted);
        }
        return {};
    }

    /// The hands' actions, suggested for the simple controller, their
    /// spaces made, and the set attached to the session.
    result::Status makeActions() {
        XrActionSetCreateInfo set{XR_TYPE_ACTION_SET_CREATE_INFO};
        named(set.actionSetName, "rawframe");
        named(set.localizedActionSetName, "Rawframe");
        if (const XrResult kMade = xrCreateActionSet(runtime->instance, &set, &actions); XR_FAILED(kMade)) {
            actions = XR_NULL_HANDLE;
            return failed("the hands' action set could not be made", kMade);
        }
        for (std::size_t hand = 0; hand < hands.size(); ++hand) {
            if (const XrResult kNamed = xrStringToPath(runtime->instance, kHandPaths[hand].data(), &hands[hand]);
                XR_FAILED(kNamed)) {
                return failed("a hand's path could not be named", kNamed);
            }
        }
        for (const auto& [kAction, kName, kType] : {std::tuple{&select, "select", XR_ACTION_TYPE_BOOLEAN_INPUT},
                                                    std::tuple{&menu, "menu", XR_ACTION_TYPE_BOOLEAN_INPUT},
                                                    std::tuple{&grip, "grip", XR_ACTION_TYPE_POSE_INPUT},
                                                    std::tuple{&aim, "aim", XR_ACTION_TYPE_POSE_INPUT}}) {
            XrActionCreateInfo create{XR_TYPE_ACTION_CREATE_INFO};
            named(create.actionName, kName);
            named(create.localizedActionName, kName);
            create.actionType = kType;
            create.countSubactionPaths = static_cast<std::uint32_t>(hands.size());
            create.subactionPaths = hands.data();
            if (const XrResult kMade = xrCreateAction(actions, &create, kAction); XR_FAILED(kMade)) {
                return failed("a hand's action could not be made", kMade);
            }
        }
        std::vector<XrActionSuggestedBinding> bindings;
        for (const std::string_view kHand : kHandPaths) {
            for (const auto& [kAction, kInput] : {std::pair{select, "/input/select/click"},
                                                  std::pair{menu, "/input/menu/click"},
                                                  std::pair{grip, "/input/grip/pose"},
                                                  std::pair{aim, "/input/aim/pose"}}) {
                XrActionSuggestedBinding& binding =
                    bindings.emplace_back(XrActionSuggestedBinding{kAction, XR_NULL_PATH});
                const std::string kPath = std::string{kHand} + kInput;
                if (const XrResult kNamed = xrStringToPath(runtime->instance, kPath.c_str(), &binding.binding);
                    XR_FAILED(kNamed)) {
                    return failed("a controller's input could not be named", kNamed);
                }
            }
        }
        XrInteractionProfileSuggestedBinding suggested{XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING};
        if (const XrResult kNamed = xrStringToPath(
                runtime->instance, "/interaction_profiles/khr/simple_controller", &suggested.interactionProfile);
            XR_FAILED(kNamed)) {
            return failed("the simple controller could not be named", kNamed);
        }
        suggested.countSuggestedBindings = static_cast<std::uint32_t>(bindings.size());
        suggested.suggestedBindings = bindings.data();
        if (const XrResult kSuggested = xrSuggestInteractionProfileBindings(runtime->instance, &suggested);
            XR_FAILED(kSuggested)) {
            return failed("the simple controller's bindings were refused", kSuggested);
        }
        for (std::size_t hand = 0; hand < hands.size(); ++hand) {
            for (const auto& [kAt, kAction] : {std::pair{0, grip}, std::pair{1, aim}}) {
                XrActionSpaceCreateInfo space{XR_TYPE_ACTION_SPACE_CREATE_INFO};
                space.action = kAction;
                space.subactionPath = hands[hand];
                space.poseInActionSpace.orientation.w = 1;
                XrSpace& made = spaces[hand][static_cast<std::size_t>(kAt)];
                if (const XrResult kMade = xrCreateActionSpace(session, &space, &made); XR_FAILED(kMade)) {
                    made = XR_NULL_HANDLE;
                    return failed("a hand's space could not be made", kMade);
                }
            }
        }
        XrSessionActionSetsAttachInfo attach{XR_TYPE_SESSION_ACTION_SETS_ATTACH_INFO};
        attach.countActionSets = 1;
        attach.actionSets = &actions;
        if (const XrResult kAttached = xrAttachSessionActionSets(session, &attach); XR_FAILED(kAttached)) {
            return failed("the hands' actions could not be attached", kAttached);
        }
        return {};
    }

    /// Both hands at `time`, their actions synchronized: neither active
    /// unless the session has the input focus.
    result::Result<std::array<Hand, 2>> readHands(XrTime time) {
        std::array<Hand, 2> read;
        const XrActiveActionSet kActive{actions, XR_NULL_PATH};
        XrActionsSyncInfo sync{XR_TYPE_ACTIONS_SYNC_INFO};
        sync.countActiveActionSets = 1;
        sync.activeActionSets = &kActive;
        const XrResult kSynced = xrSyncActions(session, &sync);
        if (XR_FAILED(kSynced)) {
            return failed("the hands' actions could not be read", kSynced);
        }
        if (kSynced == XR_SESSION_NOT_FOCUSED) {
            return read;
        }
        for (std::size_t at = 0; at < read.size(); ++at) {
            Hand& hand = read[at];
            XrActionStateGetInfo get{XR_TYPE_ACTION_STATE_GET_INFO};
            get.subactionPath = hands[at];
            get.action = grip;
            XrActionStatePose pose{XR_TYPE_ACTION_STATE_POSE};
            if (const XrResult kRead = xrGetActionStatePose(session, &get, &pose); XR_FAILED(kRead)) {
                return failed("a hand's pose could not be read", kRead);
            }
            hand.active = pose.isActive == XR_TRUE;
            for (const auto& [kAction, kInto] : {std::pair{select, &hand.select}, std::pair{menu, &hand.menu}}) {
                get.action = kAction;
                XrActionStateBoolean button{XR_TYPE_ACTION_STATE_BOOLEAN};
                if (const XrResult kRead = xrGetActionStateBoolean(session, &get, &button); XR_FAILED(kRead)) {
                    return failed("a hand's button could not be read", kRead);
                }
                *kInto = button.isActive == XR_TRUE && button.currentState == XR_TRUE;
            }
            XrResult outcome = XR_SUCCESS;
            hand.grip = where(spaces[at][0], local, time, outcome);
            if (XR_SUCCEEDED(outcome)) {
                hand.aim = where(spaces[at][1], local, time, outcome);
            }
            if (XR_FAILED(outcome)) {
                return failed("a hand could not be located", outcome);
            }
        }
        return read;
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

    /// Each view's next image, then the panel's, acquired and waited for.
    result::Result<std::vector<std::uint64_t>> acquire() {
        std::vector<std::uint64_t> images;
        for (const Chain& kChain : chains) {
            RAWFRAME_TRY_ASSIGN(const std::uint64_t kImage, acquire(kChain));
            images.push_back(kImage);
        }
        if (panel.has_value()) {
            RAWFRAME_TRY_ASSIGN(const std::uint64_t kImage, acquire(*panel));
            images.push_back(kImage);
        }
        return images;
    }

    static result::Result<std::uint64_t> acquire(const Chain& chain) {
        std::uint32_t index = 0;
        if (const XrResult kAcquired = xrAcquireSwapchainImage(chain.swapchain, nullptr, &index);
            XR_FAILED(kAcquired)) {
            return failed("a swapchain's image could not be acquired", kAcquired);
        }
        XrSwapchainImageWaitInfo wait{XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};
        wait.timeout = kImageWait;
        if (const XrResult kWaited = xrWaitSwapchainImage(chain.swapchain, &wait); XR_FAILED(kWaited)) {
            return failed("a swapchain's image could not be waited for", kWaited);
        }
        return chain.images.at(index);
    }

    /// Every image acquired, given back.
    result::Status release() {
        for (const Chain& kChain : chains) {
            RAWFRAME_TRY(release(kChain));
        }
        if (panel.has_value()) {
            RAWFRAME_TRY(release(*panel));
        }
        return {};
    }

    static result::Status release(const Chain& chain) {
        XrSwapchainImageReleaseInfo info{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
        if (const XrResult kReleased = xrReleaseSwapchainImage(chain.swapchain, &info); XR_FAILED(kReleased)) {
            return failed("a swapchain's image could not be given back", kReleased);
        }
        return {};
    }
};

std::optional<std::array<float, 2>> pointOnPanel(const SpacePose& aim, const PanelSettings& panel) noexcept {
    if (!aim.located || panel.width == 0 || panel.height == 0 || panel.meters <= 0) {
        return std::nullopt;
    }
    // The aim's -Z, turned by its quaternion.
    const auto [kX, kY, kZ, kW] = aim.orientation;
    const std::array<float, 3> kForward = {
        -2 * (kX * kZ + kW * kY), -2 * (kY * kZ - kW * kX), -(1 - 2 * (kX * kX + kY * kY))};
    // The panel's plane, `distance` ahead, faces +Z.
    const float kAhead = -panel.distance - aim.position[2];
    if (kForward[2] >= 0 || kAhead >= 0) {
        return std::nullopt;
    }
    const float kAlong = kAhead / kForward[2];
    const float kWide = panel.meters;
    const float kTall = panel.meters * static_cast<float>(panel.height) / static_cast<float>(panel.width);
    const float kU = (aim.position[0] + kAlong * kForward[0] + kWide / 2) / kWide;
    const float kV = (kTall / 2 - (aim.position[1] + kAlong * kForward[1])) / kTall;
    if (kU < 0 || kU >= 1 || kV < 0 || kV >= 1) {
        return std::nullopt;
    }
    return std::array<float, 2>{kU * static_cast<float>(panel.width), kV * static_cast<float>(panel.height)};
}

Session::Session(std::unique_ptr<State> state) noexcept : state_(std::move(state)) {
}

Session::~Session() = default;

result::Result<std::unique_ptr<Session>>
Session::create(Runtime& runtime, render::Device& device, const PanelSettings& panel) {
    const std::optional<render::VulkanObjects> kVulkan = device.vulkan();
    if (!kVulkan.has_value()) {
        return refused(XrError::State, "the device is not ready on Vulkan");
    }
    auto state = std::make_unique<State>();
    state->runtime = runtime.state_.get();
    state->device = &device;
    state->panelSettings = panel;
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
    RAWFRAME_TRY(state->makeActions());
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
    if (state.state == SessionState::Focused) {
        RAWFRAME_TRY_ASSIGN(made.hands, state.readHands(frame.predictedDisplayTime));
        ++state.statistics.framesHandsRead;
        if (std::ranges::any_of(made.hands, [](const Hand& hand) {
                return hand.grip.located;
            })) {
            ++state.statistics.framesHandLocated;
        }
    }
    if (made.shown) {
        RAWFRAME_TRY_ASSIGN(made.views, state.locate(frame.predictedDisplayTime));
        RAWFRAME_TRY_ASSIGN(made.images, state.acquire());
        state.acquired = true;
        if (state.panel.has_value()) {
            made.panel = made.images.back();
            made.images.pop_back();
        }
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
        RAWFRAME_TRY(state.release());
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
    // The panel over the world, upright before where the head began, its
    // clear parts showing what is behind (D597).
    XrCompositionLayerQuad quad{XR_TYPE_COMPOSITION_LAYER_QUAD};
    quad.layerFlags = XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT;
    quad.space = state.local;
    quad.eyeVisibility = XR_EYE_VISIBILITY_BOTH;
    std::vector<const XrCompositionLayerBaseHeader*> layers;
    if (kSubmitted) {
        layers.push_back(reinterpret_cast<const XrCompositionLayerBaseHeader*>(&projection));
        if (state.panel.has_value()) {
            const PanelSettings& kPanel = state.panelSettings;
            quad.subImage.swapchain = state.panel->swapchain;
            quad.subImage.imageRect.extent = {static_cast<std::int32_t>(kPanel.width),
                                              static_cast<std::int32_t>(kPanel.height)};
            quad.pose.orientation.w = 1;
            quad.pose.position.z = -kPanel.distance;
            quad.size = {kPanel.meters,
                         kPanel.meters * static_cast<float>(kPanel.height) / static_cast<float>(kPanel.width)};
            layers.push_back(reinterpret_cast<const XrCompositionLayerBaseHeader*>(&quad));
        }
    }
    XrFrameEndInfo end{XR_TYPE_FRAME_END_INFO};
    end.displayTime = state.displayTime;
    end.environmentBlendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
    end.layerCount = static_cast<std::uint32_t>(layers.size());
    end.layers = layers.empty() ? nullptr : layers.data();
    if (const XrResult kEnded = xrEndFrame(state.session, &end); XR_FAILED(kEnded)) {
        return failed("the frame could not end", kEnded);
    }
    ++state.statistics.framesEnded;
    if (kSubmitted) {
        ++state.statistics.framesSubmitted;
        state.statistics.framesPanelSubmitted += layers.size() > 1 ? 1U : 0U;
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

std::optional<ViewSize> Session::panel() const noexcept {
    return state_->panel.has_value() ? std::optional{state_->panel->size} : std::nullopt;
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
