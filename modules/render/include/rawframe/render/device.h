#pragma once

// The one device (ADR-0029, ADR-0045, SPEC-0024): Maul RHI's instance, the
// adapter chosen, and the device opened on it, owned here and lent to the
// canvas and scene renderers; nothing else holds a device. Opening is a
// request answered later, as Maul RHI answers every request, so a client
// on the web, whose answers arrive between frames, opens it the same way:
// ask, then look each frame until it is ready. Client only; a dedicated
// server links none of it (D277).
//
// The device side of the window seam lives here too (SPEC-0024, SPEC-0025,
// D280): a window's surface is made from its handle bundle, configured at
// `prepare` to the window's size and the present policy, and its image
// acquired into a frame.

#include "rawframe/composition/participant.h"
#include "rawframe/result/result.h"
#include "rawframe/window/handles.h"
#include "rawframe/window/surfaces.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>

// Maul RHI's own; only the rendering cluster (render, render_canvas,
// render_scene) includes its headers and so can use one (ADR-0045).
struct mrhiDevice;

namespace rawframe::render {

struct DeviceSettings {
    /// Whether a rasterizer running on the CPU may serve: CI and tests
    /// (ADR-0029), never a product's first choice.
    bool allowSoftware = false;
};

/// What the device was opened on.
struct AdapterDescription {
    std::string name;
    /// A rasterizer running on the CPU.
    bool software = false;
    /// Whether it takes BC-compressed textures (BC7 is the desktop form a
    /// cooked texture may have, D253); asked for whenever it does.
    bool blockCompression = false;
};

/// The bytes one frame may upload, and those its readbacks may hold, on
/// the device (D280): a 1080p picture read back fits, and a frame's
/// textures upload within the first. Maul RHI keeps as many upload bytes
/// again for each frame in flight.
inline constexpr std::uint32_t kFrameUploadBytes = std::uint32_t{16} << 20U;
inline constexpr std::uint32_t kReadbackBytes = std::uint32_t{16} << 20U;

/// SPEC-0024's present policies. Each falls back along its chain to what
/// the surface offers: adaptive and low-latency vsync to vsync, immediate
/// to low-latency vsync, then vsync, which every surface offers.
enum class PresentPolicy : std::uint8_t {
    Vsync,
    AdaptiveVsync,
    LowLatencyVsync,
    Immediate,
};

/// What `prepare` found for a surface this frame.
struct PreparedSurface {
    /// Whether the frame may draw to it: false while the window is hidden
    /// or without size, and while it could not be configured this frame.
    bool drawable = false;
    /// Configured again this frame: a new size, policy, or an image out of
    /// date.
    bool reconfigured = false;
    /// The policy it presents by, once configured: the one asked for, or
    /// the first of its chain the surface offers.
    PresentPolicy policy = PresentPolicy::Vsync;
    /// Its images' size.
    window::PixelSize size;
};

/// A request of the device's, as the rendering cluster names it to find its
/// answer: Maul RHI's request id, index and generation.
[[nodiscard]] constexpr std::uint64_t requestKey(std::uint32_t index1, std::uint32_t generation) noexcept {
    return (std::uint64_t{index1} << 32U) | generation;
}

class Device {
public:
    /// Asks for a device: an instance now, its adapters at the first
    /// `open`, which only lists those presenting to the first surface
    /// associated before it, if any.
    [[nodiscard]] static result::Result<std::unique_ptr<Device>> request(const DeviceSettings& settings);

    Device(const Device&) = delete;
    Device& operator=(const Device&) = delete;
    ~Device();

    /// Moves the opening on: true once the device is ready, false while it
    /// is still opening, and an error once it failed (`NoAdapter` when no
    /// adapter the settings allow answered). Never waits.
    [[nodiscard]] result::Result<bool> open();

    /// Once found.
    [[nodiscard]] const std::optional<AdapterDescription>& adapter() const noexcept;

    /// The device, once ready; for the rendering cluster only.
    [[nodiscard]] mrhiDevice* native() const noexcept;

    /// Takes every answer the device has for its requests (a frame done, a
    /// pipeline made, a readback ready) and keeps each until its asker takes
    /// it: one queue, several clients. Once a frame, and before looking for
    /// an answer.
    void pump();
    /// The answer to `request`, taken: success, or the error that ended it;
    /// none while it is owed.
    [[nodiscard]] std::optional<result::Status> answer(std::uint64_t request);
    /// Whether the device was lost: SPEC-0024's terminal failure of
    /// presentation in generation 1.
    [[nodiscard]] bool lost() const noexcept;

    /// Makes a surface from a window's handle bundle, the seam's one
    /// crossing for its generation; the key names it from then on. One
    /// made before the first `open` chooses the adapter.
    [[nodiscard]] result::Result<std::uint64_t> associate(const window::HandleBundle& bundle);
    /// Ends a surface: its configuration, then it. Its images are retired
    /// after the frames that used them.
    void release(std::uint64_t surface) noexcept;
    /// SPEC-0024's `prepare` for one surface, once a frame before a frame
    /// draws to it: configured again when its window's size, the policy,
    /// or an image out of date says so, at most once a frame.
    [[nodiscard]] result::Result<PreparedSurface>
    prepare(std::uint64_t surface, const window::SurfaceState& state, PresentPolicy policy);
    /// In an open frame: the surface's next image as the frame's resource,
    /// named as `requestKey` names ids, presented when the frame is
    /// submitted; none when it has none this frame (hidden, or out of date,
    /// which the next `prepare` mends). For the rendering cluster only.
    [[nodiscard]] result::Result<std::optional<std::uint64_t>> acquire(std::uint64_t surface);
    /// A prepared surface's format, as Maul RHI names it; for the
    /// rendering cluster's pipelines only.
    [[nodiscard]] std::uint32_t surfaceFormat(std::uint64_t surface) const noexcept;

    struct State;

private:
    explicit Device(std::unique_ptr<State> state) noexcept;
    std::unique_ptr<State> state_;
};

/// The process's one device as the rendering cluster reaches it (D279):
/// the device participant (registrar.h) asks for it, opens it, and lends
/// it once it is ready.
class DeviceHolder {
public:
    DeviceHolder() = default;
    DeviceHolder(const DeviceHolder&) = delete;
    DeviceHolder& operator=(const DeviceHolder&) = delete;
    virtual ~DeviceHolder() = default;

    /// The device, ready and not lost; none while it opens, when the
    /// process asked for none, and once it failed or was lost.
    [[nodiscard]] virtual Device* ready() noexcept = 0;
    /// SPEC-0024's `prepare` for a window's surface, by the process's
    /// present policy: the surface and what was found, or none while the
    /// window has no surface on the device. Once a frame, before drawing.
    [[nodiscard]] virtual std::optional<std::pair<std::uint64_t, PreparedSurface>>
    prepare(window::WindowId window) noexcept = 0;
};

inline constexpr composition::Capability<DeviceHolder> kDevice{"rawframe.render.device"};

} // namespace rawframe::render
