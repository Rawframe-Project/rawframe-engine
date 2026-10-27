#pragma once

// An OpenXR runtime's head-mounted system.
// The XR module is a presentation producer beside the window module, never
// a window: the runtime owns the images a headset
// shows and paces their frames. The runtime is the one the system has
// made active (Windows' registry, Linux's active_runtime.json), or the
// manifest a trusted configuration names; the loader's own environment
// overrides (XR_RUNTIME_JSON, XR_API_LAYER_PATH, XR_ENABLE_API_LAYERS),
// which would let whoever sets a variable choose the native code it loads,
// are removed before it runs, in every build. Its
// system is a head-mounted display with stereo views, drawn through
// Vulkan (XR_KHR_vulkan_enable2), whose instance and device the runtime
// makes for the render module's one device: a runtime is the device's
// `render::VulkanMaker`. Client only: no dedicated server reaches this
// module (scenario 3). Main thread only.

#include "rawframe/render/device.h"
#include "rawframe/result/result.h"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace rawframe::xr {

struct RuntimeSettings {
    /// The application's name, as the runtime shows it.
    std::string application = "Rawframe";
    /// A runtime's manifest, named by trusted configuration (`xr.runtime`);
    /// empty for the runtime the system has made active.
    std::filesystem::path manifest;
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
    /// The system tracks hands, their joints read apart from the actions
    /// (XR_EXT_hand_tracking).
    bool handTracking = false;
    /// The loader's environment overrides that were set and removed before
    /// it ran, comma-separated; empty where none was.
    std::string overridesIgnored;
};

class Session;

class Runtime final : public render::VulkanMaker {
public:
    /// The runtime's head-mounted system: `NoRuntime` where no runtime
    /// answers or it has no such system now, `Runtime` where it cannot
    /// draw through Vulkan. A refusal names the overrides ignored, if any,
    /// as its `ignored` context.
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
    /// `open`, once the loader is confined.
    [[nodiscard]] static result::Result<std::unique_ptr<Runtime>> made(const RuntimeSettings& settings);
    std::unique_ptr<State> state_;
};

} // namespace rawframe::xr
