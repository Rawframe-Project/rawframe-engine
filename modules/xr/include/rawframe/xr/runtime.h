#pragma once

// An OpenXR runtime's head-mounted system (ADR-0081's XR admission, D591).
// The XR module is a presentation producer beside the window module, never
// a window (ADR-0081, section 2): the runtime owns the images a headset
// shows and paces their frames. The runtime is found as Khronos's loader
// finds one (XR_RUNTIME_JSON, else the active runtime's manifest); its
// system is a head-mounted display with stereo views, drawn through
// Vulkan (XR_KHR_vulkan_enable2), whose instance and device the runtime
// makes for the render module's one device: a runtime is the device's
// `render::VulkanMaker`. Client only: no dedicated server reaches this
// module (ADR-0081, scenario 3). Main thread only.

#include "rawframe/render/device.h"
#include "rawframe/result/result.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace rawframe::xr {

struct RuntimeSettings {
    /// The application's name, as the runtime shows it.
    std::string application = "Rawframe";
};

/// A view's images as the runtime recommends them.
struct ViewSize {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint32_t samples = 1;
};

struct SystemDescription {
    /// The runtime's name and version, as it gives them.
    std::string runtime;
    /// The system's name.
    std::string name;
    /// Its views, the left eye's first: two for the stereo views of a
    /// head-mounted display.
    std::vector<ViewSize> views;
    bool orientationTracking = false;
    bool positionTracking = false;
};

class Session;

class Runtime final : public render::VulkanMaker {
public:
    /// The runtime's head-mounted system: `NoRuntime` where no runtime
    /// answers or it has no such system now, `Runtime` where it cannot
    /// draw through Vulkan.
    [[nodiscard]] static result::Result<std::unique_ptr<Runtime>> open(const RuntimeSettings& settings);

    ~Runtime() override;

    [[nodiscard]] const SystemDescription& system() const noexcept;

    /// `render::VulkanMaker`'s: the device's Vulkan objects made by the
    /// runtime, for its system.
    [[nodiscard]] result::Result<void*> instance(const void* createInfo, void* getInstanceProcAddr) override;
    [[nodiscard]] result::Result<void*> physicalDevice(void* instance) override;
    [[nodiscard]] result::Result<void*>
    device(void* physicalDevice, const void* createInfo, void* getInstanceProcAddr) override;

    struct State;

private:
    friend class Session;
    explicit Runtime(std::unique_ptr<State> state) noexcept;
    std::unique_ptr<State> state_;
};

} // namespace rawframe::xr
