#pragma once

// A window's native handles for one surface generation: SPEC-0025's handle
// bundle, the one opaque typed seam from the windows to the device
// (ADR-0033). Pointers the device side hands its GPU layer unread; nothing
// here names a window system's or a graphics API's types.

#include <cstdint>
#include <string>
#include <variant>

namespace rawframe::window {

/// A Win32 window: its HWND and HINSTANCE.
struct Win32Handles {
    void* window = nullptr;
    void* instance = nullptr;
};

/// A Wayland surface: its `wl_display*` and `wl_surface*`.
struct WaylandHandles {
    void* display = nullptr;
    void* surface = nullptr;
};

/// An X11 window through XCB: its `xcb_connection_t*` and `xcb_window_t`.
struct XcbHandles {
    void* connection = nullptr;
    std::uint32_t window = 0;
};

/// An Android window: its `ANativeWindow*`, the `ANativeActivity*` it
/// belongs to, through which the program reaches Java, and the global
/// reference to the activity's `android.view.View`, which hosts the
/// accessibility root (D576). All three change with the activity.
struct AndroidHandles {
    void* window = nullptr;
    void* activity = nullptr;
    void* view = nullptr;
};

/// A macOS or iOS view and its `CAMetalLayer`.
struct AppleHandles {
    void* view = nullptr;
    void* layer = nullptr;
};

/// A web page's canvas, by its CSS selector.
struct CanvasHandles {
    std::string selector;
};

/// The test platform's windows, which have nothing to draw into.
struct TestHandles {};

struct HandleBundle {
    /// The surface generation the handles are good for.
    std::uint32_t generation = 0;
    std::variant<TestHandles, Win32Handles, WaylandHandles, XcbHandles, AndroidHandles, AppleHandles, CanvasHandles>
        handles;
};

} // namespace rawframe::window
