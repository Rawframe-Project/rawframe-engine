#pragma once

// The physical controls bindings name (SPEC-0029's binding grammar): keys by
// where they are on the keyboard, mouse buttons, wheel, and motion, and the
// standard gamepad by location. Every control has one name and one shape.

#include <compare>
#include <cstdint>
#include <optional>
#include <string_view>

namespace rawframe::input {

/// SPEC-0029's closed generation-1 set.
enum class DeviceClass : std::uint8_t {
    Keyboard,
    Mouse,
    Gamepad
};

/// What a control reports: on or off, one axis, or two.
enum class ControlShape : std::uint8_t {
    Digital,
    Axis1,
    Axis2
};

/// One physical control of a device class, by its code in that class's
/// table. Code nought is no control.
struct Control {
    DeviceClass device = DeviceClass::Keyboard;
    std::uint16_t code = 0;

    [[nodiscard]] bool valid() const noexcept {
        return code != 0;
    }
    friend constexpr auto operator<=>(const Control&, const Control&) noexcept = default;
};

/// Keyboard keys are named by their physical location, as the W3C UI Events
/// `code` values are, written in snake case (`KeyW` is `key_w`,
/// `ArrowUp` is `arrow_up`, `ShiftLeft` is `shift_left`), so a binding means
/// the same place on every layout. Mouse controls: `left`, `right`,
/// `middle`, `back`, `forward`, `wheel_x`, `wheel_y`, `delta`. Gamepad
/// controls are SPEC-0029's standard locations.
[[nodiscard]] std::optional<Control> controlNamed(DeviceClass device, std::string_view name) noexcept;
[[nodiscard]] std::string_view nameOf(Control control) noexcept;
[[nodiscard]] ControlShape shapeOf(Control control) noexcept;
/// Whether a control reports motion since the last report rather than a
/// position: the wheel and the mouse's delta. What such a control reports
/// in one tick adds up, and it rests at nought after.
[[nodiscard]] bool relative(Control control) noexcept;

[[nodiscard]] std::optional<DeviceClass> deviceClassNamed(std::string_view name) noexcept;
[[nodiscard]] std::string_view nameOf(DeviceClass device) noexcept;

/// Keyboard modifiers a binding may require, as bits.
enum class Modifier : std::uint8_t {
    Ctrl = 1,
    Shift = 2,
    Alt = 4,
    Meta = 8
};

[[nodiscard]] std::optional<Modifier> modifierNamed(std::string_view name) noexcept;
/// The modifier a key is, if it is one (either side).
[[nodiscard]] std::optional<Modifier> modifierOf(Control key) noexcept;

} // namespace rawframe::input
