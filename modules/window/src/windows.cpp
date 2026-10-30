#include "platform.h"

#include <maul-window/accessibility.h>
#include <maul-window/clipboard.h>
#include <maul-window/dialog.h>
#include <maul-window/gamepad.h>
#include <maul-window/input.h>
#include <maul-window/monitor.h>
#include <maul-window/native.h>
#include <maul-window/services.h>
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
    auto* const platform = static_cast<Platform*>(user);
    platform->program->stop(platform->windows, platform->started);
    platform->context = nullptr;
    if (platform->outlivesRun) {
        delete platform;
    }
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
/// A request's identity once the call that made it has returned: the status
/// is taken first, since arguments are evaluated in no fixed order and on
/// Windows' ABI the last first.
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

mwinAppDef Platform::define(Platform& platform, const RunSettings& settings, mwinBackendKind backend) {
    mwinAppDef app = mwinDefaultAppDef();
    app.context.limits = toMaul(settings.limits, app.context.limits);
    app.context.backend = backend;
    app.init = startProgram;
    app.frame = frameProgram;
    app.quit = stopProgram;
    app.user = &platform;
    return app;
}

result::Status Platform::run(Program& program, const RunSettings& settings, mwinBackendKind backend) {
    auto platform = std::make_unique<Platform>(program);
    const mwinAppDef kApp = define(*platform, settings, backend);
    const mwinResult status = mwinRun(&kApp);
    if (platform->context != nullptr) {
        // The page's frames run the program on; its stop frees this.
        platform->outlivesRun = true;
        static_cast<void>(platform.release());
        return {};
    }
    if (!platform->started.has_value()) {
        return std::unexpected{std::move(platform->started).error()};
    }
    if (status != mwin_success) {
        return failure(status, "the window system did not run");
    }
    return {};
}

result::Status run(Program& program, const RunSettings& settings) {
    return Platform::run(program, settings, mwin_backendNative);
}

} // namespace rawframe::window

#if defined(__ANDROID__)
// Maul Window's entry on Android (its context.h): the program its library
// gives, run on a Platform that its stop frees, as the web's is.
mwinAppDef mwinAndroidMain(void) {
    using rawframe::window::Platform;
    const rawframe::window::AndroidStart kStart = rawframe::window::androidStart();
    if (kStart.program == nullptr) {
        // No init: the activity cannot run it and finishes.
        return mwinDefaultAppDef();
    }
    auto platform = std::make_unique<Platform>(*kStart.program);
    platform->outlivesRun = true;
    return Platform::define(*platform.release(), kStart.settings, mwin_backendNative);
}
#endif

namespace rawframe::window {

result::Status showMessage(const Message& message) {
    mwinMessageBoxDef def = mwinDefaultMessageBoxDef();
    def.title = message.title.data();
    def.titleLength = message.title.size();
    def.message = message.text.data();
    def.messageLength = message.text.size();
    def.kind = message.kind == Message::Kind::Information ? mwin_messageInfo
               : message.kind == Message::Kind::Warning   ? mwin_messageWarning
                                                          : mwin_messageError;
    def.buttons = mwin_buttonsOk;
    if (const mwinResult kStatus = mwinShowMessageBox(&def, nullptr); kStatus != mwin_success) {
        return failure(kStatus, "a message box");
    }
    return {};
}

result::Result<WindowId> Windows::create(const WindowSettings& settings) {
    mwinWindowDef def = mwinDefaultWindowDef();
    def.title = settings.title.data();
    def.titleLength = settings.title.size();
    def.size = {.width = settings.size.width, .height = settings.size.height};
    def.mode = toMaul(settings.mode);
    def.visible = settings.visible;
    def.style = static_cast<mwinWindowStyle>(mwin_styleDecorated | (settings.resizable ? mwin_styleResizable : 0));
    if (settings.owner.valid()) {
        def.owner = toMaul(settings.owner);
    }
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
    const mwinResult status = mwinWindow_GetState(platform_->context, toMaul(window), &state);
    if (status != mwin_success) {
        return failure(status, "a window's state");
    }
    WindowState out;
    out.size = {.width = state.size.width, .height = state.size.height};
    out.pixelSize = {.width = state.pixelSize.width, .height = state.pixelSize.height};
    out.safeArea = {.top = state.safeArea.top,
                    .right = state.safeArea.right,
                    .bottom = state.safeArea.bottom,
                    .left = state.safeArea.left};
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
    const mwinResult kStatus =
        mwinWindow_RequestTitle(platform_->context, toMaul(window), title.data(), title.size(), &request);
    return requested(kStatus, request, "a title request");
}

result::Result<RequestId> Windows::requestSize(WindowId window, LogicalSize size) {
    mwinRequestId request{};
    const mwinResult kStatus = mwinWindow_RequestSize(
        platform_->context, toMaul(window), mwinSize{.width = size.width, .height = size.height}, &request);
    return requested(kStatus, request, "a size request");
}

result::Result<RequestId> Windows::requestPosition(WindowId window, Position position) {
    mwinRequestId request{};
    const mwinResult kStatus = mwinWindow_RequestPosition(
        platform_->context, toMaul(window), mwinPosition{.x = position.x, .y = position.y}, &request);
    return requested(kStatus, request, "a position request");
}

result::Result<RequestId> Windows::requestMode(WindowId window, Mode mode) {
    mwinRequestId request{};
    const mwinResult kStatus = mwinWindow_RequestMode(platform_->context, toMaul(window), toMaul(mode), &request);
    return requested(kStatus, request, "a mode request");
}

result::Result<RequestId> Windows::requestVisible(WindowId window, bool visible) {
    mwinRequestId request{};
    const mwinResult kStatus = mwinWindow_RequestVisible(platform_->context, toMaul(window), visible, &request);
    return requested(kStatus, request, "a visibility request");
}

result::Result<RequestId> Windows::requestFocus(WindowId window) {
    mwinRequestId request{};
    const mwinResult kStatus = mwinWindow_RequestFocus(platform_->context, toMaul(window), &request);
    return requested(kStatus, request, "a focus request");
}

result::Result<RequestId> Windows::requestCursorMode(WindowId window, CursorMode mode) {
    mwinRequestId request{};
    const mwinResult kStatus = mwinWindow_RequestCursorMode(platform_->context, toMaul(window), toMaul(mode), &request);
    return requested(kStatus, request, "a cursor mode request");
}

result::Result<RequestId> Windows::requestTextInput(WindowId window, bool enabled, Rect caret) {
    mwinRequestId request{};
    const mwinResult kStatus =
        mwinWindow_RequestTextInput(platform_->context,
                                    toMaul(window),
                                    enabled,
                                    mwinRect{.x = caret.x, .y = caret.y, .width = caret.width, .height = caret.height},
                                    &request);
    return requested(kStatus, request, "a text input request");
}

result::Result<RequestId> Windows::requestAccessibilityRoot(WindowId window, void* root) {
    mwinRequestId request{};
    const mwinResult kStatus = mwinWindow_RequestAccessibilityRoot(platform_->context, toMaul(window), root, &request);
    return requested(kStatus, request, "an accessibility root request");
}

result::Result<RequestId> Windows::requestFileDialog(WindowId window, const FileDialog& dialog) {
    mwinFileDialogDef def = mwinDefaultFileDialogDef();
    def.kind = dialog.kind == FileDialog::Kind::OpenMany ? mwin_dialogOpenMany
               : dialog.kind == FileDialog::Kind::Save   ? mwin_dialogSave
               : dialog.kind == FileDialog::Kind::Folder ? mwin_dialogFolder
                                                         : mwin_dialogOpen;
    def.title = dialog.title.data();
    def.titleLength = dialog.title.size();
    def.folder = dialog.folder.data();
    def.folderLength = dialog.folder.size();
    def.name = dialog.name.data();
    def.nameLength = dialog.name.size();
    std::vector<mwinFileFilter> filters;
    for (const auto& [kName, kExtensions] : dialog.filters) {
        filters.push_back(mwinFileFilter{.name = kName.data(),
                                         .nameLength = kName.size(),
                                         .extensions = kExtensions.data(),
                                         .extensionsLength = kExtensions.size()});
    }
    def.filters = filters.data();
    def.filterCount = static_cast<std::uint32_t>(filters.size());
    mwinRequestId request{};
    const mwinResult kStatus = mwinWindow_RequestFileDialog(platform_->context, toMaul(window), &def, &request);
    return requested(kStatus, request, "a file dialog request");
}

result::Result<RequestId> Windows::requestIcon(WindowId window, std::span<const IconImage> images) {
    std::vector<mwinIconImage> made;
    for (const IconImage& kImage : images) {
        made.push_back(mwinIconImage{.width = kImage.width,
                                     .height = kImage.height,
                                     .stride = static_cast<std::size_t>(kImage.width) * 4,
                                     .pixels = kImage.pixels.data()});
    }
    mwinRequestId request{};
    const mwinResult kStatus = mwinWindow_RequestIcon(
        platform_->context, toMaul(window), made.data(), static_cast<std::uint32_t>(made.size()), &request);
    return requested(kStatus, request, "an icon request");
}

result::Result<RequestId> Windows::requestClipboardWrite(WindowId window, std::string_view text) {
    mwinRequestId request{};
    const mwinResult kStatus =
        mwinWindow_RequestClipboardWrite(platform_->context, toMaul(window), text.data(), text.size(), &request);
    return requested(kStatus, request, "a clipboard write");
}

result::Result<RequestId> Windows::requestClipboardRead(WindowId window) {
    mwinRequestId request{};
    const mwinResult kStatus = mwinWindow_RequestClipboardRead(platform_->context, toMaul(window), &request);
    return requested(kStatus, request, "a clipboard read");
}

result::Result<std::string> Windows::clipboardText(RequestId request) const {
    const mwinRequestId kRequest{.index1 = request.index, .generation = request.generation};
    std::size_t length = 0;
    mwinResult status = mwinRequest_GetClipboardText(platform_->context, kRequest, nullptr, 0, &length);
    if (status != mwin_success && status != mwin_errorCapacity) {
        return failure(status, "the clipboard's text");
    }
    std::string text(length, '\0');
    status = mwinRequest_GetClipboardText(platform_->context, kRequest, text.data(), text.size(), &length);
    if (status != mwin_success) {
        return failure(status, "the clipboard's text");
    }
    text.resize(length);
    return text;
}

result::Result<std::vector<std::string>> Windows::dialogFiles(RequestId request) const {
    const mwinRequestId kRequest{.index1 = request.index, .generation = request.generation};
    std::size_t length = 0;
    mwinResult status = mwinRequest_GetDialogFiles(platform_->context, kRequest, nullptr, 0, &length, nullptr);
    if (status != mwin_success && status != mwin_errorCapacity) {
        return failure(status, "a file dialog's paths");
    }
    std::string bytes(length, '\0');
    status = mwinRequest_GetDialogFiles(platform_->context, kRequest, bytes.data(), bytes.size(), &length, nullptr);
    if (status != mwin_success) {
        return failure(status, "a file dialog's paths");
    }
    // Each path ended by a NUL.
    std::vector<std::string> paths;
    std::size_t from = 0;
    while (from < length) {
        const std::size_t kEnd = bytes.find('\0', from);
        const std::size_t kTo = kEnd == std::string::npos ? length : kEnd;
        if (kTo > from) {
            paths.emplace_back(bytes.substr(from, kTo - from));
        }
        from = kTo + 1;
    }
    return paths;
}

DisplayFacts Windows::display(WindowId window) const {
    mwinWindowState state{};
    if (mwinWindow_GetState(platform_->context, toMaul(window), &state) != mwin_success || state.monitor.index1 == 0) {
        return {};
    }
    mwinMonitorInfo info{};
    if (mwinMonitor_GetInfo(platform_->context, state.monitor, &info) != mwin_success || !info.hdr.known) {
        return {};
    }
    return DisplayFacts{.reported = true,
                        .hdrOn = info.hdr.active,
                        .peakNits = info.hdr.peakNits,
                        .sdrWhiteNits = info.hdr.sdrWhiteNits};
}

result::Result<HandleBundle> Windows::handles(WindowId window) const {
    mwinNativeHandles native{};
    const mwinResult status = mwinWindow_GetNativeHandles(platform_->context, toMaul(window), &native);
    if (status != mwin_success) {
        return failure(status, "a window's native handles");
    }
    HandleBundle bundle{.generation = native.surfaceGeneration};
    switch (native.platform) {
    case mwin_platformWin32:
        bundle.handles = Win32Handles{.window = native.handles.win32.hwnd, .instance = native.handles.win32.hinstance};
        break;
    case mwin_platformWayland:
        bundle.handles =
            WaylandHandles{.display = native.handles.wayland.display, .surface = native.handles.wayland.surface};
        break;
    case mwin_platformX11:
        bundle.handles = XcbHandles{.connection = native.handles.x11.connection, .window = native.handles.x11.window};
        break;
    case mwin_platformAndroid:
        bundle.handles = AndroidHandles{.window = native.handles.android.window,
                                        .activity = native.handles.android.activity,
                                        .view = native.handles.android.view};
        break;
    case mwin_platformMacOS:
    case mwin_platformIOS:
        bundle.handles = AppleHandles{.view = native.handles.apple.view, .layer = native.handles.apple.layer};
        break;
    case mwin_platformWeb:
        bundle.handles = CanvasHandles{
            .selector = std::string{native.handles.web.selector, native.handles.web.selectorLength},
            .accessibility = std::string{native.handles.web.accessibility, native.handles.web.accessibilityLength}};
        break;
    default:
        bundle.handles = TestHandles{};
        break;
    }
    return bundle;
}

result::Status Windows::rumble(GamepadId gamepad, float low, float high, std::uint32_t milliseconds) {
    const mwinResult status = mwinGamepad_SetRumble(platform_->context, toMaul(gamepad), low, high, milliseconds);
    if (status != mwin_success) {
        return failure(status, "a gamepad's rumble");
    }
    return {};
}

std::optional<Event> Windows::next() {
    mwinEvent record{};
    while (mwinContext_NextEvent(platform_->context, &record) == mwin_success) {
        if (std::optional<Event> event = fromMaul(*platform_->context, record)) {
            return event;
        }
    }
    return std::nullopt;
}

} // namespace rawframe::window
