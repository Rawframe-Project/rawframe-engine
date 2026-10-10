#pragma once

// The physical controls bindings name (SPEC-0029's binding grammar): keys by
// where they are on the keyboard, mouse buttons, wheel, and motion, the
// standard gamepad by location, and a touch screen's virtual controls
// (D387). Every control has one name and one shape.

#include <compare>
#include <cstdint>
#include <optional>
#include <string_view>

namespace rawframe::input {

/// SPEC-0029's closed generation-1 set, a touch screen (D387), the class
/// ADR-0037 left open, and a headset's hand controllers (ADR-0081, D596):
/// both hands one device, as the XR runtime presents them through the
/// interaction profile it chose, the simple controller's controls the
/// floor every profile gives.
enum class DeviceClass : std::uint8_t {
    Keyboard,
    Mouse,
    Gamepad,
    Touch,
    Controller
};

/// What a control reports: on or off, one axis, two, or a pose (ADR-0081's
/// fourth value, D596).
enum class ControlShape : std::uint8_t {
    Digital,
    Axis1,
    Axis2,
    Pose
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
/// `middle`, `back`, `forward`, `wheel_x`, `wheel_y`, `delta`, and
/// `pointer` (D367), the cursor's place over the window. Gamepad
/// controls are SPEC-0029's standard locations. Touch controls (D387):
/// `stick_left` and `stick_right`, a stick under a touch that began in that
/// half of the window, and `left` and `right`, held while that touch is;
/// and `pointer`, the newest touch's place. Controller controls (D596), the
/// simple controller's, each hand's: `select_left`, `select_right`,
/// `menu_left`, and `menu_right`, digital; and `grip_left`, `grip_right`,
/// `aim_left`, and `aim_right`, poses: where the hand holds the controller,
/// and where it points from.
[[nodiscard]] std::optional<Control> controlNamed(DeviceClass device, std::string_view name) noexcept;
[[nodiscard]] std::string_view nameOf(Control control) noexcept;
/// The key at a USB HID keyboard usage (page 7), the number a window system
/// reports for a key's place; nothing for a usage no key here has.
[[nodiscard]] std::optional<Control> keyOfUsage(std::uint16_t usage) noexcept;
[[nodiscard]] ControlShape shapeOf(Control control) noexcept;
/// Whether a control reports motion since the last report rather than a
/// position: the wheel and the mouse's delta. What such a control reports
/// in one tick adds up, and it rests at nought after.
[[nodiscard]] bool relative(Control control) noexcept;
/// Whether a control reports a place rather than a deflection: the mouse's
/// and the touch screen's `pointer`, over the window in logical pixels from
/// its top left, y down (ADR-0046's screen space, D367). Its value is the place,
/// neither clamped nor dead-zoned, and it stays where it was last told.
[[nodiscard]] bool positional(Control control) noexcept;

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
