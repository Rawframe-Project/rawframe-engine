#pragma once

// The one device (ADR-0029, ADR-0045, SPEC-0024): Maul RHI's instance, the
// adapter chosen, and the device opened on it, owned here and lent to the
// canvas and scene renderers; nothing else holds a device. Opening is a
// request answered later, as Maul RHI answers every request, so a client
// on the web, whose answers arrive between frames, opens it the same way:
// ask, then look each frame until it is ready. Client only; a dedicated
// server links none of it (D277).

#include "rawframe/composition/participant.h"
#include "rawframe/result/result.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>

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

/// A request of the device's, as the rendering cluster names it to find its
/// answer: Maul RHI's request id, index and generation.
[[nodiscard]] constexpr std::uint64_t requestKey(std::uint32_t index1, std::uint32_t generation) noexcept {
    return (std::uint64_t{index1} << 32U) | generation;
}

class Device {
public:
    /// Asks for a device: an instance, then its adapters.
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
};

inline constexpr composition::Capability<DeviceHolder> kDevice{"rawframe.render.device"};

} // namespace rawframe::render
