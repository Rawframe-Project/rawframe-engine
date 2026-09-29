#include "rawframe/window/testing.h"

#include "platform.h"

#include <maul-window/test.h>

namespace rawframe::window::testing {

namespace {

result::Status checked(mwinResult status, std::string_view what) {
    if (status != mwin_success) {
        return failure(status, what);
    }
    return {};
}

} // namespace

result::Status run(Program& program, const RunSettings& settings) {
    return Platform::run(program, settings, mwin_backendTest);
}

result::Status post(Windows& windows, const Event& event) {
    RAWFRAME_TRY_ASSIGN(const mwinEvent record, toMaul(event));
    return checked(mwinTestPost(Platform::contextOf(windows), &record), "a test report");
}

result::Status drop(Windows& windows, WindowId window, Position position, std::string_view paths) {
    return checked(mwinTestDrop(Platform::contextOf(windows),
                                toMaul(window),
                                mwinPosition{.x = position.x, .y = position.y},
                                paths.data(),
                                paths.size(),
                                nullptr,
                                0),
                   "a test drop");
}

result::Status hold(Windows& windows, bool held) {
    return checked(mwinTestHold(Platform::contextOf(windows), held), "holding test requests");
}

result::Status answer(Windows& windows, RequestKind kind, Outcome outcome) {
    return checked(mwinTestSetAnswer(Platform::contextOf(windows), toMaul(kind), toMaul(outcome)), "a test answer");
}

result::Status setTime(Windows& windows, std::uint64_t timeNs) {
    return checked(mwinTestSetTime(Platform::contextOf(windows), timeNs), "the test clock");
}

result::Result<GamepadId> addGamepad(Windows& windows) {
    mwinGamepadInfo info{};
    info.mapped = true;
    info.battery = -1;
    mwinGamepadId gamepad{};
    RAWFRAME_TRY(checked(mwinTestAddGamepad(Platform::contextOf(windows), &info, &gamepad), "a test gamepad"));
    return fromMaul(gamepad);
}

result::Status removeGamepad(Windows& windows, GamepadId gamepad) {
    return checked(mwinTestRemoveGamepad(Platform::contextOf(windows), toMaul(gamepad)), "removing a test gamepad");
}

result::Status pressGamepad(Windows& windows, GamepadId gamepad, GamepadButton button, bool down) {
    return checked(
        mwinTestGamepadButton(Platform::contextOf(windows), toMaul(gamepad), static_cast<std::uint8_t>(button), down),
        "a test gamepad button");
}

result::Status moveGamepad(Windows& windows, GamepadId gamepad, GamepadAxis axis, float value) {
    return checked(
        mwinTestGamepadAxis(Platform::contextOf(windows), toMaul(gamepad), static_cast<std::uint8_t>(axis), value),
        "a test gamepad axis");
}

} // namespace rawframe::window::testing
