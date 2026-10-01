// A window's raw input reaching actions as a player's would: keys by HID
// usage, mouse buttons, the wheel, and captured motion; a gamepad's two
// stick axes combined and turned up positive; the cursor's place as the
// mouse's pointer (D367); focus loss letting go of everything; a gamepad
// that goes letting go of what it held; a haptic output felt through a
// gamepad's motors (D251); touches as the touch screen's halves, split by
// the window's width (D387).

#include "rawframe/input/mapper.h"
#include "rawframe/input_window/bridge.h"
#include "rawframe/test/test.h"
#include "rawframe/window/testing.h"

#include <cmath>
#include <cstddef>
#include <memory>
#include <optional>
#include <string_view>
#include <tuple>
#include <vector>

using namespace rawframe;
using namespace rawframe::input;

namespace {

constexpr std::size_t kJump = 0;
constexpr std::size_t kFire = 1;
constexpr std::size_t kZoom = 2;
constexpr std::size_t kLook = 3;
constexpr std::size_t kMove = 4;
constexpr std::size_t kAim = 5;
constexpr std::size_t kDash = 6;
constexpr std::size_t kSteer = 7;

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
    Action aim{.id = 6, .name = "aim", .type = ValueType::Axis2D};
    aim.bindings = {single(DeviceClass::Mouse, "pointer")};
    Action dash{.id = 7, .name = "dash", .type = ValueType::Bool};
    dash.bindings = {single(DeviceClass::Touch, "right")};
    Action steer{.id = 8, .name = "steer", .type = ValueType::Axis2D};
    steer.bindings = {single(DeviceClass::Touch, "stick_left")};
    set.actions = {jump, fire, zoom, look, move, aim, dash, steer};
    set.contexts.push_back(
        Context{.id = 10, .name = "play", .actions = {kJump, kFire, kZoom, kLook, kMove, kAim, kDash, kSteer}});
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

RAWFRAME_TEST(TheCursorsPlaceIsThePointerAndStaysWhereItWasLastTold) {
    Rig rig;
    window::Event moved = record(window::EventKind::CursorMoved);
    moved.pointer.position = {.x = 640, .y = 360};
    rig.take(moved);
    rig.commit();
    // Logical pixels, y down, neither clamped nor turned up positive.
    RAWFRAME_EXPECT(near(rig.state(kAim).x, 640) && near(rig.state(kAim).y, 360));
    // Not motion: a tick without a move keeps the place, and so does
    // focus going.
    rig.commit();
    RAWFRAME_EXPECT(near(rig.state(kAim).x, 640) && near(rig.state(kAim).y, 360));
    rig.take(record(window::EventKind::FocusLost));
    rig.commit();
    RAWFRAME_EXPECT(near(rig.state(kAim).x, 640) && near(rig.state(kAim).y, 360));
    moved.pointer.position = {.x = 2.5F, .y = 1200};
    rig.take(moved);
    rig.commit();
    RAWFRAME_EXPECT(near(rig.state(kAim).x, 2.5F) && near(rig.state(kAim).y, 1200));
    // The window's top left is a place too, not rest.
    moved.pointer.position = {.x = 0, .y = 0};
    rig.take(moved);
    rig.commit();
    RAWFRAME_EXPECT(near(rig.state(kAim).x, 0) && near(rig.state(kAim).y, 0));
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

namespace {

/// Connects a gamepad, feeds its arrival to the bridge, asks the player's
/// haptic output of it three ways, and reads the motors after each.
class Felt final : public window::Program {
public:
    result::Status start(window::Windows& windows) override {
        RAWFRAME_TRY_ASSIGN(pad, window::testing::addGamepad(windows));
        return {};
    }

    window::FrameOutcome frame(window::Windows& windows) override {
        while (std::optional<window::Event> event = windows.next()) {
            rig.take(*event);
        }
        rig.commit();
        if (step == felt.size()) {
            return window::FrameOutcome::Stop;
        }
        std::vector<HapticCommand> commands;
        rig.mapper->feel({}, 0, felt[step++], commands);
        for (const HapticCommand& command : commands) {
            rig.feed.feel(command);
        }
        // A command for a device the window system does not have.
        rig.feed.feel({.device = DeviceId{99}, .haptic = {.milliseconds = 10}});
        rig.bridge.feel(windows);
        rumbles.push_back(window::testing::rumbleOf(windows, pad).value_or(window::testing::Rumble{}));
        return window::FrameOutcome::Continue;
    }

    void stop(window::Windows& /*windows*/, const result::Status& /*status*/) override {
    }

    Rig rig;
    window::GamepadId pad;
    std::vector<Haptic> felt;
    std::size_t step = 0;
    std::vector<window::testing::Rumble> rumbles;
};

} // namespace

RAWFRAME_TEST(AHapticOutputRunsAGamepadsMotors) {
    Felt program;
    ActionSet set = actions();
    set.haptics.push_back(HapticOutput{.id = 20, .name = "thump", .bindings = {HapticBinding{}}});
    program.rig.mapper = *Mapper::create(std::move(set), {.players = 1});
    RAWFRAME_EXPECT(program.rig.mapper->activate({}, 0).has_value());
    program.felt = {Haptic{.amplitude = 0.75F, .milliseconds = 120},
                    Haptic{.amplitude = 0.5F, .frequency = 60, .milliseconds = 90},
                    Haptic{.amplitude = 2.0F, .frequency = 320, .milliseconds = 40}};
    RAWFRAME_EXPECT(window::testing::run(program, window::RunSettings{}).has_value());
    RAWFRAME_EXPECT(program.rumbles.size() == 3);
    if (program.rumbles.size() == 3) {
        // No frequency: both motors. A low one: the heavy motor. A high
        // one, its amplitude held to one: the light motor.
        const auto& [kBoth, kLow, kHigh] = std::tie(program.rumbles[0], program.rumbles[1], program.rumbles[2]);
        RAWFRAME_EXPECT(kBoth.low == 0.75F && kBoth.high == 0.75F && kBoth.milliseconds == 120 && kBoth.count == 1);
        RAWFRAME_EXPECT(kLow.low == 0.5F && kLow.high == 0 && kLow.milliseconds == 90 && kLow.count == 2);
        RAWFRAME_EXPECT(kHigh.low == 0 && kHigh.high == 1.0F && kHigh.milliseconds == 40 && kHigh.count == 3);
    }
    RAWFRAME_EXPECT(program.rig.bridge.unfelt() == 3);
}

RAWFRAME_TEST(TouchesAreTheTouchScreensHalves) {
    Rig rig;
    const auto kTouch = [&rig](window::EventKind kind, std::uint64_t id, float x, float y) {
        window::Event touch = record(kind);
        touch.touch = {.id = id, .position = {.x = x, .y = y}};
        rig.take(touch);
    };
    window::Event resized = record(window::EventKind::Resized);
    resized.size = {.width = 1000, .height = 600};
    rig.take(resized);
    kTouch(window::EventKind::TouchDown, 1, 100, 400);
    kTouch(window::EventKind::TouchMoved, 1, 132, 400);
    kTouch(window::EventKind::TouchDown, 2, 800, 300);
    rig.commit();
    RAWFRAME_EXPECT(near(rig.state(kSteer).x, 0.5F) && rig.state(kDash).on);
    // Cancelled as ended; focus lost lets go of the other.
    kTouch(window::EventKind::TouchCancelled, 1, 0, 0);
    rig.commit();
    RAWFRAME_EXPECT(near(rig.state(kSteer).x, 0) && rig.state(kDash).on);
    rig.take(record(window::EventKind::FocusLost));
    rig.commit();
    RAWFRAME_EXPECT(!rig.state(kDash).on);
    // A touch after begins afresh in its half.
    kTouch(window::EventKind::TouchDown, 3, 900, 100);
    rig.commit();
    RAWFRAME_EXPECT(rig.state(kDash).on);
}
