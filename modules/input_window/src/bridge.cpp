#include "rawframe/input_window/bridge.h"

#include <algorithm>
#include <array>
#include <string_view>

namespace rawframe::input_window {

namespace {

std::string_view mouseButtonName(window::MouseButton button) noexcept {
    switch (button) {
    case window::MouseButton::Left:
        return "left";
    case window::MouseButton::Right:
        return "right";
    case window::MouseButton::Middle:
        return "middle";
    case window::MouseButton::Back:
        return "back";
    case window::MouseButton::Forward:
        return "forward";
    case window::MouseButton::None:
        break;
    }
    return {};
}

/// The mapper's names for a mapped gamepad's buttons, by window::GamepadButton.
constexpr std::array<std::string_view, 15> kPadButtons = {
    "dpad_up",
    "dpad_down",
    "dpad_left",
    "dpad_right",
    "face_south",
    "face_east",
    "face_west",
    "face_north",
    "shoulder_left",
    "shoulder_right",
    "stick_left_click",
    "stick_right_click",
    "start",
    "select",
    "guide",
};

} // namespace

Bridge::Bridge(input::Feed& feed) : feed_(&feed), touch_(feed, kTouchScreen), nextDevice_(kTouchScreen.value + 1) {
    feed_->connect(kKeyboard, input::DeviceClass::Keyboard);
    feed_->connect(kMouse, input::DeviceClass::Mouse);
}

void Bridge::button(input::DeviceId device, input::DeviceClass deviceClass, std::string_view name, bool down) {
    if (const auto kControl = input::controlNamed(deviceClass, name)) {
        feed_->submit({.device = device, .control = *kControl, .x = down ? 1.0F : 0.0F});
    }
}

void Bridge::take(const window::Event& event) {
    using window::EventKind;
    const auto kPad = std::ranges::find(pads_, event.gamepad, &Pad::gamepad);
    switch (event.kind) {
    case EventKind::KeyDown:
    case EventKind::KeyUp:
        // A repeat changes nothing the mapper holds.
        if (!event.key.repeat) {
            if (const auto kKey = input::keyOfUsage(event.key.usage)) {
                feed_->submit(
                    {.device = kKeyboard, .control = *kKey, .x = event.kind == EventKind::KeyDown ? 1.0F : 0.0F});
            }
        }
        break;
    case EventKind::ButtonDown:
    case EventKind::ButtonUp:
        button(kMouse,
               input::DeviceClass::Mouse,
               mouseButtonName(event.pointer.button),
               event.kind == EventKind::ButtonDown);
        break;
    case EventKind::Wheel:
        if (event.motion.x != 0) {
            feed_->submit({.device = kMouse,
                           .control = *input::controlNamed(input::DeviceClass::Mouse, "wheel_x"),
                           .x = event.motion.x});
        }
        if (event.motion.y != 0) {
            feed_->submit({.device = kMouse,
                           .control = *input::controlNamed(input::DeviceClass::Mouse, "wheel_y"),
                           .x = event.motion.y});
        }
        break;
    case EventKind::CursorMoved:
        cursorMoved(event);
        break;
    case EventKind::CursorEntered:
    case EventKind::CursorLeft:
        // Where the cursor comes back in is no motion from where it left.
        cursor_.reset();
        break;
    case EventKind::RawPointerDelta:
        // Up positive, as a stick is, so one look action reads both.
        feed_->submit({.device = kMouse,
                       .control = *input::controlNamed(input::DeviceClass::Mouse, "delta"),
                       .x = event.motion.x,
                       .y = -event.motion.y});
        break;
    case EventKind::TouchDown:
        touch_.down(event.touch.id, event.touch.position.x, event.touch.position.y);
        break;
    case EventKind::TouchMoved:
        touch_.move(event.touch.id, event.touch.position.x, event.touch.position.y);
        break;
    case EventKind::TouchUp:
    case EventKind::TouchCancelled:
        touch_.up(event.touch.id);
        break;
    case EventKind::Resized:
        resize(event.size.width);
        break;
    case EventKind::FocusLost:
    case EventKind::InputStateReset:
        // A gamepad's lost records included: letting go of every device is
        // the one release the mapper has, and never leaves one held.
        feed_->releaseAll();
        touch_.forget();
        cursor_.reset();
        break;
    case EventKind::GamepadAdded:
        if (kPad == pads_.end()) {
            pads_.push_back(Pad{.gamepad = event.gamepad, .device = input::DeviceId{nextDevice_++}});
            feed_->connect(pads_.back().device, input::DeviceClass::Gamepad);
        }
        break;
    case EventKind::GamepadRemoved:
        if (kPad != pads_.end()) {
            feed_->disconnect(kPad->device);
            pads_.erase(kPad);
        }
        break;
    case EventKind::GamepadButtonDown:
    case EventKind::GamepadButtonUp:
        // An unmapped pad's buttons name no place the action set binds.
        if (kPad != pads_.end() && !event.gamepadInput.raw && event.gamepadInput.control < kPadButtons.size()) {
            button(kPad->device,
                   input::DeviceClass::Gamepad,
                   kPadButtons[event.gamepadInput.control],
                   event.kind == EventKind::GamepadButtonDown);
        }
        break;
    case EventKind::GamepadAxisMoved:
        if (kPad != pads_.end() && !event.gamepadInput.raw) {
            gamepadAxis(event);
        }
        break;
    default:
        break;
    }
}

void Bridge::cursorMoved(const window::Event& event) {
    const window::Position kAt = event.pointer.position;
    // Where the cursor is over the window, logical pixels from its top
    // left, y down (D367).
    feed_->submit({.device = kMouse,
                   .control = *input::controlNamed(input::DeviceClass::Mouse, "pointer"),
                   .x = kAt.x,
                   .y = kAt.y});
    // Its motion since, up positive (D521): the window system gives the
    // device's own only while the pointer is captured, a stream apart.
    if (cursor_.has_value() && (kAt.x != cursor_->x || kAt.y != cursor_->y)) {
        feed_->submit({.device = kMouse,
                       .control = *input::controlNamed(input::DeviceClass::Mouse, "motion"),
                       .x = kAt.x - cursor_->x,
                       .y = cursor_->y - kAt.y});
    }
    cursor_ = kAt;
}

void Bridge::resize(float width) noexcept {
    touch_.resize(width);
}

void Bridge::gamepadAxis(const window::Event& event) {
    Pad& pad = *std::ranges::find(pads_, event.gamepad, &Pad::gamepad);
    const auto kAxis = static_cast<window::GamepadAxis>(event.gamepadInput.control);
    const float kValue = event.gamepadInput.value;
    const auto kSubmit = [&](std::string_view name, float x, float y) {
        feed_->submit(
            {.device = pad.device, .control = *input::controlNamed(input::DeviceClass::Gamepad, name), .x = x, .y = y});
    };
    switch (kAxis) {
    case window::GamepadAxis::StickLeftX:
    case window::GamepadAxis::StickLeftY:
    case window::GamepadAxis::StickRightX:
    case window::GamepadAxis::StickRightY: {
        const auto kIndex = static_cast<std::size_t>(kAxis);
        pad.sticks[kIndex] = kValue;
        const std::size_t kFirst = kIndex & ~std::size_t{1};
        kSubmit(kFirst == 0 ? "stick_left" : "stick_right", pad.sticks[kFirst], -pad.sticks[kFirst + 1]);
        break;
    }
    case window::GamepadAxis::TriggerLeft:
        kSubmit("trigger_left", kValue, 0);
        break;
    case window::GamepadAxis::TriggerRight:
        kSubmit("trigger_right", kValue, 0);
        break;
    }
}

void Bridge::feel(window::Windows& windows) {
    feed_->takeFelt(felt_);
    for (const input::HapticCommand& command : felt_) {
        const auto kPad = std::ranges::find(pads_, command.device, &Pad::device);
        const float kAmplitude = std::clamp(command.haptic.amplitude, 0.0F, 1.0F);
        const float kFrequency = command.haptic.frequency;
        const float kLow = kFrequency == 0 || kFrequency < kLowMotorHertz ? kAmplitude : 0.0F;
        const float kHigh = kFrequency == 0 || kFrequency >= kLowMotorHertz ? kAmplitude : 0.0F;
        if (kPad == pads_.end() ||
            !windows.rumble(kPad->gamepad, kLow, kHigh, command.haptic.milliseconds).has_value()) {
            ++unfelt_;
        }
    }
}

} // namespace rawframe::input_window
