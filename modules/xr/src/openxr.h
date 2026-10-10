#pragma once

// OpenXR's headers as the module uses them, with Vulkan's binding, and what
// the runtime and its sessions share.

#include "rawframe/xr/errors.h"
#include "rawframe/xr/runtime.h"

#include <string_view>
#include <vulkan/vulkan_core.h>

#define XR_USE_GRAPHICS_API_VULKAN
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>

namespace rawframe::xr {

/// The runtime's refusal, named, as this module's failure: `NoRuntime` for
/// what says no runtime or system answers, `Runtime` otherwise.
[[nodiscard]] std::unexpected<result::Error> failed(std::string_view why, XrResult outcome);
[[nodiscard]] std::unexpected<result::Error> refused(XrError error, std::string_view why);

struct Runtime::State {
    XrInstance instance = XR_NULL_HANDLE;
    XrSystemId system = XR_NULL_SYSTEM_ID;
    SystemDescription description;
    /// XR_KHR_vulkan_enable2's functions, read from the instance.
    PFN_xrCreateVulkanInstanceKHR createVulkanInstance = nullptr;
    PFN_xrGetVulkanGraphicsDevice2KHR getVulkanGraphicsDevice = nullptr;
    PFN_xrCreateVulkanDeviceKHR createVulkanDevice = nullptr;

    State() = default;
    State(const State&) = delete;
    State& operator=(const State&) = delete;
    ~State() {
        if (instance != XR_NULL_HANDLE) {
            xrDestroyInstance(instance);
        }
    }
};

} // namespace rawframe::xr
