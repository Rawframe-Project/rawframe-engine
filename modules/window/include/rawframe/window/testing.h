#pragma once

// A headless window system for tests, built only with the tests
// (RAWFRAME_BUILD_TESTS); a shipped build has no such platform to fall back
// to. It answers each request at the next frame as told, and takes what a
// real platform would report from `post`, through the same path a real
// report takes.

#include "rawframe/result/result.h"
#include "rawframe/window/events.h"
#include "rawframe/window/windows.h"

#include <cstdint>
#include <string_view>

namespace rawframe::window::testing {

/// `run`, on the test platform.
[[nodiscard]] result::Status run(Program& program, const RunSettings& settings);

/// Reports what a platform would: a key went down, the user asked to
/// close, focus moved, the application is suspending. The record reaches
/// the stream at the next frame. Records only the window system makes
/// (creation, destruction, completions, input state resets) are Invalid,
/// and so are drops, gamepads, and monitors, which have their own calls.
[[nodiscard]] result::Status post(Windows& windows, const Event& event);

/// Drops files on a window: `paths` holds each path ended by a NUL.
[[nodiscard]] result::Status drop(Windows& windows, WindowId window, Position position, std::string_view paths);

/// Holds every request unanswered, or answers them again from the next
/// frame.
[[nodiscard]] result::Status hold(Windows& windows, bool held);

/// How the test platform answers requests of a kind from now on; Done at
/// first. Superseded and Cancelled are the window system's own answers.
[[nodiscard]] result::Status answer(Windows& windows, RequestKind kind, Outcome outcome);

/// Sets the clock that stamps every record; it never goes back.
[[nodiscard]] result::Status setTime(Windows& windows, std::uint64_t timeNs);

/// Connects a mapped gamepad at once.
[[nodiscard]] result::Result<GamepadId> addGamepad(Windows& windows);
[[nodiscard]] result::Status removeGamepad(Windows& windows, GamepadId gamepad);
[[nodiscard]] result::Status pressGamepad(Windows& windows, GamepadId gamepad, GamepadButton button, bool down);
[[nodiscard]] result::Status moveGamepad(Windows& windows, GamepadId gamepad, GamepadAxis axis, float value);

} // namespace rawframe::window::testing
