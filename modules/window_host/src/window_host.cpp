#include "rawframe/window_host/window_host.h"

#include "rawframe/base/platform.h"
#include "rawframe/input_kest/sources.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <optional>
#include <utility>

#if RAWFRAME_THREADS
#include <thread>
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
    lent_.insert(lent_.end(), settings_.lent.begin(), settings_.lent.end());
    request_.lent = lent_;
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
        made.size = {.width = static_cast<float>(kWidth), .height = static_cast<float>(kHeight)};
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
    bridge_.emplace(feed_);
    host_ = std::make_unique<host::Host>(request_);
    return {};
}

window::FrameOutcome WindowHost::frame(window::Windows& windows) {
    bool closing = false;
    // The touch screen's halves split the window as it is now (D387).
    if (const auto kState = windows.state(window_); kState.has_value()) {
        bridge_->resize(kState->size.width);
    }
    while (std::optional<window::Event> event = windows.next()) {
        closing = closing || event->kind == window::EventKind::CloseRequested;
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
        // field however many a slow frame gathers (D458).
        if (event->kind == window::EventKind::KeyDown && !typing_.editing() && navigation_.focused()) {
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
    for (int ran = 0; ran < kMostIterationsPerFrame && clock_.now() >= host_->due(); ++ran) {
        if (!host_->iterate()) {
            return end();
        }
    }
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
