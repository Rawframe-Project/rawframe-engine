#include "platform.h"

#include <maul-window/input.h>
#include <maul-window/window.h>
#include <utility>

namespace rawframe::window {

namespace {

mwinResult startProgram(mwinContext* context, void* user) {
    auto& platform = *static_cast<Platform*>(user);
    platform.context = context;
    platform.started = platform.program->start(platform.windows);
    // Any status but success skips the frames; which one does not matter,
    // since run returns start's own Error.
    return platform.started.has_value() ? mwin_success : mwin_errorState;
}

mwinFrameResult frameProgram(mwinContext* /*context*/, void* user) {
    auto& platform = *static_cast<Platform*>(user);
    return platform.program->frame(platform.windows) == FrameOutcome::Stop ? mwin_frameStop : mwin_frameContinue;
}

void stopProgram(mwinContext* /*context*/, mwinResult /*status*/, void* user) {
    auto& platform = *static_cast<Platform*>(user);
    platform.program->stop(platform.windows, platform.started);
    platform.context = nullptr;
}

mwinLimits toMaul(const Limits& limits, mwinLimits defaults) noexcept {
    defaults.windows = limits.windows;
    defaults.requestsPerWindow = limits.requestsPerWindow;
    defaults.notificationsPerWindow = limits.notificationsPerWindow;
    defaults.titleBytes = limits.titleBytes;
    defaults.inputPerWindow = limits.inputPerWindow;
    defaults.textBytesPerWindow = limits.textBytesPerWindow;
    defaults.monitors = limits.monitors;
    defaults.gamepads = limits.gamepads;
    defaults.droppedFiles = limits.droppedFiles;
    defaults.dropBytes = limits.dropBytes;
    return defaults;
}

/// The id a request call gave, or its failure.
result::Result<RequestId> requested(mwinResult status, mwinRequestId request, std::string_view what) {
    if (status != mwin_success) {
        return failure(status, what);
    }
    return fromMaul(request);
}

mwinCursorMode toMaul(CursorMode mode) noexcept {
    switch (mode) {
    case CursorMode::Hidden:
        return mwin_cursorHidden;
    case CursorMode::Captured:
        return mwin_cursorCaptured;
    case CursorMode::Confined:
        return mwin_cursorConfined;
    case CursorMode::ConfinedHidden:
        return mwin_cursorConfinedHidden;
    case CursorMode::Visible:
        break;
    }
    return mwin_cursorVisible;
}

} // namespace

result::Status Platform::run(Program& program, const RunSettings& settings, mwinBackendKind backend) {
    Platform platform{program};
    mwinAppDef app = mwinDefaultAppDef();
    app.context.limits = toMaul(settings.limits, app.context.limits);
    app.context.backend = backend;
    app.init = startProgram;
    app.frame = frameProgram;
    app.quit = stopProgram;
    app.user = &platform;
    const mwinResult status = mwinRun(&app);
    if (!platform.started.has_value()) {
        return std::unexpected{std::move(platform.started).error()};
    }
    if (status != mwin_success) {
        return failure(status, "the window system did not run");
    }
    return {};
}

result::Status run(Program& program, const RunSettings& settings) {
    return Platform::run(program, settings, mwin_backendNative);
}

result::Result<WindowId> Windows::create(const WindowSettings& settings) {
    mwinWindowDef def = mwinDefaultWindowDef();
    def.title = settings.title.data();
    def.titleLength = settings.title.size();
    def.size = {.width = settings.size.width, .height = settings.size.height};
    def.mode = toMaul(settings.mode);
    def.visible = settings.visible;
    def.style = static_cast<mwinWindowStyle>(mwin_styleDecorated | (settings.resizable ? mwin_styleResizable : 0));
    mwinWindowId window{};
    const mwinResult status = mwinCreateWindow(platform_->context, &def, &window, nullptr);
    if (status != mwin_success) {
        return failure(status, "a window was not created");
    }
    return fromMaul(window);
}

result::Status Windows::destroy(WindowId window) {
    const mwinResult status = mwinDestroyWindow(platform_->context, toMaul(window));
    if (status != mwin_success) {
        return failure(status, "a window was not destroyed");
    }
    return {};
}

result::Result<WindowState> Windows::state(WindowId window) const {
    mwinWindowState state{};
    const mwinResult status = mwinGetWindowState(platform_->context, toMaul(window), &state);
    if (status != mwin_success) {
        return failure(status, "a window's state");
    }
    WindowState out;
    out.size = {.width = state.size.width, .height = state.size.height};
    out.pixelSize = {.width = state.pixelSize.width, .height = state.pixelSize.height};
    out.scale = state.scale;
    out.position = {.x = state.position.x, .y = state.position.y};
    switch (state.mode) {
    case mwin_modeBorderlessFullscreen:
        out.mode = Mode::BorderlessFullscreen;
        break;
    case mwin_modeMinimized:
        out.mode = Mode::Minimized;
        break;
    case mwin_modeMaximized:
        out.mode = Mode::Maximized;
        break;
    default:
        out.mode = Mode::Windowed;
        break;
    }
    out.created = state.created;
    out.visible = state.visible;
    out.focused = state.focused;
    out.occluded = state.occluded;
    out.surfaceLost = state.surfaceLost;
    out.textInput = state.textInput;
    out.composing = state.composing;
    out.monitor = {.index = state.monitor.index1, .generation = state.monitor.generation};
    out.surfaceGeneration = state.surfaceGeneration;
    return out;
}

result::Result<RequestId> Windows::requestTitle(WindowId window, std::string_view title) {
    mwinRequestId request{};
    return requested(mwinRequestTitle(platform_->context, toMaul(window), title.data(), title.size(), &request),
                     request,
                     "a title request");
}

result::Result<RequestId> Windows::requestSize(WindowId window, LogicalSize size) {
    mwinRequestId request{};
    return requested(
        mwinRequestSize(
            platform_->context, toMaul(window), mwinSize{.width = size.width, .height = size.height}, &request),
        request,
        "a size request");
}

result::Result<RequestId> Windows::requestPosition(WindowId window, Position position) {
    mwinRequestId request{};
    return requested(mwinRequestPosition(
                         platform_->context, toMaul(window), mwinPosition{.x = position.x, .y = position.y}, &request),
                     request,
                     "a position request");
}

result::Result<RequestId> Windows::requestMode(WindowId window, Mode mode) {
    mwinRequestId request{};
    return requested(
        mwinRequestMode(platform_->context, toMaul(window), toMaul(mode), &request), request, "a mode request");
}

result::Result<RequestId> Windows::requestVisible(WindowId window, bool visible) {
    mwinRequestId request{};
    return requested(
        mwinRequestVisible(platform_->context, toMaul(window), visible, &request), request, "a visibility request");
}

result::Result<RequestId> Windows::requestFocus(WindowId window) {
    mwinRequestId request{};
    return requested(mwinRequestFocus(platform_->context, toMaul(window), &request), request, "a focus request");
}

result::Result<RequestId> Windows::requestCursorMode(WindowId window, CursorMode mode) {
    mwinRequestId request{};
    return requested(mwinRequestCursorMode(platform_->context, toMaul(window), toMaul(mode), &request),
                     request,
                     "a cursor mode request");
}

result::Result<RequestId> Windows::requestTextInput(WindowId window, bool enabled, Rect caret) {
    mwinRequestId request{};
    return requested(
        mwinRequestTextInput(platform_->context,
                             toMaul(window),
                             enabled,
                             mwinRect{.x = caret.x, .y = caret.y, .width = caret.width, .height = caret.height},
                             &request),
        request,
        "a text input request");
}

std::optional<Event> Windows::next() {
    mwinEvent record{};
    while (mwinNextEvent(platform_->context, &record) == mwin_success) {
        if (std::optional<Event> event = fromMaul(*platform_->context, record)) {
            return event;
        }
    }
    return std::nullopt;
}

} // namespace rawframe::window
