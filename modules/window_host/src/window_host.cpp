#include "rawframe/window_host/window_host.h"

#include "rawframe/base/platform.h"
#include "rawframe/input_kest/sources.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <optional>
#include <utility>
#include <variant>

#if RAWFRAME_THREADS
#include <thread>
#endif
#if defined(__ANDROID__)
#include <android/native_activity.h>
#endif

namespace rawframe::window_host {

namespace {

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

/// A window's record as typing for the field that holds focus, if it is
/// any: a key that edits, typed or committed text, or a composition.
std::optional<view::Typing> typingOf(const window::Event& event) {
    switch (event.kind) {
    case window::EventKind::KeyDown:
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

} // namespace

WindowHost::WindowHost(const host::HostRequest& request, WindowHostSettings settings)
    : request_(request), settings_(std::move(settings)) {
    lent_.push_back(composition::LentCapability{input_kest::kFeed.name, composition::provideAs(feed_)});
    lent_.push_back(composition::LentCapability{window::kSurfaces.name, composition::provideAs(surfaces_)});
    lent_.push_back(composition::LentCapability{view::kPlayerViews.name, composition::provideAs(views_)});
    lent_.push_back(composition::LentCapability{view::kUiPointing.name, composition::provideAs(pointing_)});
    lent_.push_back(composition::LentCapability{view::kUiTyping.name, composition::provideAs(typing_)});
    lent_.push_back(composition::LentCapability{view::kUiNavigation.name, composition::provideAs(navigation_)});
    lent_.push_back(composition::LentCapability{ui::kAccessSeat.name, composition::provideAs(access_)});
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
        // A player who plays on the whole screen (D523).
        RAWFRAME_TRY_ASSIGN(const bool kFullscreen, kConfiguration->truth("window.fullscreen", false));
        if (kFullscreen) {
            made.mode = window::Mode::BorderlessFullscreen;
        }
        made.size = {.width = static_cast<float>(kWidth), .height = static_cast<float>(kHeight)};
        // A game's own name over its window, as an export gives it (D519).
        if (const auto kTitle = kConfiguration->text("window.title")) {
            made.title = std::string{*kTitle};
        }
        if (kConfiguration->text("window.x").has_value() || kConfiguration->text("window.y").has_value()) {
            place = window::Position{.x = static_cast<float>(kX), .y = static_cast<float>(kY)};
        }
    }
    RAWFRAME_TRY_ASSIGN(const window::WindowId kWindow, windows.create(made));
    if (place.has_value()) {
        static_cast<void>(windows.requestPosition(kWindow, *place));
    }
    surfaces_.watch(kWindow);
    window_ = kWindow;
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
    bridge_.emplace(feed_);
    host_ = std::make_unique<host::Host>(request_);
    return {};
}

window::FrameOutcome WindowHost::frame(window::Windows& windows) {
    bool closing = false;
    const bool kWasSuspended = suspended_.load(std::memory_order_relaxed);
    // The touch screen's halves split the window as it is now (D387).
    if (const auto kState = windows.state(window_); kState.has_value()) {
        bridge_->resize(kState->size.width);
    }
    // Not until the players' input has read the key that ended the last
    // reading: a frame may come before the host is due, and an iteration
    // may run no world tick, and the keys after it would then be read in
    // the same tick after all (D506).
    while (!awaitingRead_) {
        const std::optional<window::Event> event = windows.next();
        if (!event.has_value()) {
            break;
        }
        closing = closing || event->kind == window::EventKind::CloseRequested;
        if (event->kind == window::EventKind::Suspending || event->kind == window::EventKind::Resumed) {
            suspended_.store(event->kind == window::EventKind::Suspending, std::memory_order_release);
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
        bridge_->take(*event);
        // Where the mouse is, for what the UI shows under it (D422), where a
        // press went down, for the field that takes the keyboard (D426),
        // and the wheel turned there, for what scrolls (D441).
        if (event->kind == window::EventKind::ButtonDown) {
            pointing_.pressed(event->pointer.position.x, event->pointer.position.y);
            // Where an author clicked in the preview, as a ray (D456).
            if (settings_.preview != nullptr && settings_.preview->looking().has_value() &&
                event->pointer.button == window::MouseButton::Left) {
                if (const auto kRay = view::pointToRay(*settings_.preview->looking(),
                                                       views_.window(),
                                                       event->pointer.position.x,
                                                       event->pointer.position.y);
                    kRay.has_value()) {
                    settings_.preview->clicked(*kRay, event->pointer.modifiers);
                }
            }
        } else if (event->kind == window::EventKind::TouchDown) {
            pointing_.pressed(event->touch.position.x, event->touch.position.y);
        } else if (event->kind == window::EventKind::ButtonUp || event->kind == window::EventKind::TouchUp) {
            pointing_.released();
            // And where the press was let go, for a drag (D457).
            if (event->kind == window::EventKind::ButtonUp && settings_.preview != nullptr &&
                settings_.preview->looking().has_value() && event->pointer.button == window::MouseButton::Left) {
                if (const auto kRay = view::pointToRay(*settings_.preview->looking(),
                                                       views_.window(),
                                                       event->pointer.position.x,
                                                       event->pointer.position.y);
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
                const auto kRay = view::pointToRay(*settings_.preview->looking(),
                                                   views_.window(),
                                                   event->pointer.position.x,
                                                   event->pointer.position.y);
                settings_.preview->pointed(kRay.has_value() ? std::optional<view::Ray>{*kRay} : std::nullopt);
            }
        } else if (event->kind == window::EventKind::CursorLeft || event->kind == window::EventKind::FocusLost) {
            pointing_.left();
            if (settings_.preview != nullptr) {
                settings_.preview->pointed(std::nullopt);
            }
        }
        // A field holding focus takes what is typed (D426); the keys still
        // reach the players' input, whose gated actions they no longer move.
        if (typing_.editing()) {
            if (const std::optional<view::Typing> kTyping = typingOf(*event)) {
                typing_.type(*kTyping);
            }
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
    surfaces_.update(windows);
    if (const auto kState = windows.state(window_); kState.has_value()) {
        views_.window({.width = kState->size.width, .height = kState->size.height});
    }
    // The platform's `suspending` comes in a frame of its own, after which
    // frames pause (Maul Window's mobile backends): the Host runs in it,
    // due or not, so its participants hear of it before then (D565).
    const bool kTold = suspended_.load(std::memory_order_relaxed) != kWasSuspended;
    for (int ran = 0; ran < kMostIterationsPerFrame && (clock_.now() >= host_->due() || (kTold && ran == 0)); ++ran) {
        if (!host_->iterate()) {
            return end();
        }
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
                // AppKit measures in points, which a UI pixel is.
                settings = ui::AccessSettings{.platform = ui::AccessPlatform::AppKit, .scale = 1, .host = kApple->view};
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
        if (accessPlatform_ != ui::AccessPlatform::AppKit && kState->scale != accessScale_ &&
            access_.setScale(kState->scale).has_value()) {
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
