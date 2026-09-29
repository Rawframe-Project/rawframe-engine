// A window's raw input reaching actions as a player's would: keys by HID
// usage, mouse buttons, the wheel, and captured motion; a gamepad's two
// stick axes combined and turned up positive; focus loss letting go of
// everything; a gamepad that goes letting go of what it held.

#include "rawframe/input/mapper.h"
#include "rawframe/input_window/bridge.h"
#include "rawframe/test/test.h"

#include <cmath>
#include <cstddef>
#include <memory>
#include <string_view>

using namespace rawframe;
using namespace rawframe::input;

namespace {

constexpr std::size_t kJump = 0;
constexpr std::size_t kFire = 1;
constexpr std::size_t kZoom = 2;
constexpr std::size_t kLook = 3;
constexpr std::size_t kMove = 4;

Binding single(DeviceClass device, std::string_view name) {
    Binding binding;
    binding.device = device;
    binding.controls[0] = *controlNamed(device, name);
    binding.deadzoneLower = 0;
    return binding;
}

ActionSet actions() {
    ActionSet set;
    Action jump{.id = 1, .name = "jump", .type = ValueType::Bool};
    jump.bindings = {single(DeviceClass::Keyboard, "space"), single(DeviceClass::Gamepad, "face_south")};
    Action fire{.id = 2, .name = "fire", .type = ValueType::Bool};
    fire.bindings = {single(DeviceClass::Mouse, "left")};
    Action zoom{.id = 3, .name = "zoom", .type = ValueType::Axis1D};
    zoom.bindings = {single(DeviceClass::Mouse, "wheel_y")};
    Action look{.id = 4, .name = "look", .type = ValueType::Axis2D};
    look.bindings = {single(DeviceClass::Mouse, "delta")};
    Action move{.id = 5, .name = "move", .type = ValueType::Axis2D};
    move.bindings = {single(DeviceClass::Gamepad, "stick_left")};
    set.actions = {jump, fire, zoom, look, move};
    set.contexts.push_back(Context{.id = 10, .name = "play", .actions = {kJump, kFire, kZoom, kLook, kMove}});
    return set;
}

window::Event record(window::EventKind kind) {
    window::Event event;
    event.kind = kind;
    event.window = window::WindowId{.index = 1, .generation = 1};
    return event;
}

window::Event key(window::EventKind kind, std::uint16_t usage, bool repeat = false) {
    window::Event event = record(kind);
    event.key = {.usage = usage, .repeat = repeat};
    return event;
}

window::Event pad(window::EventKind kind, window::GamepadId gamepad, std::uint8_t control, float value = 0) {
    window::Event event;
    event.kind = kind;
    event.gamepad = gamepad;
    event.gamepadInput = {.control = control, .value = value};
    return event;
}

struct Rig {
    Feed feed;
    input_window::Bridge bridge{feed};
    std::unique_ptr<Mapper> mapper = *Mapper::create(actions(), {.players = 1});
    std::uint64_t tick = 0;

    Rig() {
        RAWFRAME_EXPECT(mapper->activate({}, 0).has_value());
    }
    void take(const window::Event& event) {
        bridge.take(event);
    }
    void commit() {
        feed.deliver(*mapper, {});
        mapper->commit(++tick);
    }
    const ActionState& state(std::size_t action) const {
        return mapper->committed({}, action);
    }
};

bool near(float value, float expected) {
    return std::abs(value - expected) < 1e-5F;
}

} // namespace

RAWFRAME_TEST(KeysButtonsWheelAndMotionReachTheirActions) {
    Rig rig;
    rig.take(key(window::EventKind::KeyDown, 44));
    window::Event press = record(window::EventKind::ButtonDown);
    press.pointer.button = window::MouseButton::Left;
    rig.take(press);
    window::Event wheel = record(window::EventKind::Wheel);
    wheel.motion = {.x = 0, .y = 1};
    rig.take(wheel);
    window::Event motion = record(window::EventKind::RawPointerDelta);
    motion.motion = {.x = 3, .y = 4};
    rig.take(motion);
    // Records that are not input, and a usage no key has, change nothing.
    rig.take(record(window::EventKind::Resized));
    rig.take(key(window::EventKind::KeyDown, 0x68));
    rig.commit();
    RAWFRAME_EXPECT(rig.state(kJump).on && rig.state(kFire).on);
    RAWFRAME_EXPECT(near(rig.state(kZoom).x, 1));
    // Screen motion is down positive; actions read up positive.
    RAWFRAME_EXPECT(near(rig.state(kLook).x, 3) && near(rig.state(kLook).y, -4));
    // A repeat is not a second press, and the release lets go.
    rig.take(key(window::EventKind::KeyDown, 44, true));
    rig.commit();
    RAWFRAME_EXPECT(rig.state(kJump).on && !rig.mapper->pressedThisTick({}, kJump));
    RAWFRAME_EXPECT(near(rig.state(kLook).x, 0) && near(rig.state(kZoom).x, 0));
    rig.take(key(window::EventKind::KeyUp, 44));
    rig.commit();
    RAWFRAME_EXPECT(!rig.state(kJump).on && rig.state(kFire).on);
    // Focus goes: nothing stays held.
    rig.take(record(window::EventKind::FocusLost));
    rig.commit();
    RAWFRAME_EXPECT(!rig.state(kFire).on);
}

RAWFRAME_TEST(AGamepadsSticksCombineAndItsDepartureLetsGo) {
    Rig rig;
    constexpr window::GamepadId kPad{.index = 4, .generation = 1};
    constexpr window::GamepadId kStranger{.index = 5, .generation = 1};
    // A pad that never arrived is no device.
    rig.take(pad(
        window::EventKind::GamepadButtonDown, kStranger, static_cast<std::uint8_t>(window::GamepadButton::FaceSouth)));
    rig.commit();
    RAWFRAME_EXPECT(!rig.state(kJump).on);
    rig.take(pad(window::EventKind::GamepadAdded, kPad, 0));
    rig.take(
        pad(window::EventKind::GamepadButtonDown, kPad, static_cast<std::uint8_t>(window::GamepadButton::FaceSouth)));
    rig.take(pad(
        window::EventKind::GamepadAxisMoved, kPad, static_cast<std::uint8_t>(window::GamepadAxis::StickLeftX), 0.5F));
    rig.take(pad(
        window::EventKind::GamepadAxisMoved, kPad, static_cast<std::uint8_t>(window::GamepadAxis::StickLeftY), -0.25F));
    rig.commit();
    RAWFRAME_EXPECT(rig.state(kJump).on);
    // Pushed up (the window's negative y) reads positive.
    RAWFRAME_EXPECT(near(rig.state(kMove).x, 0.5F) && near(rig.state(kMove).y, 0.25F));
    rig.take(pad(window::EventKind::GamepadRemoved, kPad, 0));
    rig.commit();
    RAWFRAME_EXPECT(!rig.state(kJump).on && near(rig.state(kMove).x, 0) && near(rig.state(kMove).y, 0));
}
