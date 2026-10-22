// An editor's windows beside the one shown (D988): opened and closed as it
// asks, and what happens in each told to it by its key.

#include "rawframe/window_host/window_host.h"

#include <algorithm>
#include <cstdint>
#include <utility>

namespace rawframe::window_host {

namespace {

constexpr float kLeast = 160;
constexpr float kMost = 4096;

std::uint64_t valueOf(window::WindowId window) {
    return (std::uint64_t{window.index} << 32U) | window.generation;
}

} // namespace

void WindowHost::followAsides(window::Windows& windows) {
    // A window closed is destroyed once an iteration has run since its
    // surface was let go, so the device has released it.
    std::erase_if(asidesClosed_, [&](const std::pair<window::WindowId, std::uint64_t>& closed) {
        if (iterations_ <= closed.second) {
            return false;
        }
        static_cast<void>(windows.destroy(closed.first));
        return true;
    });
    for (view::WindowAsides::Asked& asked : asides_.asked()) {
        const auto kOpen =
            std::ranges::find(asideWindows_, asked.key, &std::pair<std::uint64_t, window::WindowId>::first);
        if (asked.close) {
            if (kOpen != asideWindows_.end()) {
                surfaces_.unwatch(kOpen->second);
                static_cast<void>(windows.requestVisible(kOpen->second, false));
                asidesClosed_.emplace_back(kOpen->second, iterations_);
                asideWindows_.erase(kOpen);
            }
            continue;
        }
        if (kOpen != asideWindows_.end()) {
            continue;
        }
        // Past the most open, or where the platform makes no other window
        // (the web, a phone), it closes as it opens.
        // Owned by the editor's window, kept above it as its panels are.
        const window::WindowSettings kMade{.title = std::move(asked.title),
                                           .size = {.width = std::clamp(asked.width, kLeast, kMost),
                                                    .height = std::clamp(asked.height, kLeast, kMost)},
                                           .owner = window_};
        const auto kWindow = asideWindows_.size() < view::WindowAsides::kMostOpen
                                 ? windows.create(kMade).value_or(window::WindowId{})
                                 : window::WindowId{};
        if (!kWindow.valid()) {
            asides_.happened({.key = asked.key, .kind = view::WindowAsides::Happened::Kind::Closed});
            continue;
        }
        if (asked.place.has_value()) {
            static_cast<void>(windows.requestPosition(kWindow, {.x = (*asked.place)[0], .y = (*asked.place)[1]}));
        }
        surfaces_.watch(kWindow);
        if (!settings_.icon.empty()) {
            static_cast<void>(windows.requestIcon(kWindow, settings_.icon));
        }
        asideWindows_.emplace_back(asked.key, kWindow);
        asides_.happened(
            {.key = asked.key, .kind = view::WindowAsides::Happened::Kind::Opened, .window = valueOf(kWindow)});
    }
}

bool WindowHost::aside(const window::Windows& windows, const window::Event& event) {
    if (!event.window.valid() || event.window == window_) {
        return false;
    }
    const auto kOpen =
        std::ranges::find(asideWindows_, event.window, &std::pair<std::uint64_t, window::WindowId>::second);
    if (kOpen == asideWindows_.end()) {
        // A window closed, whose last records still come.
        return true;
    }
    using Kind = view::WindowAsides::Happened::Kind;
    view::WindowAsides::Happened happened{
        .key = kOpen->first, .x = event.pointer.position.x, .y = event.pointer.position.y};
    // Where it is over the editor's own window, from where each window is.
    const auto kApart = windows.state(event.window);
    const auto kMain = windows.state(window_);
    if (kApart.has_value() && kMain.has_value()) {
        happened.mainX = happened.x + kApart->position.x - kMain->position.x;
        happened.mainY = happened.y + kApart->position.y - kMain->position.y;
    }
    switch (event.kind) {
    case window::EventKind::CursorMoved:
        happened.kind = Kind::Moved;
        break;
    case window::EventKind::ButtonDown:
        happened.kind = Kind::Pressed;
        happened.secondary = event.pointer.button == window::MouseButton::Right;
        break;
    case window::EventKind::ButtonUp:
        happened.kind = Kind::Released;
        break;
    case window::EventKind::Wheel:
        happened.kind = Kind::Wheeled;
        happened.deltaX = event.motion.x;
        happened.deltaY = event.motion.y;
        break;
    case window::EventKind::CursorLeft:
        happened.kind = Kind::Left;
        break;
    case window::EventKind::Moved:
    case window::EventKind::Resized:
        if (!kApart.has_value()) {
            return true;
        }
        // The record's own value, the other as last told.
        happened.kind = Kind::Placed;
        happened.x = event.kind == window::EventKind::Moved ? event.position.x : kApart->position.x;
        happened.y = event.kind == window::EventKind::Moved ? event.position.y : kApart->position.y;
        happened.width = event.kind == window::EventKind::Resized ? event.size.width : kApart->size.width;
        happened.height = event.kind == window::EventKind::Resized ? event.size.height : kApart->size.height;
        break;
    case window::EventKind::CloseRequested:
        happened.kind = Kind::Closed;
        asides_.close(kOpen->first);
        break;
    case window::EventKind::KeyDown:
    case window::EventKind::KeyUp:
    case window::EventKind::TextInput:
    case window::EventKind::ImePreedit:
        // Typed into the editor, as in its window.
        return false;
    default:
        return true;
    }
    asides_.happened(happened);
    return true;
}

} // namespace rawframe::window_host
