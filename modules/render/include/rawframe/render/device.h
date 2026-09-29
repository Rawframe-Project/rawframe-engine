#pragma once

// The one device (ADR-0029, ADR-0045, SPEC-0024): Maul RHI's instance, the
// adapter chosen, and the device opened on it, owned here and lent to the
// canvas and scene renderers; nothing else holds a device. Opening is a
// request answered later, as Maul RHI answers every request, so a client
// on the web, whose answers arrive between frames, opens it the same way:
// ask, then look each frame until it is ready. Client only; a dedicated
// server links none of it (D277).

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
};

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

    struct State;

private:
    explicit Device(std::unique_ptr<State> state) noexcept;
    std::unique_ptr<State> state_;
};

} // namespace rawframe::render
