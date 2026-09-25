#include "rawframe/input/controls.h"

#include <array>
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

constexpr std::array<std::string_view, 8> kMouse = {
    "left",
    "right",
    "middle",
    "back",
    "forward",
    "wheel_x",
    "wheel_y",
    "delta",
};

constexpr std::array<std::string_view, 19> kGamepad = {
    "dpad_up",           "dpad_down",     "dpad_left",  "dpad_right",    "face_south",
    "face_east",         "face_west",     "face_north", "shoulder_left", "shoulder_right",
    "trigger_left",      "trigger_right", "stick_left", "stick_right",   "stick_left_click",
    "stick_right_click", "start",         "select",     "guide",
};

std::span<const std::string_view> tableOf(DeviceClass device) noexcept {
    switch (device) {
    case DeviceClass::Keyboard:
        return kKeys;
    case DeviceClass::Mouse:
        return kMouse;
    case DeviceClass::Gamepad:
        return kGamepad;
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

std::string_view nameOf(Control control) noexcept {
    const std::span<const std::string_view> kTable = tableOf(control.device);
    return control.code == 0 || control.code > kTable.size() ? std::string_view{} : kTable[control.code - 1U];
}

ControlShape shapeOf(Control control) noexcept {
    const std::string_view kName = nameOf(control);
    if (kName == "wheel_x" || kName == "wheel_y" || kName == "trigger_left" || kName == "trigger_right") {
        return ControlShape::Axis1;
    }
    if (kName == "delta" || kName == "stick_left" || kName == "stick_right") {
        return ControlShape::Axis2;
    }
    return ControlShape::Digital;
}

bool relative(Control control) noexcept {
    return control.device == DeviceClass::Mouse && shapeOf(control) != ControlShape::Digital;
}

std::optional<DeviceClass> deviceClassNamed(std::string_view name) noexcept {
    for (const DeviceClass kDevice : {DeviceClass::Keyboard, DeviceClass::Mouse, DeviceClass::Gamepad}) {
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
