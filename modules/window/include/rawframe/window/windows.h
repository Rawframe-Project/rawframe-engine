#pragma once

// The windows of a client process (SPEC-0025) and the loop that runs them.
// `run` hands the process's main thread to the window system. On desktops
// that call returns when the program stops; where the platform owns the
// loop (the web, mobile) it drives the same calls. The program cannot tell
// the two apart, so a client host iterates its Host once per frame and
// never blocks in one.
//
// Every change to a window is a request: the call returns its id at once,
// the platform's answer arrives later as notifications and then a
// RequestCompleted record. Nothing here calls back into the program except
// `run` itself, and only between frames.

#include "rawframe/result/result.h"
#include "rawframe/window/events.h"
#include "rawframe/window/handles.h"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace rawframe::window {

enum class CursorMode : std::uint8_t {
    Visible,
    Hidden,
    /// Locked and hidden; RawPointerDelta records report the motion.
    Captured,
    Confined,
    ConfinedHidden
};

struct WindowSettings {
    /// UTF-8, at most the title limit.
    std::string title;
    LogicalSize size{.width = 1280, .height = 720};
    Mode mode = Mode::Windowed;
    bool visible = true;
    bool resizable = true;
};

/// What the program has been told about a window so far: the values of the
/// notifications drained until now, never a guess at the platform's state.
struct WindowState {
    LogicalSize size;
    PixelSize pixelSize;
    float scale = 0;
    Position position;
    Mode mode = Mode::Windowed;
    /// WindowCreated was drained.
    bool created = false;
    bool visible = false;
    bool focused = false;
    bool occluded = false;
    /// Between SurfaceLost and SurfaceRestored.
    bool surfaceLost = false;
    /// The window accepts text (a TextInput request answered done).
    bool textInput = false;
    /// An input method composes.
    bool composing = false;
    /// The monitor that shows most of the window, zero before the platform
    /// says.
    MonitorId monitor;
    /// One more at creation and at each SurfaceRestored.
    std::uint32_t surfaceGeneration = 0;
};

/// The named limits (SPEC-0025's limit points). Each is a hard ceiling: a
/// request past one is refused with OverLimit, and discrete input past its
/// queue becomes InputStateReset. The values are the Maul Window defaults
/// until a presentation profile names its own.
struct Limits {
    std::uint16_t windows = 8;
    std::uint16_t requestsPerWindow = 32;
    std::uint16_t notificationsPerWindow = 256;
    std::uint16_t titleBytes = 1024;
    /// Per window and per class (discrete, motion, raw deltas, wheel).
    std::uint16_t inputPerWindow = 256;
    std::uint16_t textBytesPerWindow = 4096;
    std::uint16_t monitors = 16;
    std::uint16_t gamepads = 8;
    std::uint16_t droppedFiles = 256;
    std::uint32_t dropBytes = 1u << 20;
};

struct Platform;

/// The running window system, lent to the program for the length of one
/// of its calls. Main thread only, as every call of the program is.
class Windows {
public:
    Windows(const Windows&) = delete;
    Windows& operator=(const Windows&) = delete;

    /// Creates a window. Its id is good at once; WindowCreated follows when
    /// the platform has made it, then the request's completion.
    [[nodiscard]] result::Result<WindowId> create(const WindowSettings& settings);

    /// Destroys a window at once, after the program released its surfaces:
    /// the id goes stale, its requests complete as cancelled, and
    /// WindowDestroyed follows. Its undrained records are dropped.
    [[nodiscard]] result::Status destroy(WindowId window);

    [[nodiscard]] result::Result<WindowState> state(WindowId window) const;

    /// The window's native handles for its current surface generation:
    /// State while it has no surface (before WindowCreated, and between
    /// SurfaceLost and SurfaceRestored). For the seam to the device only
    /// (`Surfaces`), never to be kept past the generation.
    [[nodiscard]] result::Result<HandleBundle> handles(WindowId window) const;

    [[nodiscard]] result::Result<RequestId> requestTitle(WindowId window, std::string_view title);
    /// Answered by Resized and PixelSizeChanged with what the platform
    /// chose, which may differ.
    [[nodiscard]] result::Result<RequestId> requestSize(WindowId window, LogicalSize size);
    /// Wayland lets no program place its windows and answers Unsupported.
    [[nodiscard]] result::Result<RequestId> requestPosition(WindowId window, Position position);
    [[nodiscard]] result::Result<RequestId> requestMode(WindowId window, Mode mode);
    [[nodiscard]] result::Result<RequestId> requestVisible(WindowId window, bool visible);
    [[nodiscard]] result::Result<RequestId> requestFocus(WindowId window);
    [[nodiscard]] result::Result<RequestId> requestCursorMode(WindowId window, CursorMode mode);
    /// While a window accepts text, input methods compose into it and the
    /// keys they consume make no key records; `caret` places the candidate
    /// list. Ask again to move the caret.
    [[nodiscard]] result::Result<RequestId> requestTextInput(WindowId window, bool enabled, Rect caret);

    /// Runs a gamepad's motors, the heavy low-frequency one and the light
    /// high-frequency one, each from nought to one, for `milliseconds` or
    /// until asked again: the latest wins, and nought milliseconds stops
    /// them. Unsupported for a gamepad without motors, Stale for one gone.
    [[nodiscard]] result::Status rumble(GamepadId gamepad, float low, float high, std::uint32_t milliseconds);

    /// Takes the next record in the order the platform gave them, across
    /// windows; nothing once the frame's records are drained. A frame drains
    /// them all, or the next frame starts behind.
    [[nodiscard]] std::optional<Event> next();

private:
    friend struct Platform;
    explicit Windows(Platform& platform) noexcept : platform_(&platform) {
    }

    Platform* platform_;
};

enum class FrameOutcome : std::uint8_t {
    Continue,
    Stop
};

/// A client as the window system runs it.
class Program {
public:
    virtual ~Program() = default;

    /// Once, when the platform allows windows. A failure skips the frames
    /// and goes to stop with it.
    [[nodiscard]] virtual result::Status start(Windows& windows) = 0;
    /// Once a frame: drains the records, then does the frame's work.
    [[nodiscard]] virtual FrameOutcome frame(Windows& windows) = 0;
    /// Once at the end, with start's failure or success; the windows still
    /// exist.
    virtual void stop(Windows& windows, const result::Status& status) = 0;
};

struct RunSettings {
    Limits limits;
};

/// Runs a program on the platform's window system: start, the frames until
/// one asks to stop, then stop. Fails with Platform when there is no window
/// system to reach, OverLimit when the limits' storage cannot be had,
/// Invalid for a zero limit, and with start's failure. On the web it
/// returns success once start succeeded, while the page's frames run the
/// program on: the program outlives its stop, where its cleanup belongs.
/// Never from inside a running program.
[[nodiscard]] result::Status run(Program& program, const RunSettings& settings);

} // namespace rawframe::window
