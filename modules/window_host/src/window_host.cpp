#include "rawframe/window_host/window_host.h"

#include "icon.h"
#include "rawframe/base/platform.h"
#include "rawframe/input_kest/sources.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <variant>

#if RAWFRAME_THREADS
#include <thread>
#endif
#if defined(__ANDROID__)
#include <android/native_activity.h>
#endif

#if defined(__APPLE__)
#include <TargetConditionals.h>
#endif

namespace rawframe::window_host {

namespace {

/// The accessibility an Apple system's view takes: UIKit's on iOS (D588),
/// AppKit's on macOS (D579).
#if defined(__APPLE__) && TARGET_OS_IPHONE
constexpr ui::AccessPlatform kAppleAccess = ui::AccessPlatform::UiKit;
#else
constexpr ui::AccessPlatform kAppleAccess = ui::AccessPlatform::AppKit;
#endif

// The most iterations one frame runs to catch up: a window hidden for a
// while is not replayed in one frame.
constexpr int kMostIterationsPerFrame = 4;
// The longest a desktop frame waits for the Host, since nothing else paces
// a window that draws nothing: input waits no longer than this to be seen.
// A page's frames are paced by the browser and never wait.
[[maybe_unused]] constexpr auto kLongestWait = std::chrono::milliseconds{4};
// The most iterations the events after a key wait for the players' input
// to read it (D506): a few world ticks at any rate a game runs.
constexpr int kMostIterationsAwaited = 16;

// USB HID keyboard usages of the keys a text field takes.
constexpr std::uint16_t kUsageA = 0x04;
constexpr std::uint16_t kUsageEnter = 0x28;
constexpr std::uint16_t kUsageEscape = 0x29;
constexpr std::uint16_t kUsageBackspace = 0x2A;
constexpr std::uint16_t kUsageTab = 0x2B;
constexpr std::uint16_t kUsageHome = 0x4A;
constexpr std::uint16_t kUsageDelete = 0x4C;
constexpr std::uint16_t kUsageEnd = 0x4D;
constexpr std::uint16_t kUsageRight = 0x4F;
constexpr std::uint16_t kUsageLeft = 0x50;
constexpr std::uint16_t kUsageDown = 0x51;
constexpr std::uint16_t kUsageUp = 0x52;
constexpr std::uint16_t kUsageKeypadEnter = 0x58;
constexpr std::uint16_t kUsageF11 = 0x44;
// And the keys a chord may end in (D777): the letters, the digits one to
// nine and nought, and the function keys one to twelve.
constexpr std::uint16_t kUsageZ = 0x1D;
constexpr std::uint16_t kUsage1 = 0x1E;
constexpr std::uint16_t kUsage0 = 0x27;
constexpr std::uint16_t kUsageF1 = 0x3A;
constexpr std::uint16_t kUsageF12 = 0x45;

bool held(const window::Key& key, window::Modifier modifier) noexcept {
    return (key.modifiers & static_cast<std::uint16_t>(modifier)) != 0;
}

/// What a key going down does to a text field, if anything: Control or
/// Option goes by word, Shift extends the selection, and Control or Command
/// with A selects everything.
std::optional<view::Typing> typingOf(const window::Key& key) {
    const bool kShortcut = held(key, window::Modifier::Control) || held(key, window::Modifier::Meta);
    view::Typing typing{.kind = view::Typing::Kind::Key,
                        .extend = held(key, window::Modifier::Shift),
                        .word = held(key, window::Modifier::Control) || held(key, window::Modifier::Alt)};
    switch (key.usage) {
    case kUsageBackspace:
        typing.key = view::TypingKey::Backspace;
        break;
    case kUsageDelete:
        typing.key = view::TypingKey::Delete;
        break;
    case kUsageLeft:
        typing.key = view::TypingKey::Left;
        break;
    case kUsageRight:
        typing.key = view::TypingKey::Right;
        break;
    case kUsageUp:
        typing.key = view::TypingKey::Up;
        break;
    case kUsageDown:
        typing.key = view::TypingKey::Down;
        break;
    case kUsageHome:
        typing.key = view::TypingKey::Home;
        break;
    case kUsageEnd:
        typing.key = view::TypingKey::End;
        break;
    case kUsageEnter:
    case kUsageKeypadEnter:
        typing.key = view::TypingKey::Submit;
        break;
    case kUsageTab:
        typing.key = view::TypingKey::Next;
        break;
    case kUsageEscape:
        typing.key = view::TypingKey::Dismiss;
        break;
    case kUsageA:
        if (!kShortcut) {
            return std::nullopt;
        }
        typing.key = view::TypingKey::SelectAll;
        break;
    default:
        return std::nullopt;
    }
    return typing;
}

/// A key going down as a chord (D777), if it is one: a letter or a digit
/// with Control or Command, Command read as Control so a shortcut is one
/// name on every desktop, or a function key with any modifiers but F11,
/// which the window keeps for its full screen. Control or Command with A
/// stays the field's select all.
std::optional<view::Typing> chordOf(const window::Key& key) {
    const bool kControl = held(key, window::Modifier::Control) || held(key, window::Modifier::Meta);
    std::string name;
    if (key.usage >= kUsageF1 && key.usage <= kUsageF12 && key.usage != kUsageF11) {
        name = "F" + std::to_string(key.usage - kUsageF1 + 1);
    } else if (kControl && key.usage > kUsageA && key.usage <= kUsageZ) {
        name = std::string(1, static_cast<char>('A' + (key.usage - kUsageA)));
    } else if (kControl && key.usage >= kUsage1 && key.usage <= kUsage0) {
        name = key.usage == kUsage0 ? "0" : std::string(1, static_cast<char>('1' + (key.usage - kUsage1)));
    } else {
        return std::nullopt;
    }
    const std::string kHeld = std::string{kControl ? "Ctrl+" : ""} +
                              (held(key, window::Modifier::Shift) ? "Shift+" : "") +
                              (held(key, window::Modifier::Alt) ? "Alt+" : "");
    return view::Typing{.kind = view::Typing::Kind::Chord, .text = kHeld + name};
}

/// A window's record as typing for the field that holds focus, if it is
/// any: a key that edits, typed or committed text, or a composition.
/// A letter going down with no modifier, as a chord of its capital.
std::optional<view::Typing> letterOf(const window::Event& event) {
    if (event.kind != window::EventKind::KeyDown || event.key.usage < kUsageA || event.key.usage > kUsageZ ||
        held(event.key, window::Modifier::Control) || held(event.key, window::Modifier::Meta) ||
        held(event.key, window::Modifier::Alt) || held(event.key, window::Modifier::Shift) || event.key.repeat) {
        return std::nullopt;
    }
    return view::Typing{.kind = view::Typing::Kind::Chord,
                        .text = std::string(1, static_cast<char>('A' + (event.key.usage - kUsageA)))};
}

std::optional<view::Typing> typingOf(const window::Event& event) {
    switch (event.kind) {
    case window::EventKind::KeyDown:
        if (auto chord = chordOf(event.key); chord.has_value()) {
            return chord;
        }
        return typingOf(event.key);
    case window::EventKind::TextInput:
        return view::Typing{.kind = view::Typing::Kind::Text, .text = event.text};
    case window::EventKind::ImePreedit: {
        view::Typing typing{.kind = view::Typing::Kind::Composition, .text = event.text, .caret = event.preedit.caret};
        // The platform tells its selection; it is drawn as the part being
        // converted, the rest as still to convert.
        if (event.preedit.selectionEnd > event.preedit.selectionStart) {
            typing.spans.push_back(view::TypingSpan{.start = event.preedit.selectionStart,
                                                    .length = event.preedit.selectionEnd - event.preedit.selectionStart,
                                                    .style = view::TypingSpan::Style::Target});
        }
        return typing;
    }
    default:
        return std::nullopt;
    }
}

/// An editor's input as the window's own record, for `window` (D790).
std::optional<window::Event> windowEventOf(const world_tooling::WindowInput& given, window::WindowId window) {
    window::Event made{.window = window};
    made.pointer.position = {.x = given.x, .y = given.y};
    made.pointer.modifiers = given.modifiers;
    made.pointer.button = given.button == 1   ? window::MouseButton::Right
                          : given.button == 2 ? window::MouseButton::Middle
                                              : window::MouseButton::Left;
    switch (given.kind) {
    case world_tooling::WindowInput::Kind::Move:
        made.kind = window::EventKind::CursorMoved;
        made.pointer.button = window::MouseButton::None;
        break;
    case world_tooling::WindowInput::Kind::Press:
        made.kind = window::EventKind::ButtonDown;
        made.pointer.clicks = 1;
        break;
    case world_tooling::WindowInput::Kind::Release:
        made.kind = window::EventKind::ButtonUp;
        break;
    case world_tooling::WindowInput::Kind::Wheel:
        made.kind = window::EventKind::Wheel;
        made.motion = {.x = given.deltaX, .y = given.deltaY};
        break;
    case world_tooling::WindowInput::Kind::KeyDown:
    case world_tooling::WindowInput::Kind::KeyUp:
        made.kind = given.kind == world_tooling::WindowInput::Kind::KeyDown ? window::EventKind::KeyDown
                                                                            : window::EventKind::KeyUp;
        made.key = {.usage = given.usage,
                    .meaning = window::kNamedKey | given.usage,
                    .modifiers = given.modifiers,
                    .repeat = given.repeat};
        break;
    case world_tooling::WindowInput::Kind::Text:
        made.kind = window::EventKind::TextInput;
        made.text = given.text;
        break;
    case world_tooling::WindowInput::Kind::Leave:
        made.kind = window::EventKind::CursorLeft;
        break;
    }
    return made;
}

} // namespace

std::optional<view::Ray> WindowHost::previewRay(float x, float y) const {
    const view::ViewSize kWindow = views_.window();
    const view::Region kRegion = view::previewRegion(settings_.preview->others().size() + 1, 0);
    const float kLeft = kRegion.left * kWindow.width;
    const float kTop = kRegion.top * kWindow.height;
    const view::ViewSize kSize{.width = kRegion.width * kWindow.width, .height = kRegion.height * kWindow.height};
    if (x < kLeft || y < kTop || x >= kLeft + kSize.width || y >= kTop + kSize.height) {
        return std::nullopt;
    }
    const auto kRay = view::pointToRay(*settings_.preview->looking(), kSize, x - kLeft, y - kTop);
    return kRay.has_value() ? std::optional<view::Ray>{*kRay} : std::nullopt;
}

bool WindowHost::asFinger(window::Event& event) noexcept {
    // One finger, its id as a platform's are: stable from down to up.
    constexpr std::uint64_t kFinger = 1;
    switch (event.kind) {
    case window::EventKind::ButtonDown:
    case window::EventKind::ButtonUp:
        if (event.pointer.button != window::MouseButton::Left) {
            return false;
        }
        fingerDown_ = event.kind == window::EventKind::ButtonDown;
        event.touch = window::Touch{.id = kFinger, .position = event.pointer.position};
        event.kind = fingerDown_ ? window::EventKind::TouchDown : window::EventKind::TouchUp;
        return true;
    case window::EventKind::CursorMoved:
        if (!fingerDown_) {
            return false;
        }
        event.touch = window::Touch{.id = kFinger, .position = event.pointer.position};
        event.kind = window::EventKind::TouchMoved;
        return true;
    case window::EventKind::CursorLeft:
        if (!fingerDown_) {
            return false;
        }
        fingerDown_ = false;
        event.touch = window::Touch{.id = kFinger};
        event.kind = window::EventKind::TouchCancelled;
        return true;
    case window::EventKind::CursorEntered:
    case window::EventKind::Wheel:
        return false;
    default:
        return true;
    }
}

bool WindowHost::forwarded(const window::Event& event) {
    if (!forwarding_.area().has_value()) {
        forwardedPress_ = false;
        forwardedOver_ = false;
        return false;
    }
    const std::array<float, 4> kArea = *forwarding_.area();
    const float kX = event.pointer.position.x - kArea[0];
    const float kY = event.pointer.position.y - kArea[1];
    const auto kButton = [&event]() -> std::uint8_t {
        return event.pointer.button == window::MouseButton::Right    ? 1
               : event.pointer.button == window::MouseButton::Middle ? 2
                                                                     : 0;
    };
    switch (event.kind) {
    case window::EventKind::ButtonDown:
        if (!forwarding_.pressed(event.pointer.position.x, event.pointer.position.y)) {
            return false;
        }
        forwardedPress_ = true;
        forwarding_.give({.kind = world_tooling::WindowInput::Kind::Press,
                          .x = kX,
                          .y = kY,
                          .button = kButton(),
                          .modifiers = event.pointer.modifiers});
        return true;
    case window::EventKind::ButtonUp:
        // A press handed over is let go there too, wherever it ends.
        if (!forwardedPress_) {
            return false;
        }
        forwardedPress_ = false;
        forwarding_.give({.kind = world_tooling::WindowInput::Kind::Release,
                          .x = kX,
                          .y = kY,
                          .button = kButton(),
                          .modifiers = event.pointer.modifiers});
        return true;
    case window::EventKind::CursorMoved: {
        const bool kOver = forwarding_.over(event.pointer.position.x, event.pointer.position.y);
        if (kOver || forwardedPress_) {
            forwardedOver_ = kOver;
            forwarding_.give({.kind = world_tooling::WindowInput::Kind::Move,
                              .x = kX,
                              .y = kY,
                              .modifiers = event.pointer.modifiers});
            return true;
        }
        if (std::exchange(forwardedOver_, false)) {
            forwarding_.give({.kind = world_tooling::WindowInput::Kind::Leave});
        }
        return false;
    }
    case window::EventKind::Wheel:
        if (!forwardedOver_) {
            return false;
        }
        forwarding_.give(
            {.kind = world_tooling::WindowInput::Kind::Wheel, .deltaX = event.motion.x, .deltaY = event.motion.y});
        return true;
    case window::EventKind::KeyDown:
    case window::EventKind::KeyUp:
        // Held only: a key goes down there while a press is, and up there
        // whenever it went down there (D916).
        if (!forwarding_.focused() ||
            (forwarding_.keysOnlyWhileHeld() && event.kind == window::EventKind::KeyDown && !forwardedPress_)) {
            return false;
        }
        forwarding_.give({.kind = event.kind == window::EventKind::KeyDown ? world_tooling::WindowInput::Kind::KeyDown
                                                                           : world_tooling::WindowInput::Kind::KeyUp,
                          .usage = event.key.usage,
                          .repeat = event.key.repeat,
                          .modifiers = event.key.modifiers});
        return true;
    case window::EventKind::TextInput:
        if (forwarding_.keysOnlyWhileHeld() && !forwardedPress_) {
            return false;
        }
        if (!forwarding_.focused() || event.text.empty() || event.text.size() > world_tooling::kMostText) {
            return forwarding_.focused();
        }
        forwarding_.give({.kind = world_tooling::WindowInput::Kind::Text, .text = event.text});
        return true;
    default:
        return false;
    }
}

WindowHost::WindowHost(const host::HostRequest& request, WindowHostSettings settings)
    : request_(request), settings_(std::move(settings)) {
    lent_.push_back(composition::LentCapability{input_kest::kFeed.name, composition::provideAs(feed_)});
    lent_.push_back(composition::LentCapability{window::kSurfaces.name, composition::provideAs(surfaces_)});
    lent_.push_back(composition::LentCapability{view::kPlayerViews.name, composition::provideAs(views_)});
    lent_.push_back(composition::LentCapability{view::kUiPointing.name, composition::provideAs(pointing_)});
    lent_.push_back(composition::LentCapability{world_tooling::kForwarding.name, composition::provideAs(forwarding_)});
    lent_.push_back(composition::LentCapability{view::kUiTyping.name, composition::provideAs(typing_)});
    lent_.push_back(composition::LentCapability{view::kUiNavigation.name, composition::provideAs(navigation_)});
    lent_.push_back(composition::LentCapability{ui::kAccessSeat.name, composition::provideAs(access_)});
    lent_.push_back(composition::LentCapability{view::kHeadsetAway.name, composition::provideAs(away_)});
    lent_.push_back(composition::LentCapability{view::kFilesDropped.name, composition::provideAs(dropped_)});
    lent_.push_back(composition::LentCapability{view::kFileDialogs.name, composition::provideAs(dialogs_)});
    lent_.push_back(composition::LentCapability{view::kWindowClosing.name, composition::provideAs(closing_)});
    lent_.push_back(composition::LentCapability{view::kWindowShowing.name, composition::provideAs(showing_)});
    lent_.insert(lent_.end(), settings_.lent.begin(), settings_.lent.end());
    request_.lent = lent_;
    request_.suspended = &suspended_;
}

result::Status WindowHost::start(window::Windows& windows) {
    // The window's size and place, logical pixels (D445): a player's
    // choice, or a tool's keeping a game it plays beside its own.
    window::WindowSettings made{.title = settings_.title};
    std::optional<window::Position> place;
    if (const composition::Configuration* kConfiguration = request_.configuration) {
        RAWFRAME_TRY_ASSIGN(const std::uint64_t kWidth, kConfiguration->unsignedInteger("window.width", 1280));
        RAWFRAME_TRY_ASSIGN(const std::uint64_t kHeight, kConfiguration->unsignedInteger("window.height", 720));
        RAWFRAME_TRY_ASSIGN(const std::uint64_t kX, kConfiguration->unsignedInteger("window.x", 0));
        RAWFRAME_TRY_ASSIGN(const std::uint64_t kY, kConfiguration->unsignedInteger("window.y", 0));
        // A machine's gamepads may be left out, as a player who plays with
        // none keeps one lying there from moving them, or a test keeps the
        // pads another program made (D467).
        RAWFRAME_TRY_ASSIGN(gamepads_, kConfiguration->truth("input.gamepads", true));
        // A game made for touch tried with a mouse (D809).
        RAWFRAME_TRY_ASSIGN(touchFromMouse_, kConfiguration->truth("input.touch_from_mouse", false));
        // A player who plays on the whole screen (D523).
        RAWFRAME_TRY_ASSIGN(const bool kFullscreen, kConfiguration->truth("window.fullscreen", false));
        if (kFullscreen) {
            made.mode = window::Mode::BorderlessFullscreen;
        }
        // An editor opened as an application, its window as large as the
        // screen lets it (D920).
        RAWFRAME_TRY_ASSIGN(const bool kMaximized, kConfiguration->truth("window.maximized", false));
        if (kMaximized && !kFullscreen) {
            made.mode = window::Mode::Maximized;
        }
        made.size = {.width = static_cast<float>(kWidth), .height = static_cast<float>(kHeight)};
        // Kept hidden, as an editor keeps a game it shows in its own
        // viewport, the pictures streamed to it (D789).
        RAWFRAME_TRY_ASSIGN(made.visible, kConfiguration->truth("window.visible", true));
        // Or hidden until what draws in it asks, so a program that finds
        // nothing to draw with never stands as a blank window (D942).
        RAWFRAME_TRY_ASSIGN(showWhenAsked_, kConfiguration->truth("window.shown_when_asked", false));
        if (showWhenAsked_ && made.visible) {
            made.visible = false;
        } else {
            showWhenAsked_ = false;
        }
        // The views a preview of it may be split into, which an editor
        // playing it in its viewport allows (D797).
        RAWFRAME_TRY_ASSIGN(const std::uint64_t kViews, kConfiguration->unsignedInteger("preview.views", 1));
        // An editor's viewport, its camera flown here (D913).
        RAWFRAME_TRY_ASSIGN(flying_, kConfiguration->truth("preview.flight", false));
        // An editor's edited World, its picture standing while time alone
        // moves it (D979).
        RAWFRAME_TRY_ASSIGN(const bool kEdited, kConfiguration->truth("preview.edited", false));
        if (settings_.preview != nullptr) {
            settings_.preview->allow(static_cast<std::size_t>(std::min<std::uint64_t>(kViews, 4)));
            settings_.preview->showEdited(kEdited);
        }
        // A game's own name over its window, as an export gives it (D519).
        if (const auto kTitle = kConfiguration->text("window.title")) {
            made.title = std::string{*kTitle};
        }
        // And its own icon, as an export gives it too (D951), over the
        // program's.
        if (const auto kIcon = kConfiguration->path("window.icon")) {
            RAWFRAME_TRY_ASSIGN(std::vector<IconSide> sides, readIcon(*kIcon, request_.files));
            iconPixels_.clear();
            iconPixels_.reserve(sides.size());
            settings_.icon.clear();
            for (IconSide& each : sides) {
                const std::vector<std::uint8_t>& kPixels = iconPixels_.emplace_back(std::move(each.pixels));
                settings_.icon.push_back(window::IconImage{.width = each.side, .height = each.side, .pixels = kPixels});
            }
        }
        if (kConfiguration->text("window.x").has_value() || kConfiguration->text("window.y").has_value()) {
            place = window::Position{.x = static_cast<float>(kX), .y = static_cast<float>(kY)};
        }
    }
    asked_ = made.size;
    shownMode_ = made.mode;
    RAWFRAME_TRY_ASSIGN(const window::WindowId kWindow, windows.create(made));
    if (place.has_value()) {
        static_cast<void>(windows.requestPosition(kWindow, *place));
    }
    surfaces_.watch(kWindow);
    window_ = kWindow;
    // Its own icon, where the program has one (D920); a platform that shows
    // none answers so, which changes nothing.
    if (!settings_.icon.empty()) {
        static_cast<void>(windows.requestIcon(window_, settings_.icon));
    }
    // The UI read by a screen reader where the platform has a bus for it,
    // AT-SPI's (D571), unless `ui.accessibility` says not; the participant
    // whose tree is the window's seats it, and says whether it is read.
    if (const composition::Configuration* kConfiguration = request_.configuration) {
        RAWFRAME_TRY_ASSIGN(accessible_, kConfiguration->truth("ui.accessibility", true));
    }
    if (accessible_ && ui::accessBuilt(ui::AccessPlatform::Atspi)) {
        accessPlatform_ = ui::AccessPlatform::Atspi;
        RAWFRAME_TRY(access_.open({.platform = ui::AccessPlatform::Atspi,
                                   .application = made.title.empty() ? std::string{"Rawframe"} : made.title}));
    }
    // A page's elements over the canvas (D612), made now: they wait behind
    // the adapter's hidden button until a screen reader's user presses it.
    // A UI pixel is a CSS pixel, as it is AppKit's point.
    if (accessible_ && ui::accessBuilt(ui::AccessPlatform::Aria)) {
        const auto kHandles = windows.handles(kWindow);
        const auto* kCanvas = kHandles.has_value() ? std::get_if<window::CanvasHandles>(&kHandles->handles) : nullptr;
        if (kCanvas != nullptr && !kCanvas->accessibility.empty()) {
            accessPlatform_ = ui::AccessPlatform::Aria;
            RAWFRAME_TRY(access_.open({.platform = ui::AccessPlatform::Aria, .selector = kCanvas->accessibility}));
        }
    }
    bridge_.emplace(feed_);
    host_ = std::make_unique<host::Host>(request_);
    return {};
}

window::FrameOutcome WindowHost::frame(window::Windows& windows) {
    // An editor asking to close is a press on the close button (D923).
    bool closing = closing_.asked();
    if (showing_.asked() && std::exchange(showWhenAsked_, false)) {
        static_cast<void>(windows.requestVisible(window_, true));
        // Shown, a window made hidden takes its mode again: Win32 shows it
        // restored (D943).
        if (shownMode_ != window::Mode::Windowed) {
            static_cast<void>(windows.requestMode(window_, shownMode_));
        }
    }
    const bool kWasSuspended = suspended_.load(std::memory_order_relaxed);
    // The touch screen's halves split the window as it is now (D387).
    if (const auto kState = windows.state(window_); kState.has_value()) {
        bridge_->resize(kState->size.width);
    }
    // Not until the players' input has read the key that ended the last
    // reading: a frame may come before the host is due, and an iteration
    // may run no world tick, and the keys after it would then be read in
    // the same tick after all (D506).
    // What an editor's viewport handed this preview is acted on first,
    // as the window's (D790).
    // Kept until acted on: a frame that waits on the players' reading acts
    // on none.
    // An editor's file dialogs shown over the window (D918); one the
    // window cannot show is answered unsupported at once.
    for (view::FileDialogs::Asked& asked : dialogs_.asked()) {
        const window::FileDialog kDialog{
            .kind = asked.kind == view::FileDialogs::Kind::OpenMany ? window::FileDialog::Kind::OpenMany
                    : asked.kind == view::FileDialogs::Kind::Save   ? window::FileDialog::Kind::Save
                    : asked.kind == view::FileDialogs::Kind::Folder ? window::FileDialog::Kind::Folder
                                                                    : window::FileDialog::Kind::Open,
            .title = std::move(asked.title),
            .folder = std::move(asked.folder),
            .filters = std::move(asked.filters)};
        if (auto shown = windows.requestFileDialog(window_, kDialog); shown.has_value()) {
            dialogsShown_.emplace_back(*shown, asked.ticket);
        } else {
            dialogs_.answered(view::FileDialogs::Answer{.ticket = asked.ticket, .unsupported = true});
        }
    }
    if (settings_.injection != nullptr) {
        for (const world_tooling::WindowInput& kEach : settings_.injection->take()) {
            if (std::optional<window::Event> made = windowEventOf(kEach, window_); made.has_value()) {
                injected_.push_back(std::move(*made));
            }
        }
    }
    while (!awaitingRead_) {
        std::optional<window::Event> event;
        if (!injected_.empty()) {
            event = std::move(injected_.front());
            injected_.pop_front();
        } else {
            event = windows.next();
        }
        if (!event.has_value()) {
            break;
        }
        // What lands on an editor's viewport is the game's it shows.
        if (forwarded(*event)) {
            continue;
        }
        if (touchFromMouse_ && !asFinger(*event)) {
            continue;
        }
        closing = closing || event->kind == window::EventKind::CloseRequested;
        if (event->kind == window::EventKind::Suspending || event->kind == window::EventKind::Resumed) {
            platformSuspended_ = event->kind == window::EventKind::Suspending;
        }
        accessAsked_ = accessAsked_ || event->kind == window::EventKind::AccessibilityRequested;
        // Whether a field takes text, told among the keys (D430): a key
        // typed into a field is never a gated action's, even when the
        // field lets go before the input next reads.
        if (typing_.editing() != toldEditing_) {
            toldEditing_ = typing_.editing();
            feed_.textEditing(toldEditing_);
        }
        if (!gamepads_ &&
            (event->kind == window::EventKind::GamepadAdded || event->kind == window::EventKind::GamepadRemoved ||
             event->kind == window::EventKind::GamepadButtonDown || event->kind == window::EventKind::GamepadButtonUp ||
             event->kind == window::EventKind::GamepadAxisMoved)) {
            continue;
        }
        // F11, or Alt and Enter, puts the window on the whole screen and back
        // (D523): the window's, never a game's action.
        if (event->kind == window::EventKind::KeyDown && !event->key.repeat &&
            (event->key.usage == kUsageF11 ||
             (event->key.usage == kUsageEnter &&
              (event->key.modifiers & static_cast<std::uint16_t>(window::Modifier::Alt)) != 0))) {
            const auto kState = windows.state(window_);
            const bool kWhole = kState.has_value() && kState->mode == window::Mode::BorderlessFullscreen;
            static_cast<void>(
                windows.requestMode(window_, kWhole ? window::Mode::Windowed : window::Mode::BorderlessFullscreen));
            continue;
        }
        // A flown preview's input is its camera's alone (D913).
        if (fly(*event)) {
            continue;
        }
        bridge_->take(*event);
        // A file dialog's choice, for the editor that asked (D918).
        if (event->kind == window::EventKind::RequestCompleted &&
            event->completion.kind == window::RequestKind::FileDialog) {
            if (const auto kShown = std::ranges::find(
                    dialogsShown_, event->completion.request, &std::pair<window::RequestId, std::uint64_t>::first);
                kShown != dialogsShown_.end()) {
                view::FileDialogs::Answer answer{
                    .ticket = kShown->second, .unsupported = event->completion.outcome == window::Outcome::Unsupported};
                if (event->completion.outcome == window::Outcome::Done) {
                    answer.paths = windows.dialogFiles(event->completion.request).value_or(std::vector<std::string>{});
                }
                dialogs_.answered(std::move(answer));
                dialogsShown_.erase(kShown);
            }
        }
        // Files dropped from outside, for an editor (D802).
        if (event->kind == window::EventKind::FilesDropped) {
            dropped_.dropped(view::FilesDropped::Drop{
                .x = event->position.x, .y = event->position.y, .paths = event->paths, .truncated = event->truncated});
        }
        // Where the mouse is, for what the UI shows under it (D422), where a
        // press went down, for the field that takes the keyboard (D426),
        // and the wheel turned there, for what scrolls (D441).
        if (event->kind == window::EventKind::ButtonDown) {
            // With the keys held, which an editor reads (D787).
            const std::uint16_t kHeld = event->pointer.modifiers;
            const auto kHolds = [kHeld](window::Modifier modifier) {
                return (kHeld & static_cast<std::uint16_t>(modifier)) != 0;
            };
            pointing_.pressed(
                event->pointer.position.x,
                event->pointer.position.y,
                view::UiPointing::Held{.control = kHolds(window::Modifier::Control) || kHolds(window::Modifier::Meta),
                                       .shift = kHolds(window::Modifier::Shift),
                                       .secondary = event->pointer.button == window::MouseButton::Right});
            // Where an author clicked in the preview, as a ray (D456).
            if (settings_.preview != nullptr && settings_.preview->looking().has_value() &&
                event->pointer.button == window::MouseButton::Left) {
                if (const auto kRay = previewRay(event->pointer.position.x, event->pointer.position.y);
                    kRay.has_value()) {
                    settings_.preview->clicked(*kRay, event->pointer.modifiers);
                }
            }
        } else if (event->kind == window::EventKind::TouchDown || event->kind == window::EventKind::TouchMoved ||
                   event->kind == window::EventKind::TouchUp || event->kind == window::EventKind::TouchCancelled) {
            // A finger is the pointer while it touches (D628).
            using Touching = view::UiPointing::Touching;
            const Touching kTouching = event->kind == window::EventKind::TouchDown    ? Touching::Down
                                       : event->kind == window::EventKind::TouchMoved ? Touching::Moved
                                       : event->kind == window::EventKind::TouchUp    ? Touching::Up
                                                                                      : Touching::Cancelled;
            pointing_.touched(kTouching, event->touch.id, event->touch.position.x, event->touch.position.y);
        } else if (event->kind == window::EventKind::ButtonUp) {
            pointing_.released();
            // And where the press was let go, for a drag (D457).
            if (settings_.preview != nullptr && settings_.preview->looking().has_value() &&
                event->pointer.button == window::MouseButton::Left) {
                if (const auto kRay = previewRay(event->pointer.position.x, event->pointer.position.y);
                    kRay.has_value()) {
                    settings_.preview->released(*kRay);
                }
            }
        }
        if (event->kind == window::EventKind::Wheel) {
            pointing_.wheeled(event->motion.x, event->motion.y);
        }
        moveView(*event);
        if (event->kind == window::EventKind::CursorMoved) {
            pointing_.pointAt(event->pointer.position.x, event->pointer.position.y);
            // And where it points in a preview, so the handle under it is
            // drawn lit (D468).
            if (settings_.preview != nullptr && settings_.preview->looking().has_value()) {
                settings_.preview->pointed(previewRay(event->pointer.position.x, event->pointer.position.y));
            }
        } else if (event->kind == window::EventKind::CursorLeft || event->kind == window::EventKind::FocusLost) {
            pointing_.left();
            if (settings_.preview != nullptr) {
                settings_.preview->pointed(std::nullopt);
            }
        }
        // A field holding focus takes what is typed (D426); the keys still
        // reach the players' input, whose gated actions they no longer move.
        // A chord goes to the UI whether or not a field holds focus, and
        // Delete with none holding it is one: what is chosen goes (D777).
        // So is a letter alone with none holding it, an editor's F framing
        // what is chosen (D792); a game's UI holds no such chord.
        std::optional<view::Typing> typed = typingOf(*event);
        if (typed.has_value() && !typing_.editing() && typed->kind == view::Typing::Kind::Key &&
            typed->key == view::TypingKey::Delete) {
            typed = view::Typing{.kind = view::Typing::Kind::Chord, .text = "Delete"};
        }
        // As is Escape, an editor's dialog or menu closed by it (D942).
        if (typed.has_value() && !typing_.editing() && typed->kind == view::Typing::Kind::Key &&
            typed->key == view::TypingKey::Dismiss) {
            typed = view::Typing{.kind = view::Typing::Kind::Chord, .text = "Escape"};
        }
        if (!typed.has_value() && !typing_.editing()) {
            typed = letterOf(*event);
        }
        if (typed.has_value() && (typing_.editing() || typed->kind == view::Typing::Kind::Chord)) {
            typing_.type(*typed);
        }
        // A key pressed while the UI is navigated and no field holds the
        // keyboard may move focus or give a field the keyboard, which the
        // iterations do; the events after it wait for the next frame, so a
        // key typed after one that gave a field the keyboard reaches that
        // field however many a slow frame gathers (D458). So may a key
        // pressed while navigation reaches a node but is not entered: the
        // game's sample may enter it, after the frame's navigation is
        // done, and a key after it would find navigation not yet entered
        // (D476).
        if (event->kind == window::EventKind::KeyDown && !typing_.editing() &&
            (navigation_.focused() || navigation_.reachable())) {
            awaitingRead_ = true;
            iterationsAwaited_ = 0;
            break;
        }
    }
    if (closing) {
        return end();
    }
    // The flown preview's view moved by what the input asked, at the
    // frame's pace (D913).
    if (flying_ && settings_.preview != nullptr) {
        const execution::MonotonicInstant kNow = clock_.now();
        const double kSeconds = flown_.has_value() ? static_cast<double>((kNow - *flown_).nanoseconds) / 1e9 : 0.0;
        flown_ = kNow;
        settings_.preview->fly(kSeconds, static_cast<double>(asked_.height));
    }
    surfaces_.update(windows);
    if (const auto kState = windows.state(window_); kState.has_value()) {
        // A window kept hidden is told no size: it is the size it was
        // asked to be, as an editor's viewport is (D790).
        const bool kSized = kState->size.width > 0 && kState->size.height > 0;
        views_.window({.width = kSized ? kState->size.width : asked_.width,
                       .height = kSized ? kState->size.height : asked_.height});
        views_.safeArea({.top = kState->safeArea.top,
                         .right = kState->safeArea.right,
                         .bottom = kState->safeArea.bottom,
                         .left = kState->safeArea.left});
    }
    // The platform's `suspending` comes in a frame of its own, after which
    // frames pause (Maul Window's mobile backends): the Host runs in it,
    // due or not, so its participants hear of it before then (D565). A
    // headset's session tells its own as an iteration runs (D679), heard
    // from the next.
    suspended_.store(platformSuspended_ || away_.away(), std::memory_order_release);
    const bool kTold = suspended_.load(std::memory_order_relaxed) != kWasSuspended;
    for (int ran = 0; ran < kMostIterationsPerFrame && (clock_.now() >= host_->due() || (kTold && ran == 0)); ++ran) {
        if (!host_->iterate()) {
            return end();
        }
        suspended_.store(platformSuspended_ || away_.away(), std::memory_order_release);
        if (awaitingRead_ && (feed_.waiting() == 0 || ++iterationsAwaited_ >= kMostIterationsAwaited)) {
            awaitingRead_ = false;
        }
    }
    followAccess(windows);
    // What the iterations asked the player's gamepads to feel.
    bridge_->feel(windows);
    followTextInput(windows);
#if RAWFRAME_THREADS
    const execution::MonotonicDuration kUntilDue = host_->due() - clock_.now();
    const auto kWait =
        std::min<std::chrono::nanoseconds>(std::chrono::nanoseconds{kUntilDue.nanoseconds}, kLongestWait);
    if (kWait.count() > 0) {
        std::this_thread::sleep_for(kWait);
    }
#endif
    return window::FrameOutcome::Continue;
}

void WindowHost::followAccess(window::Windows& windows) {
    // A platform whose clients ask for the window's tree (Android, Windows,
    // macOS) is read once one first asks, so nothing is built where no
    // screen reader runs, and again for each view the window shows in,
    // as Android's changes with the activity (D576, D579).
    if (accessible_ && accessAsked_) {
        const auto kHandles = windows.handles(window_);
        const auto kState = windows.state(window_);
        std::optional<ui::AccessSettings> settings;
        if (kHandles.has_value() && kState.has_value() && kState->scale > 0) {
#if defined(__ANDROID__)
            if (const auto* kAndroid = std::get_if<window::AndroidHandles>(&kHandles->handles)) {
                settings = ui::AccessSettings{.platform = ui::AccessPlatform::Android,
                                              .scale = kState->scale,
                                              .env = static_cast<ANativeActivity*>(kAndroid->activity)->env,
                                              .host = kAndroid->view};
            }
#endif
            if (const auto* kWin32 = std::get_if<window::Win32Handles>(&kHandles->handles)) {
                settings = ui::AccessSettings{
                    .platform = ui::AccessPlatform::Uia, .scale = kState->scale, .host = kWin32->window};
            } else if (const auto* kApple = std::get_if<window::AppleHandles>(&kHandles->handles)) {
                // AppKit and UIKit measure in points, which a UI pixel is.
                settings = ui::AccessSettings{.platform = kAppleAccess, .scale = 1, .host = kApple->view};
            }
        }
        if (settings.has_value() && settings->host != nullptr && settings->host != accessView_ &&
            ui::accessBuilt(settings->platform)) {
            accessView_ = settings->host;
            accessScale_ = settings->scale;
            accessPlatform_ = settings->platform;
            static_cast<void>(access_.open(*settings));
        }
    }
    // The root the window hands its clients follows the access, made or
    // gone; only a platform that takes one has one.
    void* const kRoot = access_.access() != nullptr ? access_.access()->root() : nullptr;
    if (kRoot != accessRoot_ && windows.requestAccessibilityRoot(window_, kRoot).has_value()) {
        accessRoot_ = kRoot;
    }
    if (access_.access() == nullptr) {
        return;
    }
    if (const auto kState = windows.state(window_); kState.has_value() && kState->scale > 0) {
        if (accessPlatform_ != kAppleAccess && accessPlatform_ != ui::AccessPlatform::Aria &&
            kState->scale != accessScale_ && access_.setScale(kState->scale).has_value()) {
            accessScale_ = kState->scale;
        }
        // X11 says where a window is, in its pixels, and AT-SPI's screen is
        // the X screen; Wayland says not, and the window is its own screen.
        const auto kHandles = windows.handles(window_);
        if (kHandles.has_value() && std::holds_alternative<window::XcbHandles>(kHandles->handles)) {
            const std::array<std::int32_t, 2> kPlace = {
                static_cast<std::int32_t>(std::lround(kState->position.x * kState->scale)),
                static_cast<std::int32_t>(std::lround(kState->position.y * kState->scale))};
            if (kPlace != accessPlace_) {
                access_.setPlace(kPlace);
                accessPlace_ = kPlace;
            }
        }
    }
    // A platform that refuses an update leaves the UI as it was last read;
    // the game plays on.
    static_cast<void>(access_.update());
}

bool WindowHost::fly(const window::Event& event) {
    if (!flying_ || settings_.preview == nullptr || !settings_.preview->looking().has_value()) {
        return false;
    }
    view::Steering& steering = settings_.preview->steering();
    const auto kHeld = [](std::uint16_t modifiers, window::Modifier modifier) {
        return (modifiers & static_cast<std::uint16_t>(modifier)) != 0;
    };
    switch (event.kind) {
    case window::EventKind::ButtonDown: {
        const std::array<float, 2> kAt{event.pointer.position.x, event.pointer.position.y};
        if (event.pointer.button == window::MouseButton::Right) {
            turning_ = kAt;
            return true;
        }
        if (event.pointer.button == window::MouseButton::Middle) {
            panning_ = kAt;
            return true;
        }
        if (event.pointer.button == window::MouseButton::Left &&
            kHeld(event.pointer.modifiers, window::Modifier::Alt)) {
            altOrbiting_ = kAt;
            return true;
        }
        // A left press picks and drags, as it does in any preview.
        return false;
    }
    case window::EventKind::ButtonUp:
        if (event.pointer.button == window::MouseButton::Right) {
            turning_.reset();
            // The keys fly only while it is held.
            steering = view::Steering{.turn = steering.turn, .pan = steering.pan, .orbit = steering.orbit};
            return true;
        }
        if (event.pointer.button == window::MouseButton::Middle) {
            panning_.reset();
            return true;
        }
        if (event.pointer.button == window::MouseButton::Left && altOrbiting_.has_value()) {
            altOrbiting_.reset();
            return true;
        }
        return false;
    case window::EventKind::CursorMoved: {
        const std::array<float, 2> kAt{event.pointer.position.x, event.pointer.position.y};
        const auto kCarry = [&kAt](std::optional<std::array<float, 2>>& from, std::array<double, 2>& into) {
            if (from.has_value()) {
                into[0] += kAt[0] - (*from)[0];
                into[1] += kAt[1] - (*from)[1];
                from = kAt;
            }
        };
        kCarry(turning_, steering.turn);
        kCarry(panning_, steering.pan);
        kCarry(altOrbiting_, steering.orbit);
        // The pointer still points, for the handle lit under it.
        return false;
    }
    case window::EventKind::Wheel:
        // The window's wheel turned away is positive; toward the author
        // counts so here. With the right button held, it sets the speed.
        (turning_.has_value() ? steering.speedWheel : steering.wheel) += -event.motion.y;
        return true;
    case window::EventKind::KeyDown:
    case window::EventKind::KeyUp: {
        const bool kDown = event.kind == window::EventKind::KeyDown;
        constexpr std::uint16_t kW = 0x1A;
        constexpr std::uint16_t kA = 0x04;
        constexpr std::uint16_t kS = 0x16;
        constexpr std::uint16_t kD = 0x07;
        constexpr std::uint16_t kQ = 0x14;
        constexpr std::uint16_t kE = 0x08;
        constexpr std::uint16_t kLeftShift = 0xE1;
        constexpr std::uint16_t kRightShift = 0xE5;
        bool* held = nullptr;
        switch (event.key.usage) {
        case kW:
            held = &steering.forward;
            break;
        case kA:
            held = &steering.left;
            break;
        case kS:
            held = &steering.back;
            break;
        case kD:
            held = &steering.right;
            break;
        case kQ:
            held = &steering.down;
            break;
        case kE:
            held = &steering.up;
            break;
        case kLeftShift:
        case kRightShift:
            steering.faster = kDown;
            return turning_.has_value();
        default:
            return false;
        }
        // Flown only while the right button turns, else the key is the
        // editor's (W, E, R choose a tool).
        if (!turning_.has_value()) {
            *held = false;
            return false;
        }
        *held = kDown;
        return true;
    }
    default:
        return false;
    }
}

void WindowHost::moveView(const window::Event& event) {
    // A preview's view moved as an editor's viewport is (D469): the wheel
    // zooms and a drag with the right button orbits, both told to the
    // preview, which the author's session reads.
    if (settings_.preview == nullptr || !settings_.preview->looking().has_value()) {
        orbiting_.reset();
        return;
    }
    // With Shift held, freelook's: the view turned about its eye and flown
    // along (D472).
    // A wheel's record names no modifiers, so the last a key or the
    // pointer told are kept.
    if (event.kind == window::EventKind::KeyDown || event.kind == window::EventKind::KeyUp) {
        modifiers_ = event.key.modifiers;
        // A Shift key's own record may tell the modifiers before it, so it
        // is read by its place: HID's left and right Shift.
        constexpr std::uint16_t kLeftShift = 0xE1;
        constexpr std::uint16_t kRightShift = 0xE5;
        if (event.key.usage == kLeftShift || event.key.usage == kRightShift) {
            const auto kShift = static_cast<std::uint16_t>(window::Modifier::Shift);
            modifiers_ = event.kind == window::EventKind::KeyDown ? static_cast<std::uint16_t>(modifiers_ | kShift)
                                                                  : static_cast<std::uint16_t>(modifiers_ & ~kShift);
        }
    } else if (event.kind == window::EventKind::CursorMoved || event.kind == window::EventKind::ButtonDown ||
               event.kind == window::EventKind::ButtonUp) {
        modifiers_ = event.pointer.modifiers;
    }
    const bool kFree = (modifiers_ & static_cast<std::uint16_t>(window::Modifier::Shift)) != 0;
    if (event.kind == window::EventKind::Wheel) {
        // The window's wheel turned away is positive; the preview counts
        // toward the author so.
        if (kFree) {
            settings_.preview->flown(-event.motion.y);
        } else {
            settings_.preview->wheeled(-event.motion.y);
        }
    } else if (event.kind == window::EventKind::ButtonDown && event.pointer.button == window::MouseButton::Right) {
        orbiting_ = std::array<float, 2>{event.pointer.position.x, event.pointer.position.y};
    } else if (event.kind == window::EventKind::ButtonUp && event.pointer.button == window::MouseButton::Right) {
        orbiting_.reset();
    } else if (event.kind == window::EventKind::CursorMoved && orbiting_.has_value()) {
        const double kAcross = event.pointer.position.x - (*orbiting_)[0];
        const double kUp = event.pointer.position.y - (*orbiting_)[1];
        if (kFree) {
            settings_.preview->looked(kAcross, kUp);
        } else {
            settings_.preview->orbited(kAcross, kUp);
        }
        orbiting_ = std::array<float, 2>{event.pointer.position.x, event.pointer.position.y};
    }
}

void WindowHost::followTextInput(window::Windows& windows) {
    const std::optional<view::UiTyping::Caret> kCaret = typing_.editing() ? typing_.caret() : std::nullopt;
    if (kCaret == textInput_) {
        return;
    }
    const window::Rect kAt =
        kCaret.has_value()
            ? window::Rect{.x = (*kCaret)[0], .y = (*kCaret)[1], .width = (*kCaret)[2], .height = (*kCaret)[3]}
            : window::Rect{};
    // A platform without text input refuses; its keys still edit, and the
    // request is not repeated until the caret moves.
    static_cast<void>(windows.requestTextInput(window_, kCaret.has_value(), kAt));
    textInput_ = kCaret;
}

void WindowHost::stop(window::Windows& /*windows*/, const result::Status& /*status*/) {
    if (host_ != nullptr && !exit_.has_value()) {
        exit_ = host_->stop();
    }
}

window::FrameOutcome WindowHost::end() {
    exit_ = host_->stop();
    return window::FrameOutcome::Stop;
}

} // namespace rawframe::window_host
