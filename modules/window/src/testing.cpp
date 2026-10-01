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

namespace {

/// A full HD monitor at the origin, of `facts`.
mwinMonitorInfo monitorOf(const DisplayFacts& facts) noexcept {
    mwinMonitorInfo info{};
    info.bounds = {.x = 0, .y = 0, .width = 1920, .height = 1080};
    info.workArea = info.bounds;
    info.scale = 1;
    info.primary = true;
    info.hdr = {.known = facts.reported,
                .active = facts.hdrOn,
                .peakNits = facts.peakNits,
                .fullFrameNits = 0,
                .sdrWhiteNits = facts.sdrWhiteNits};
    return info;
}

} // namespace

result::Result<MonitorId> addMonitor(Windows& windows, const DisplayFacts& facts) {
    const mwinMonitorInfo kInfo = monitorOf(facts);
    mwinMonitorId monitor{};
    RAWFRAME_TRY(checked(mwinTestAddMonitor(Platform::contextOf(windows), &kInfo, &monitor), "a test monitor"));
    return fromMaul(monitor);
}

result::Status changeMonitor(Windows& windows, MonitorId monitor, const DisplayFacts& facts) {
    const mwinMonitorInfo kInfo = monitorOf(facts);
    return checked(mwinTestChangeMonitor(Platform::contextOf(windows), toMaul(monitor), &kInfo),
                   "changing a test monitor");
}

result::Result<GamepadId> addGamepad(Windows& windows) {
    mwinGamepadInfo info{};
    info.mapped = true;
    info.capabilities = mwin_padRumble;
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

result::Result<Rumble> rumbleOf(Windows& windows, GamepadId gamepad) {
    Rumble rumble;
    RAWFRAME_TRY(checked(mwinTestGetRumble(Platform::contextOf(windows),
                                           toMaul(gamepad),
                                           &rumble.low,
                                           &rumble.high,
                                           &rumble.milliseconds,
                                           &rumble.count),
                         "a test gamepad's rumble"));
    return rumble;
}

} // namespace rawframe::window::testing
