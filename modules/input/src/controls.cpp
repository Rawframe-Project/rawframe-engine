#include "rawframe/input/controls.h"

#include <array>
#include <cstdint>
#include <span>

namespace rawframe::input {

namespace {

/// Keys in W3C UI Events `code` order by section: writing system, functional,
/// control pad, arrows, numpad, function. A key's code is its place here
/// plus one; the order never changes, only grows at the end.
constexpr std::array<std::string_view, 105> kKeys = {
    "backquote",
    "backslash",
    "bracket_left",
    "bracket_right",
    "comma",
    "digit_0",
    "digit_1",
    "digit_2",
    "digit_3",
    "digit_4",
    "digit_5",
    "digit_6",
    "digit_7",
    "digit_8",
    "digit_9",
    "equal",
    "intl_backslash",
    "key_a",
    "key_b",
    "key_c",
    "key_d",
    "key_e",
    "key_f",
    "key_g",
    "key_h",
    "key_i",
    "key_j",
    "key_k",
    "key_l",
    "key_m",
    "key_n",
    "key_o",
    "key_p",
    "key_q",
    "key_r",
    "key_s",
    "key_t",
    "key_u",
    "key_v",
    "key_w",
    "key_x",
    "key_y",
    "key_z",
    "minus",
    "period",
    "quote",
    "semicolon",
    "slash",
    "alt_left",
    "alt_right",
    "backspace",
    "caps_lock",
    "context_menu",
    "control_left",
    "control_right",
    "enter",
    "meta_left",
    "meta_right",
    "shift_left",
    "shift_right",
    "space",
    "tab",
    "delete",
    "end",
    "home",
    "insert",
    "page_down",
    "page_up",
    "arrow_down",
    "arrow_left",
    "arrow_right",
    "arrow_up",
    "num_lock",
    "numpad_0",
    "numpad_1",
    "numpad_2",
    "numpad_3",
    "numpad_4",
    "numpad_5",
    "numpad_6",
    "numpad_7",
    "numpad_8",
    "numpad_9",
    "numpad_add",
    "numpad_decimal",
    "numpad_divide",
    "numpad_enter",
    "numpad_multiply",
    "numpad_subtract",
    "escape",
    "f1",
    "f2",
    "f3",
    "f4",
    "f5",
    "f6",
    "f7",
    "f8",
    "f9",
    "f10",
    "f11",
    "f12",
    "print_screen",
    "scroll_lock",
    "pause",
};

/// Each key's USB HID keyboard usage (page 7), the number window systems
/// report for a key's place, in kKeys's order.
constexpr std::array<std::uint16_t, kKeys.size()> kKeyUsages = {
    53,  49,  47, 48, 54,  39,  30,  31, 32,  33,  34,  35,  36, 37, 38, 46, 100, // writing system, to intl_backslash
    4,   5,   6,  7,  8,   9,   10,  11, 12,  13,  14,  15,  16, 17, 18, 19, 20,
    21,  22,  23, 24, 25,  26,  27,  28, 29,                                     // key_a to key_z
    45,  55,  52, 51, 56,                                                        // minus to slash
    226, 230, 42, 57, 101, 224, 228, 40, 227, 231, 225, 229, 44, 43,             // functional
    76,  77,  74, 73, 78,  75,                                                   // control pad
    81,  80,  79, 82,                                                            // arrows
    83,  98,  89, 90, 91,  92,  93,  94, 95,  96,  97,  87,  99, 84, 88, 85, 86, // numpad
    41,  58,  59, 60, 61,  62,  63,  64, 65,  66,  67,  68,  69, 70, 71, 72,     // function
};

// `delta` is the device's own motion, given while the pointer is captured;
// `motion` the cursor's over the window, in logical pixels (D521): two
// streams SPEC-0025 keeps apart.
constexpr std::array<std::string_view, 10> kMouse = {
    "left",
    "right",
    "middle",
    "back",
    "forward",
    "wheel_x",
    "wheel_y",
    "delta",
    "pointer",
    "motion",
};

constexpr std::array<std::string_view, 19> kGamepad = {
    "dpad_up",           "dpad_down",     "dpad_left",  "dpad_right",    "face_south",
    "face_east",         "face_west",     "face_north", "shoulder_left", "shoulder_right",
    "trigger_left",      "trigger_right", "stick_left", "stick_right",   "stick_left_click",
    "stick_right_click", "start",         "select",     "guide",
};

constexpr std::array<std::string_view, 5> kTouch = {
    "left",
    "right",
    "stick_left",
    "stick_right",
    "pointer",
};

constexpr std::array<std::string_view, 10> kController = {
    "select_left",
    "select_right",
    "menu_left",
    "menu_right",
    "grip_left",
    "grip_right",
    "aim_left",
    "aim_right",
    "pointer_left",
    "pointer_right",
};

std::span<const std::string_view> tableOf(DeviceClass device) noexcept {
    switch (device) {
    case DeviceClass::Keyboard:
        return kKeys;
    case DeviceClass::Mouse:
        return kMouse;
    case DeviceClass::Gamepad:
        return kGamepad;
    case DeviceClass::Touch:
        return kTouch;
    case DeviceClass::Controller:
        return kController;
    }
    return {};
}

} // namespace

std::optional<Control> controlNamed(DeviceClass device, std::string_view name) noexcept {
    const std::span<const std::string_view> kTable = tableOf(device);
    for (std::size_t index = 0; index < kTable.size(); ++index) {
        if (kTable[index] == name) {
            return Control{.device = device, .code = static_cast<std::uint16_t>(index + 1)};
        }
    }
    return std::nullopt;
}

std::optional<Control> keyOfUsage(std::uint16_t usage) noexcept {
    for (std::size_t index = 0; index < kKeyUsages.size(); ++index) {
        if (usage != 0 && kKeyUsages[index] == usage) {
            return Control{.device = DeviceClass::Keyboard, .code = static_cast<std::uint16_t>(index + 1)};
        }
    }
    return std::nullopt;
}

std::string_view nameOf(Control control) noexcept {
    const std::span<const std::string_view> kTable = tableOf(control.device);
    return control.code == 0 || control.code > kTable.size() ? std::string_view{} : kTable[control.code - 1U];
}

ControlShape shapeOf(Control control) noexcept {
    const std::string_view kName = nameOf(control);
    if (control.device == DeviceClass::Controller && (kName.starts_with("grip_") || kName.starts_with("aim_"))) {
        return ControlShape::Pose;
    }
    if (kName == "wheel_x" || kName == "wheel_y" || kName == "trigger_left" || kName == "trigger_right") {
        return ControlShape::Axis1;
    }
    if (kName == "delta" || kName == "motion" || kName == "stick_left" || kName == "stick_right" ||
        kName == "pointer" || kName == "pointer_left" || kName == "pointer_right") {
        return ControlShape::Axis2;
    }
    return ControlShape::Digital;
}

bool relative(Control control) noexcept {
    return control.device == DeviceClass::Mouse && shapeOf(control) != ControlShape::Digital && !positional(control);
}

bool positional(Control control) noexcept {
    const std::string_view kName = nameOf(control);
    return ((control.device == DeviceClass::Mouse || control.device == DeviceClass::Touch) && kName == "pointer") ||
           (control.device == DeviceClass::Controller && (kName == "pointer_left" || kName == "pointer_right"));
}

std::optional<DeviceClass> deviceClassNamed(std::string_view name) noexcept {
    for (const DeviceClass kDevice : {DeviceClass::Keyboard,
                                      DeviceClass::Mouse,
                                      DeviceClass::Gamepad,
                                      DeviceClass::Touch,
                                      DeviceClass::Controller}) {
        if (nameOf(kDevice) == name) {
            return kDevice;
        }
    }
    return std::nullopt;
}

std::string_view nameOf(DeviceClass device) noexcept {
    switch (device) {
    case DeviceClass::Keyboard:
        return "keyboard";
    case DeviceClass::Mouse:
        return "mouse";
    case DeviceClass::Gamepad:
        return "gamepad";
    case DeviceClass::Touch:
        return "touch";
    case DeviceClass::Controller:
        return "controller";
    }
    return {};
}

std::optional<Modifier> modifierNamed(std::string_view name) noexcept {
    if (name == "ctrl") {
        return Modifier::Ctrl;
    }
    if (name == "shift") {
        return Modifier::Shift;
    }
    if (name == "alt") {
        return Modifier::Alt;
    }
    if (name == "meta") {
        return Modifier::Meta;
    }
    return std::nullopt;
}

std::optional<Modifier> modifierOf(Control key) noexcept {
    if (key.device != DeviceClass::Keyboard) {
        return std::nullopt;
    }
    const std::string_view kName = nameOf(key);
    if (kName == "control_left" || kName == "control_right") {
        return Modifier::Ctrl;
    }
    if (kName == "shift_left" || kName == "shift_right") {
        return Modifier::Shift;
    }
    if (kName == "alt_left" || kName == "alt_right") {
        return Modifier::Alt;
    }
    if (kName == "meta_left" || kName == "meta_right") {
        return Modifier::Meta;
    }
    return std::nullopt;
}

} // namespace rawframe::input
