#pragma once

// A window system's raw input as a player's devices (SPEC-0025 into
// SPEC-0029): keys by their HID usage, mouse buttons, the wheel, a
// captured pointer's motion, and mapped gamepads become control events of
// their devices in the feed; losing focus or input lets go of everything.
// The window reports sticks one axis at a time, down positive; the feed
// gets both axes of a stick together, up positive, as the mapper reads
// them. The other way, what the feed asks a gamepad to feel runs its
// motors (D251).

#include "rawframe/input/feed.h"
#include "rawframe/window/events.h"
#include "rawframe/window/windows.h"

#include <array>
#include <cstdint>
#include <string_view>
#include <vector>

namespace rawframe::input_window {

class Bridge {
public:
    /// Connects the keyboard and the mouse, which every window has.
    explicit Bridge(input::Feed& feed);

    /// Takes one record; what is not input is left alone.
    void take(const window::Event& event);

    /// Runs the motors of the gamepads the feed's waiting haptic commands
    /// name: an unspecified frequency drives both, one below
    /// `kLowMotorHertz` the heavy one, any other the light one, each at the
    /// command's amplitude.
    void feel(window::Windows& windows);
    /// Commands for no gamepad of this window system, or that its gamepad
    /// refused (one without motors).
    [[nodiscard]] std::uint64_t unfelt() const noexcept {
        return unfelt_;
    }

    static constexpr float kLowMotorHertz = 150;

private:
    struct Pad {
        window::GamepadId gamepad;
        input::DeviceId device;
        /// The sticks' last axes, by window::GamepadAxis.
        std::array<float, 4> sticks{};
    };

    void button(input::DeviceId device, input::DeviceClass deviceClass, std::string_view name, bool down);
    void gamepadAxis(const window::Event& event);

    input::Feed* feed_;
    std::vector<Pad> pads_;
    std::uint32_t nextDevice_;
    std::vector<input::HapticCommand> felt_;
    std::uint64_t unfelt_ = 0;
};

/// The keyboard's and the mouse's devices in the feed.
inline constexpr input::DeviceId kKeyboard{1};
inline constexpr input::DeviceId kMouse{2};

} // namespace rawframe::input_window
