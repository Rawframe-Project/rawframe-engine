#include "platform.h"
#include "rawframe/window/errors.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <maul-window/drop.h>
#include <string>
#include <utility>

namespace rawframe::window {

namespace {

/// The engine's kinds and Maul Window's, one row each. A Maul Window type
/// with no row is outside SPEC-0025's vocabularies (D248).
constexpr auto kKinds = std::to_array<std::pair<EventKind, mwinEventType>>({
    {EventKind::WindowCreated, mwin_eventWindowCreated},
    {EventKind::CloseRequested, mwin_eventCloseRequested},
    {EventKind::WindowDestroyed, mwin_eventWindowDestroyed},
    {EventKind::Resized, mwin_eventResized},
    {EventKind::PixelSizeChanged, mwin_eventPixelSizeChanged},
    {EventKind::ScaleChanged, mwin_eventScaleChanged},
    {EventKind::Moved, mwin_eventMoved},
    {EventKind::FocusGained, mwin_eventFocusGained},
    {EventKind::FocusLost, mwin_eventFocusLost},
    {EventKind::Occluded, mwin_eventOccluded},
    {EventKind::Revealed, mwin_eventRevealed},
    {EventKind::ModeChanged, mwin_eventModeChanged},
    {EventKind::Shown, mwin_eventShown},
    {EventKind::Hidden, mwin_eventHidden},
    {EventKind::RequestCompleted, mwin_eventRequestCompleted},
    {EventKind::InputStateReset, mwin_eventInputStateReset},
    {EventKind::KeyDown, mwin_eventKeyDown},
    {EventKind::KeyUp, mwin_eventKeyUp},
    {EventKind::TextInput, mwin_eventTextInput},
    {EventKind::CursorMoved, mwin_eventCursorMoved},
    {EventKind::CursorEntered, mwin_eventCursorEntered},
    {EventKind::CursorLeft, mwin_eventCursorLeft},
    {EventKind::ButtonDown, mwin_eventButtonDown},
    {EventKind::ButtonUp, mwin_eventButtonUp},
    {EventKind::Wheel, mwin_eventWheel},
    {EventKind::RawPointerDelta, mwin_eventRawPointerDelta},
    {EventKind::TouchDown, mwin_eventTouchDown},
    {EventKind::TouchMoved, mwin_eventTouchMoved},
    {EventKind::TouchUp, mwin_eventTouchUp},
    {EventKind::TouchCancelled, mwin_eventTouchCancelled},
    {EventKind::ImePreedit, mwin_eventImePreedit},
    {EventKind::FilesDropped, mwin_eventDropped},
    {EventKind::Suspending, mwin_eventSuspending},
    {EventKind::Suspended, mwin_eventSuspended},
    {EventKind::Resuming, mwin_eventResuming},
    {EventKind::Resumed, mwin_eventResumed},
    {EventKind::SurfaceLost, mwin_eventSurfaceLost},
    {EventKind::SurfaceRestored, mwin_eventSurfaceRestored},
    {EventKind::MonitorAdded, mwin_eventMonitorAdded},
    {EventKind::MonitorRemoved, mwin_eventMonitorRemoved},
    {EventKind::DisplayChanged, mwin_eventDisplayChanged},
    {EventKind::AccessibilityRequested, mwin_eventAccessibilityRequested},
    {EventKind::GamepadAdded, mwin_eventGamepadAdded},
    {EventKind::GamepadRemoved, mwin_eventGamepadRemoved},
    {EventKind::GamepadButtonDown, mwin_eventGamepadButtonDown},
    {EventKind::GamepadButtonUp, mwin_eventGamepadButtonUp},
    {EventKind::GamepadAxisMoved, mwin_eventGamepadAxisMoved},
});

std::optional<EventKind> kindOf(mwinEventType type) noexcept {
    for (const auto& [kind, maul] : kKinds) {
        if (maul == type) {
            return kind;
        }
    }
    return std::nullopt;
}

std::optional<mwinEventType> typeOf(EventKind kind) noexcept {
    for (const auto& [engine, maul] : kKinds) {
        if (engine == kind) {
            return maul;
        }
    }
    return std::nullopt;
}

constexpr auto kRequests = std::to_array<std::pair<RequestKind, mwinRequestKind>>({
    {RequestKind::Create, mwin_requestCreate},
    {RequestKind::Title, mwin_requestTitle},
    {RequestKind::Size, mwin_requestSize},
    {RequestKind::Position, mwin_requestPosition},
    {RequestKind::Mode, mwin_requestMode},
    {RequestKind::Visible, mwin_requestVisible},
    {RequestKind::Focus, mwin_requestFocus},
    {RequestKind::CursorMode, mwin_requestCursorMode},
    {RequestKind::TextInput, mwin_requestTextInput},
    {RequestKind::AccessibilityRoot, mwin_requestAccessibilityRoot},
});

Mode modeOf(mwinWindowMode mode) noexcept {
    switch (mode) {
    case mwin_modeBorderlessFullscreen:
        return Mode::BorderlessFullscreen;
    case mwin_modeMinimized:
        return Mode::Minimized;
    case mwin_modeMaximized:
        return Mode::Maximized;
    default:
        return Mode::Windowed;
    }
}

Outcome outcomeOf(mwinOutcome outcome) noexcept {
    switch (outcome) {
    case mwin_outcomeDone:
        return Outcome::Done;
    case mwin_outcomeUnsupported:
        return Outcome::Unsupported;
    case mwin_outcomeDenied:
        return Outcome::Denied;
    case mwin_outcomeSuperseded:
        return Outcome::Superseded;
    case mwin_outcomeCancelled:
        return Outcome::Cancelled;
    default:
        return Outcome::Failed;
    }
}

MouseButton buttonOf(mwinMouseButton button) noexcept {
    switch (button) {
    case mwin_buttonLeft:
        return MouseButton::Left;
    case mwin_buttonRight:
        return MouseButton::Right;
    case mwin_buttonMiddle:
        return MouseButton::Middle;
    case mwin_buttonBack:
        return MouseButton::Back;
    case mwin_buttonForward:
        return MouseButton::Forward;
    default:
        return MouseButton::None;
    }
}

Position positionOf(mwinPosition position) noexcept {
    return {.x = position.x, .y = position.y};
}
mwinPosition toMaul(Position position) noexcept {
    return {.x = position.x, .y = position.y};
}

/// A drop's files, each path ended by a NUL. A drop of text alone is
/// outside the vocabulary; so is one whose files were replaced by a later
/// drop before its record was drained.
std::optional<Event> droppedFiles(const mwinContext& context, const mwinDropEvent& drop, Event event) {
    if (drop.fileCount == 0) {
        return std::nullopt;
    }
    std::size_t length = 0;
    mwinResult status = mwinGetDroppedFiles(&context, drop.drop, nullptr, 0, &length);
    if (status != mwin_errorCapacity && status != mwin_success) {
        return std::nullopt;
    }
    std::string paths(length, '\0');
    status = mwinGetDroppedFiles(&context, drop.drop, paths.data(), paths.size(), &length);
    if (status != mwin_success) {
        return std::nullopt;
    }
    paths.resize(length);
    std::size_t start = 0;
    while (start < paths.size()) {
        const std::size_t end = paths.find('\0', start);
        if (end == std::string::npos) {
            break;
        }
        event.paths.emplace_back(paths, start, end - start);
        start = end + 1;
    }
    event.truncated = drop.truncated;
    event.position = positionOf(drop.position);
    return event;
}

} // namespace

std::unexpected<result::Error> failure(mwinResult status, std::string_view what) {
    switch (status) {
    case mwin_errorStale:
        return result::fail(result::ErrorClass::NotFound, kWindowDomain, code(WindowError::Stale), what);
    case mwin_errorCapacity:
        return result::fail(result::ErrorClass::ResourceExhausted, kWindowDomain, code(WindowError::OverLimit), what);
    case mwin_errorUnsupported:
        return result::fail(result::ErrorClass::Unsupported, kWindowDomain, code(WindowError::Unsupported), what);
    case mwin_errorPlatform:
        return result::fail(result::ErrorClass::Unavailable, kWindowDomain, code(WindowError::Platform), what);
    case mwin_errorState:
        return result::fail(result::ErrorClass::FailedPrecondition, kWindowDomain, code(WindowError::State), what);
    default:
        return result::fail(result::ErrorClass::InvalidArgument, kWindowDomain, code(WindowError::Invalid), what);
    }
}

mwinWindowMode toMaul(Mode mode) noexcept {
    switch (mode) {
    case Mode::BorderlessFullscreen:
        return mwin_modeBorderlessFullscreen;
    case Mode::Minimized:
        return mwin_modeMinimized;
    case Mode::Maximized:
        return mwin_modeMaximized;
    case Mode::Windowed:
        break;
    }
    return mwin_modeWindowed;
}

mwinRequestKind toMaul(RequestKind kind) noexcept {
    for (const auto& [engine, maul] : kRequests) {
        if (engine == kind) {
            return maul;
        }
    }
    return mwin_requestCreate;
}

mwinOutcome toMaul(Outcome outcome) noexcept {
    switch (outcome) {
    case Outcome::Done:
        return mwin_outcomeDone;
    case Outcome::Unsupported:
        return mwin_outcomeUnsupported;
    case Outcome::Denied:
        return mwin_outcomeDenied;
    case Outcome::Superseded:
        return mwin_outcomeSuperseded;
    case Outcome::Cancelled:
        return mwin_outcomeCancelled;
    case Outcome::Failed:
        break;
    }
    return mwin_outcomeFailed;
}

std::optional<Event> fromMaul(const mwinContext& context, const mwinEvent& record) {
    const std::optional<EventKind> kind = kindOf(record.type);
    if (!kind) {
        return std::nullopt;
    }
    Event event;
    event.kind = *kind;
    event.window = fromMaul(record.window);
    event.timeNs = record.timeNs;
    event.samples = record.samples;
    const auto& data = record.data;
    switch (*kind) {
    case EventKind::Resized:
        event.size = {.width = data.size.width, .height = data.size.height};
        break;
    case EventKind::PixelSizeChanged:
        event.pixelSize = {.width = data.pixelSize.width, .height = data.pixelSize.height};
        break;
    case EventKind::ScaleChanged:
        event.scale = data.scale.scale;
        event.size = {.width = data.scale.suggestedSize.width, .height = data.scale.suggestedSize.height};
        break;
    case EventKind::Moved:
        event.position = positionOf(data.position);
        break;
    case EventKind::ModeChanged:
        event.mode = modeOf(data.mode);
        break;
    case EventKind::RequestCompleted: {
        const auto request =
            std::ranges::find(kRequests, data.completion.kind, &decltype(kRequests)::value_type::second);
        if (request == kRequests.end()) {
            return std::nullopt;
        }
        event.completion = {.request = fromMaul(data.completion.request),
                            .kind = request->first,
                            .outcome = outcomeOf(data.completion.outcome)};
        break;
    }
    case EventKind::InputStateReset:
        if (record.window.index1 == 0) {
            event.gamepad = fromMaul(data.gamepad);
        }
        break;
    case EventKind::KeyDown:
    case EventKind::KeyUp:
        event.key = {.usage = data.key.code,
                     .meaning = data.key.key,
                     .modifiers = data.key.modifiers,
                     .repeat = data.key.repeat};
        break;
    case EventKind::TextInput:
        event.text.assign(data.text.text, data.text.length);
        break;
    case EventKind::CursorMoved:
    case EventKind::CursorEntered:
    case EventKind::CursorLeft:
    case EventKind::ButtonDown:
    case EventKind::ButtonUp:
        event.pointer = {.position = positionOf(data.pointer.position),
                         .modifiers = data.pointer.modifiers,
                         .buttons = data.pointer.buttons,
                         .button = buttonOf(data.pointer.button),
                         .clicks = data.pointer.clicks};
        break;
    case EventKind::Wheel:
        event.motion = {.x = data.wheel.x, .y = data.wheel.y};
        break;
    case EventKind::RawPointerDelta:
        event.motion = {.x = data.delta.x, .y = data.delta.y};
        break;
    case EventKind::TouchDown:
    case EventKind::TouchMoved:
    case EventKind::TouchUp:
    case EventKind::TouchCancelled:
        event.touch = {
            .id = data.touch.id, .position = positionOf(data.touch.position), .pressure = data.touch.pressure};
        break;
    case EventKind::ImePreedit:
        event.text.assign(data.preedit.text, data.preedit.length);
        event.preedit = {.caret = data.preedit.caret,
                         .selectionStart = data.preedit.selectionStart,
                         .selectionEnd = data.preedit.selectionEnd};
        break;
    case EventKind::FilesDropped:
        return droppedFiles(context, data.drop, std::move(event));
    case EventKind::MonitorAdded:
    case EventKind::MonitorRemoved:
    case EventKind::DisplayChanged:
        event.monitor = {.index = data.monitor.index1, .generation = data.monitor.generation};
        break;
    case EventKind::GamepadAdded:
    case EventKind::GamepadRemoved:
        event.gamepad = fromMaul(data.gamepad);
        break;
    case EventKind::GamepadButtonDown:
    case EventKind::GamepadButtonUp:
        event.gamepad = fromMaul(data.gamepadButton.gamepad);
        event.gamepadInput = {.control = data.gamepadButton.button,
                              .raw = data.gamepadButton.raw,
                              .value = *kind == EventKind::GamepadButtonDown ? 1.0f : 0.0f};
        break;
    case EventKind::GamepadAxisMoved:
        event.gamepad = fromMaul(data.gamepadAxis.gamepad);
        event.gamepadInput = {
            .control = data.gamepadAxis.axis, .raw = data.gamepadAxis.raw, .value = data.gamepadAxis.value};
        break;
    default:
        break;
    }
    return event;
}

result::Result<mwinEvent> toMaul(const Event& event) {
    const std::optional<mwinEventType> type = typeOf(event.kind);
    if (!type) {
        return failure(mwin_errorInvalid, "a record no platform reports");
    }
    mwinEvent record{};
    record.type = *type;
    record.samples = event.samples;
    record.window = toMaul(event.window);
    record.timeNs = event.timeNs;
    auto& data = record.data;
    switch (event.kind) {
    case EventKind::Resized:
        data.size = {.width = event.size.width, .height = event.size.height};
        break;
    case EventKind::PixelSizeChanged:
        data.pixelSize = {.width = event.pixelSize.width, .height = event.pixelSize.height};
        break;
    case EventKind::ScaleChanged:
        data.scale = {.scale = event.scale, .suggestedSize = {.width = event.size.width, .height = event.size.height}};
        break;
    case EventKind::Moved:
        data.position = toMaul(event.position);
        break;
    case EventKind::ModeChanged:
        data.mode = toMaul(event.mode);
        break;
    case EventKind::KeyDown:
    case EventKind::KeyUp:
        data.key = {.code = event.key.usage,
                    .modifiers = event.key.modifiers,
                    .key = event.key.meaning,
                    .repeat = event.key.repeat};
        break;
    case EventKind::TextInput:
        data.text = {.text = event.text.data(), .length = static_cast<std::uint32_t>(event.text.size())};
        break;
    case EventKind::CursorMoved:
    case EventKind::CursorEntered:
    case EventKind::CursorLeft:
    case EventKind::ButtonDown:
    case EventKind::ButtonUp:
        data.pointer = {.position = toMaul(event.pointer.position),
                        .modifiers = event.pointer.modifiers,
                        .buttons = event.pointer.buttons,
                        .button = static_cast<mwinMouseButton>(event.pointer.button),
                        .clicks = event.pointer.clicks};
        break;
    case EventKind::Wheel:
        data.wheel = {.x = event.motion.x, .y = event.motion.y};
        break;
    case EventKind::RawPointerDelta:
        data.delta = {.x = event.motion.x, .y = event.motion.y};
        break;
    case EventKind::TouchDown:
    case EventKind::TouchMoved:
    case EventKind::TouchUp:
    case EventKind::TouchCancelled:
        data.touch = {.id = event.touch.id, .position = toMaul(event.touch.position), .pressure = event.touch.pressure};
        break;
    case EventKind::ImePreedit:
        data.preedit = {.text = event.text.data(),
                        .length = static_cast<std::uint32_t>(event.text.size()),
                        .caret = event.preedit.caret,
                        .selectionStart = event.preedit.selectionStart,
                        .selectionEnd = event.preedit.selectionEnd,
                        .segments = nullptr,
                        .segmentCount = 0};
        break;
    case EventKind::FilesDropped:
    case EventKind::MonitorAdded:
    case EventKind::MonitorRemoved:
    case EventKind::DisplayChanged:
    case EventKind::GamepadAdded:
    case EventKind::GamepadRemoved:
    case EventKind::GamepadButtonDown:
    case EventKind::GamepadButtonUp:
    case EventKind::GamepadAxisMoved:
        return failure(mwin_errorInvalid, "a record the test platform takes from its own call");
    default:
        break;
    }
    return record;
}

} // namespace rawframe::window
