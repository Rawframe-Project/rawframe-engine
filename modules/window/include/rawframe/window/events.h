#pragma once

// What a window system reports: SPEC-0025's closed notification and raw
// input vocabularies, each record owned and in the order the platform gave
// it. Positions and sizes are logical (the platform's scaled units) unless
// a type says pixels. Gamepads are here too, since Maul Window owns them
// (D126); their records are about no window. What Maul Window reports past
// these vocabularies (pens, drags in progress, system facts) waits for the
// amendment that admits it (D248).

#include <compare>
#include <cstdint>
#include <string>
#include <vector>

namespace rawframe::window {

/// A window, a monitor, a gamepad, or a request in flight, by an index and
/// the generation it was issued in; a stale one is refused on every use.
/// Runtime only: never saved, sent, or handed to scripts.
struct WindowId {
    std::uint32_t index = 0;
    std::uint32_t generation = 0;
    [[nodiscard]] bool valid() const noexcept {
        return index != 0;
    }
    friend constexpr auto operator<=>(const WindowId&, const WindowId&) noexcept = default;
};
struct MonitorId {
    std::uint32_t index = 0;
    std::uint32_t generation = 0;
    friend constexpr auto operator<=>(const MonitorId&, const MonitorId&) noexcept = default;
};
struct GamepadId {
    std::uint32_t index = 0;
    std::uint32_t generation = 0;
    [[nodiscard]] bool valid() const noexcept {
        return index != 0;
    }
    friend constexpr auto operator<=>(const GamepadId&, const GamepadId&) noexcept = default;
};
struct RequestId {
    std::uint32_t index = 0;
    std::uint32_t generation = 0;
    friend constexpr auto operator<=>(const RequestId&, const RequestId&) noexcept = default;
};

struct LogicalSize {
    float width = 0;
    float height = 0;
};
/// Distances in logical pixels from each edge of a window.
struct Insets {
    float top = 0;
    float right = 0;
    float bottom = 0;
    float left = 0;
};
struct PixelSize {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
};
struct Position {
    float x = 0;
    float y = 0;
};
struct Rect {
    float x = 0;
    float y = 0;
    float width = 0;
    float height = 0;
};

/// SPEC-0025's closed generation-1 modes; nothing is called exclusive.
enum class Mode : std::uint8_t {
    Windowed,
    BorderlessFullscreen,
    Minimized,
    Maximized
};

/// What a request asked.
enum class RequestKind : std::uint8_t {
    Create,
    Title,
    Size,
    Position,
    Mode,
    Visible,
    Focus,
    CursorMode,
    TextInput,
    AccessibilityRoot
};

/// How a request ended.
enum class Outcome : std::uint8_t {
    Done,
    Unsupported,
    Denied,
    /// A later request of the same kind on the same window replaced it.
    Superseded,
    /// Its window went first.
    Cancelled,
    Failed
};

struct Completion {
    RequestId request;
    RequestKind kind = RequestKind::Create;
    Outcome outcome = Outcome::Done;
};

/// Modifier keys and locks held, as bits.
enum class Modifier : std::uint16_t {
    Shift = 1,
    Control = 2,
    Alt = 4,
    Meta = 8,
    CapsLock = 16,
    NumLock = 32
};

/// A key going down or up: where it is, as its USB HID keyboard usage (the
/// place a W3C `code` names), what it means under the current layout (the
/// code point it types unmodified, or kNamedKey with the usage for a key
/// that types nothing), the modifiers held, and whether the platform
/// repeats it.
struct Key {
    std::uint16_t usage = 0;
    std::uint32_t meaning = 0;
    std::uint16_t modifiers = 0;
    bool repeat = false;
};
inline constexpr std::uint32_t kNamedKey = 0x40000000u;

enum class MouseButton : std::uint8_t {
    None,
    Left,
    Right,
    Middle,
    Back,
    Forward
};

/// The cursor over the window: where it is, the modifiers and buttons held
/// (bit b - 1 for button b), and for a button record the button and how
/// many quick clicks it completes.
struct Pointer {
    Position position;
    std::uint16_t modifiers = 0;
    std::uint8_t buttons = 0;
    MouseButton button = MouseButton::None;
    std::uint8_t clicks = 0;
};

/// Wheel detents (fractional for smooth wheels; positive y away from the
/// user, positive x to the right), or a captured pointer's unscaled motion
/// in the device's own units. The two are never merged.
struct Motion {
    float x = 0;
    float y = 0;
};

/// A touch, by an id stable from down to up or cancel; pressure from 0 to
/// 1, or -1 where the platform does not measure it.
struct Touch {
    std::uint64_t id = 0;
    Position position;
    float pressure = -1;
};

/// An input method's composition, whose text is the record's `text`: the
/// caret (-1 where hidden) and the selection, in bytes of the text. Empty
/// text ends the composition.
struct Preedit {
    std::int32_t caret = -1;
    std::uint32_t selectionStart = 0;
    std::uint32_t selectionEnd = 0;
};

/// A mapped gamepad's controls, by where they are, not what they say.
enum class GamepadButton : std::uint8_t {
    DpadUp,
    DpadDown,
    DpadLeft,
    DpadRight,
    FaceSouth,
    FaceEast,
    FaceWest,
    FaceNorth,
    ShoulderLeft,
    ShoulderRight,
    StickLeft,
    StickRight,
    Start,
    Select,
    Guide
};
enum class GamepadAxis : std::uint8_t {
    StickLeftX,
    StickLeftY,
    StickRightX,
    StickRightY,
    TriggerLeft,
    TriggerRight
};

/// A gamepad's button or axis. `raw` means the pad is not mapped and
/// `control` is the platform's own button or axis number; otherwise it is a
/// GamepadButton or GamepadAxis. Values are as the platform gave them: no
/// dead zone is applied here (ADR-0037).
struct GamepadInput {
    std::uint8_t control = 0;
    bool raw = false;
    float value = 0;
};

/// The closed record vocabulary: notifications, then raw input, then
/// gamepads.
enum class EventKind : std::uint8_t {
    None,
    WindowCreated,
    /// The user asked to close the window; nothing closes by itself.
    CloseRequested,
    /// The window is gone and its id stale.
    WindowDestroyed,
    Resized,
    PixelSizeChanged,
    /// With `scale`, and in `size` the size the platform suggests for it,
    /// which the program answers within the same frame.
    ScaleChanged,
    Moved,
    FocusGained,
    FocusLost,
    Occluded,
    Revealed,
    /// With `mode`: minimized, maximized, and restored are mode changes.
    ModeChanged,
    Shown,
    Hidden,
    /// With `completion`.
    RequestCompleted,
    /// Every key, button, and touch held is released: focus was lost, the
    /// application is suspending, or discrete input did not fit. About a
    /// gamepad, its controls are to be read again.
    InputStateReset,
    KeyDown,
    KeyUp,
    TextInput,
    CursorMoved,
    CursorEntered,
    CursorLeft,
    ButtonDown,
    ButtonUp,
    Wheel,
    RawPointerDelta,
    TouchDown,
    TouchMoved,
    TouchUp,
    TouchCancelled,
    ImePreedit,
    /// With `paths`.
    FilesDropped,
    Suspending,
    Suspended,
    Resuming,
    Resumed,
    SurfaceLost,
    SurfaceRestored,
    MonitorAdded,
    MonitorRemoved,
    /// The window moved to the monitor in `monitor`.
    DisplayChanged,
    /// An accessibility client first asked for the window's tree, on the
    /// platforms that say so (Win32, macOS, iOS, Android; D576).
    AccessibilityRequested,
    GamepadAdded,
    GamepadRemoved,
    GamepadButtonDown,
    GamepadButtonUp,
    GamepadAxisMoved
};

/// One owned record. The fields its kind names are set; the rest keep
/// their defaults.
struct Event {
    EventKind kind = EventKind::None;
    WindowId window;
    /// Nanoseconds on a monotonic clock, from the platform's own event time
    /// where it has one.
    std::uint64_t timeNs = 0;
    /// How many platform samples a motion record stands for: more than one
    /// when motion was merged for want of room.
    std::uint16_t samples = 1;

    LogicalSize size;
    PixelSize pixelSize;
    float scale = 0;
    Position position;
    Mode mode = Mode::Windowed;
    Completion completion;
    Key key;
    /// Typed or committed text, or a composition's; validated UTF-8.
    std::string text;
    Pointer pointer;
    Motion motion;
    Touch touch;
    Preedit preedit;
    /// A drop's files as validated UTF-8 paths: inert data that grant no
    /// access to them. `truncated` says some were left out for the limit.
    std::vector<std::string> paths;
    bool truncated = false;
    MonitorId monitor;
    GamepadId gamepad;
    GamepadInput gamepadInput;
};

} // namespace rawframe::window
