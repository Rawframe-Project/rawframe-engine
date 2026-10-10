#include "openxr.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <string>
#include <vector>

namespace rawframe::xr {

std::unexpected<result::Error> failed(std::string_view why, XrResult outcome) {
    const bool kAbsent = outcome == XR_ERROR_RUNTIME_UNAVAILABLE || outcome == XR_ERROR_FORM_FACTOR_UNAVAILABLE ||
                         outcome == XR_ERROR_INSTANCE_LOST;
    return std::unexpected<result::Error>{
        result::fail(kAbsent ? result::ErrorClass::Unavailable : result::ErrorClass::Internal,
                     kXrDomain,
                     code(kAbsent ? XrError::NoRuntime : XrError::Runtime),
                     why)
            .error()
            .withContext("outcome", std::to_string(static_cast<std::int32_t>(outcome)))};
}

std::unexpected<result::Error> refused(XrError error, std::string_view why) {
    return std::unexpected<result::Error>{
        result::fail(error == XrError::NoRuntime ? result::ErrorClass::Unavailable : result::ErrorClass::Internal,
                     kXrDomain,
                     code(error),
                     why)
            .error()};
}

namespace {

/// Whether the runtime offers `name`; failing to ask is the runtime's
/// absence, as the loader finds none.
result::Result<bool> offers(const char* name) {
    std::uint32_t count = 0;
    if (const XrResult kAsked = xrEnumerateInstanceExtensionProperties(nullptr, 0, &count, nullptr);
        XR_FAILED(kAsked)) {
        return failed("no OpenXR runtime answered", kAsked);
    }
    std::vector<XrExtensionProperties> extensions(count, {XR_TYPE_EXTENSION_PROPERTIES});
    if (const XrResult kRead = xrEnumerateInstanceExtensionProperties(nullptr, count, &count, extensions.data());
        XR_FAILED(kRead)) {
        return failed("the runtime's extensions could not be read", kRead);
    }
    return std::ranges::any_of(extensions, [name](const XrExtensionProperties& each) {
        return std::strcmp(each.extensionName, name) == 0;
    });
}

template <typename Function> result::Status function(XrInstance instance, const char* name, Function& out) {
    PFN_xrVoidFunction found = nullptr;
    if (const XrResult kRead = xrGetInstanceProcAddr(instance, name, &found); XR_FAILED(kRead)) {
        return failed("a function of the runtime's Vulkan binding could not be read", kRead);
    }
    out = reinterpret_cast<Function>(found);
    return {};
}

} // namespace

Runtime::Runtime(std::unique_ptr<State> state) noexcept : state_(std::move(state)) {
}

Runtime::~Runtime() = default;

result::Result<std::unique_ptr<Runtime>> Runtime::open(const RuntimeSettings& settings) {
    RAWFRAME_TRY_ASSIGN(const bool kVulkan, offers(XR_KHR_VULKAN_ENABLE2_EXTENSION_NAME));
    if (!kVulkan) {
        return refused(XrError::Runtime, "the runtime cannot draw through Vulkan");
    }
    auto state = std::make_unique<State>();
    XrInstanceCreateInfo create{XR_TYPE_INSTANCE_CREATE_INFO};
    const std::size_t kName = std::min(settings.application.size(), std::size_t{XR_MAX_APPLICATION_NAME_SIZE - 1});
    std::memcpy(create.applicationInfo.applicationName, settings.application.data(), kName);
    std::memcpy(create.applicationInfo.engineName, "Rawframe", sizeof("Rawframe"));
    // OpenXR 1.0, which every runtime takes, 1.1's included.
    create.applicationInfo.apiVersion = XR_API_VERSION_1_0;
    const std::array<const char*, 1> kExtensions = {XR_KHR_VULKAN_ENABLE2_EXTENSION_NAME};
    create.enabledExtensionCount = kExtensions.size();
    create.enabledExtensionNames = kExtensions.data();
    if (const XrResult kMade = xrCreateInstance(&create, &state->instance); XR_FAILED(kMade)) {
        state->instance = XR_NULL_HANDLE;
        return failed("the runtime refused an instance", kMade);
    }
    XrInstanceProperties instance{XR_TYPE_INSTANCE_PROPERTIES};
    if (XR_SUCCEEDED(xrGetInstanceProperties(state->instance, &instance))) {
        state->description.runtime = std::string{instance.runtimeName} + " " +
                                     std::to_string(XR_VERSION_MAJOR(instance.runtimeVersion)) + "." +
                                     std::to_string(XR_VERSION_MINOR(instance.runtimeVersion)) + "." +
                                     std::to_string(XR_VERSION_PATCH(instance.runtimeVersion));
    }
    XrSystemGetInfo system{XR_TYPE_SYSTEM_GET_INFO};
    system.formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;
    if (const XrResult kFound = xrGetSystem(state->instance, &system, &state->system); XR_FAILED(kFound)) {
        return failed("the runtime has no head-mounted system", kFound);
    }
    XrSystemProperties properties{XR_TYPE_SYSTEM_PROPERTIES};
    if (const XrResult kRead = xrGetSystemProperties(state->instance, state->system, &properties); XR_FAILED(kRead)) {
        return failed("the system could not be read", kRead);
    }
    state->description.name = properties.systemName;
    state->description.orientationTracking = properties.trackingProperties.orientationTracking == XR_TRUE;
    state->description.positionTracking = properties.trackingProperties.positionTracking == XR_TRUE;
    std::uint32_t views = 0;
    if (const XrResult kCounted = xrEnumerateViewConfigurationViews(
            state->instance, state->system, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, 0, &views, nullptr);
        XR_FAILED(kCounted)) {
        return failed("the system has no stereo views", kCounted);
    }
    std::vector<XrViewConfigurationView> configured(views, {XR_TYPE_VIEW_CONFIGURATION_VIEW});
    if (const XrResult kRead = xrEnumerateViewConfigurationViews(state->instance,
                                                                 state->system,
                                                                 XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO,
                                                                 views,
                                                                 &views,
                                                                 configured.data());
        XR_FAILED(kRead)) {
        return failed("the system's views could not be read", kRead);
    }
    for (const XrViewConfigurationView& kView : configured) {
        state->description.views.push_back({.width = kView.recommendedImageRectWidth,
                                            .height = kView.recommendedImageRectHeight,
                                            .samples = kView.recommendedSwapchainSampleCount});
    }
    RAWFRAME_TRY(function(state->instance, "xrCreateVulkanInstanceKHR", state->createVulkanInstance));
    RAWFRAME_TRY(function(state->instance, "xrGetVulkanGraphicsDevice2KHR", state->getVulkanGraphicsDevice));
    RAWFRAME_TRY(function(state->instance, "xrCreateVulkanDeviceKHR", state->createVulkanDevice));
    // The binding's requirements are asked before any Vulkan object is
    // made, as OpenXR requires: the device layer makes Vulkan 1.3's.
    PFN_xrGetVulkanGraphicsRequirements2KHR requirements = nullptr;
    RAWFRAME_TRY(function(state->instance, "xrGetVulkanGraphicsRequirements2KHR", requirements));
    XrGraphicsRequirementsVulkan2KHR needed{XR_TYPE_GRAPHICS_REQUIREMENTS_VULKAN2_KHR};
    if (const XrResult kRead = requirements(state->instance, state->system, &needed); XR_FAILED(kRead)) {
        return failed("the runtime's Vulkan requirements could not be read", kRead);
    }
    const XrVersion kMade = XR_MAKE_VERSION(1, 3, 0);
    if (needed.minApiVersionSupported > kMade) {
        return refused(XrError::Runtime, "the runtime needs a newer Vulkan than the device layer makes");
    }
    return std::unique_ptr<Runtime>{new Runtime{std::move(state)}};
}

const SystemDescription& Runtime::system() const noexcept {
    return state_->description;
}

result::Result<void*> Runtime::instance(const void* createInfo, void* getInstanceProcAddr) {
    XrVulkanInstanceCreateInfoKHR create{XR_TYPE_VULKAN_INSTANCE_CREATE_INFO_KHR};
    create.systemId = state_->system;
    create.pfnGetInstanceProcAddr = reinterpret_cast<PFN_vkGetInstanceProcAddr>(getInstanceProcAddr);
    create.vulkanCreateInfo = static_cast<const VkInstanceCreateInfo*>(createInfo);
    VkInstance made = VK_NULL_HANDLE;
    VkResult vulkan = VK_SUCCESS;
    if (const XrResult kMade = state_->createVulkanInstance(state_->instance, &create, &made, &vulkan);
        XR_FAILED(kMade) || vulkan != VK_SUCCESS) {
        return failed("the runtime could not make the Vulkan instance",
                      XR_FAILED(kMade) ? kMade : XR_ERROR_RUNTIME_FAILURE);
    }
    return static_cast<void*>(made);
}

result::Result<void*> Runtime::physicalDevice(void* instance) {
    XrVulkanGraphicsDeviceGetInfoKHR asked{XR_TYPE_VULKAN_GRAPHICS_DEVICE_GET_INFO_KHR};
    asked.systemId = state_->system;
    asked.vulkanInstance = static_cast<VkInstance>(instance);
    VkPhysicalDevice found = VK_NULL_HANDLE;
    if (const XrResult kFound = state_->getVulkanGraphicsDevice(state_->instance, &asked, &found); XR_FAILED(kFound)) {
        return failed("the runtime named no Vulkan adapter", kFound);
    }
    return static_cast<void*>(found);
}

result::Result<void*> Runtime::device(void* physicalDevice, const void* createInfo, void* getInstanceProcAddr) {
    XrVulkanDeviceCreateInfoKHR create{XR_TYPE_VULKAN_DEVICE_CREATE_INFO_KHR};
    create.systemId = state_->system;
    create.pfnGetInstanceProcAddr = reinterpret_cast<PFN_vkGetInstanceProcAddr>(getInstanceProcAddr);
    create.vulkanPhysicalDevice = static_cast<VkPhysicalDevice>(physicalDevice);
    create.vulkanCreateInfo = static_cast<const VkDeviceCreateInfo*>(createInfo);
    VkDevice made = VK_NULL_HANDLE;
    VkResult vulkan = VK_SUCCESS;
    if (const XrResult kMade = state_->createVulkanDevice(state_->instance, &create, &made, &vulkan);
        XR_FAILED(kMade) || vulkan != VK_SUCCESS) {
        return failed("the runtime could not make the Vulkan device",
                      XR_FAILED(kMade) ? kMade : XR_ERROR_RUNTIME_FAILURE);
    }
    return static_cast<void*>(made);
}

} // namespace rawframe::xr
