#pragma once

// A Host driven from a window's frames (D249, D250): the process's one
// window, whose raw input the Host's participants get as the player's
// devices (`rawframe.input.feed`), and each frame every Host iteration due,
// then what they asked the player's gamepads to feel (D251). The window's
// surface is lent too (`rawframe.window.surfaces`, D280), read in each
// frame before the Host runs, for the device to present to; and the local
// players' views (`rawframe.view.player_views`, D367), told the window's
// size each frame, for the presentation to write and the sample to read,
// and what the UI takes of the pointer (`rawframe.view.ui_pointing`, D421),
// which the UI answers and each player's input asks as a press begins.
// The window system owns the loop on every platform, so a desktop client
// and a page run the same program: `window::run` returns when it stops on
// a desktop, and at once on the web, where the page's frames go on.
//
//   window.width, window.height   the window's size, logical pixels
//                                 (1280 by 720)
//   window.x, window.y            its place on the screen, either naming it;
//                                 where the system places it otherwise (D445)

#include "rawframe/host/host.h"
#include "rawframe/input/feed.h"
#include "rawframe/input_window/bridge.h"
#include "rawframe/result/result.h"
#include "rawframe/ui/access.h"
#include "rawframe/view/asides.h"
#include "rawframe/view/closing.h"
#include "rawframe/view/dialogs.h"
#include "rawframe/view/dropping.h"
#include "rawframe/view/headset.h"
#include "rawframe/view/navigation.h"
#include "rawframe/view/players.h"
#include "rawframe/view/pointing.h"
#include "rawframe/view/preview.h"
#include "rawframe/view/showing.h"
#include "rawframe/view/typing.h"
#include "rawframe/window/surfaces.h"
#include "rawframe/window/windows.h"
#include "rawframe/world_tooling/forwarding.h"

#include <array>
#include <atomic>
#include <deque>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace rawframe::window_host {

struct WindowHostSettings {
    std::string title = "Rawframe";
    /// What else the host lends its participants beside the player's
    /// devices (a page's sound, D259), each outliving the WindowHost.
    std::vector<composition::LentCapability> lent;
    /// The camera an authoring preview looks through, if the host shows
    /// one: a left press while it looks is kept as the ray it makes
    /// (D456). It outlives the WindowHost.
    view::PreviewCamera* preview = nullptr;
    /// Input to act on as the window's, before the window's own, if the
    /// host shows a preview an editor plays in its viewport (D790). It
    /// outlives the WindowHost.
    world_tooling::Injection* injection = nullptr;
    /// The window's icon, its images' pixels outliving the WindowHost
    /// (D920); none for the platform's own. A configuration's
    /// `window.icon` is shown instead (D951).
    std::vector<window::IconImage> icon;
};

class WindowHost final : public window::Program {
public:
    /// `request` and what it points to outlive the WindowHost; its lent
    /// capabilities are replaced by the player's devices and the settings'.
    explicit WindowHost(const host::HostRequest& request, WindowHostSettings settings = {});

    result::Status start(window::Windows& windows) override;
    window::FrameOutcome frame(window::Windows& windows) override;
    void stop(window::Windows& windows, const result::Status& status) override;

    /// How the Host ended, or nothing while it runs or when it never did.
    [[nodiscard]] std::optional<host::HostExit> exit() const noexcept {
        return exit_;
    }

private:
    window::FrameOutcome end();
    /// Asks the platform for text input, at the caret, while a UI field
    /// holds focus, and to stop when none does (D426).
    void followTextInput(window::Windows& windows);
    /// Tells a preview the wheel and the right button's drags (D469).
    void moveView(const window::Event& event);
    /// The UI's readers told what changed, the window's scale and, on X11,
    /// its place first (D571); on Android, Windows, and macOS, read once a
    /// client asks, for each view, its root handed to the window (D576,
    /// D579).
    void followAccess(window::Windows& windows);

    host::HostRequest request_;
    WindowHostSettings settings_;
    input::Feed feed_;
    window::Surfaces surfaces_;
    view::PlayerViews views_;
    view::UiPointing pointing_;
    /// What lands on the area an editor shows a game over, handed to it
    /// (D790): whether the press held was handed over, and whether the
    /// pointer is over the area.
    world_tooling::Forwarding forwarding_;
    /// An editor's input to act on as the window's, oldest first.
    std::deque<window::Event> injected_;
    /// The size the window was asked to be.
    window::LogicalSize asked_;
    bool forwardedPress_ = false;
    bool forwardedOver_ = false;
    /// Whether `event` was handed over, in place of acted on.
    bool forwarded(const window::Event& event);
    /// The ray from the preview's look through window point (`x`, `y`),
    /// in its region of a split window (D797); none outside it.
    [[nodiscard]] std::optional<view::Ray> previewRay(float x, float y) const;
    view::UiTyping typing_;
    view::UiNavigation navigation_;
    /// Where the window's UI is read for assistive technology (D571), and
    /// the scale and place it was last told.
    ui::AccessSeat access_;
    float accessScale_ = 1;
    std::optional<std::array<std::int32_t, 2>> accessPlace_;
    /// Whether `ui.accessibility` lets the UI be read, whether a client
    /// asked for the window's tree, the platform and the view the access
    /// was made for, and the root last handed to the window (D576, D579).
    bool accessible_ = true;
    bool accessAsked_ = false;
    ui::AccessPlatform accessPlatform_ = ui::AccessPlatform::Copy;
    void* accessView_ = nullptr;
    void* accessRoot_ = nullptr;
    /// Whether the feed was last told a field takes text.
    bool toldEditing_ = false;
    /// Whether a key ended a frame's reading and the players' input has
    /// not read it yet: the events after it wait until it has (D506).
    bool awaitingRead_ = false;
    /// Iterations run while it waited, so a game whose input reads nothing
    /// does not hold its events for ever.
    int iterationsAwaited_ = 0;
    /// Whether the players' gamepads play: `input.gamepads`, true unless
    /// told false (D467).
    bool gamepads_ = true;
    /// Whether the mouse is played as a finger, `input.touch_from_mouse`,
    /// and whether that finger touches (D809).
    bool touchFromMouse_ = false;
    bool fingerDown_ = false;
    /// `event` made a finger's where the mouse plays one; whether it is
    /// still acted on, a touch screen having no hover or other buttons.
    bool asFinger(window::Event& event) noexcept;
    /// Where the pointer was while the right button is held in a preview,
    /// so its moves orbit the preview's view (D469); none while it is not.
    std::optional<std::array<float, 2>> orbiting_;
    /// Flown as an editor's viewport camera (D913), where `preview.flight`
    /// says so: the input is the preview's, the game hears none of it.
    /// Where the pointer was while the right button turns, the middle
    /// pans, or Alt and the left orbit; when it was last flown.
    bool flying_ = false;
    std::optional<std::array<float, 2>> turning_;
    std::optional<std::array<float, 2>> panning_;
    std::optional<std::array<float, 2>> altOrbiting_;
    std::optional<execution::MonotonicInstant> flown_;
    /// What the input asks of a flown preview's view; whether it took it.
    bool fly(const window::Event& event);
    /// The modifiers last told while a preview looks, for the wheel's
    /// records, which name none (D472).
    std::uint16_t modifiers_ = 0;
    /// The caret the platform was last asked for text input at, none while
    /// it was not asked.
    std::optional<view::UiTyping::Caret> textInput_;
    window::WindowId window_;
    /// From the platform's `suspending` until its `resumed` (D565), and
    /// while a headset shows the person nothing (D679).
    bool platformSuspended_ = false;
    view::HeadsetAway away_;
    /// Files dropped on the window, held for an editor (D802).
    view::FilesDropped dropped_;
    /// An editor's file dialogs (D918): asked, shown over the window, and
    /// each request's ticket until it completes.
    view::FileDialogs dialogs_;
    view::WindowClosing closing_;
    /// An editor's windows beside this one (D988): each key and window
    /// open, and each closed, its surface let go, until an iteration has
    /// run since and it is destroyed.
    view::WindowAsides asides_;
    std::vector<std::pair<std::uint64_t, window::WindowId>> asideWindows_;
    std::vector<std::pair<window::WindowId, std::uint64_t>> asidesClosed_;
    std::uint64_t iterations_ = 0;
    /// What an editor asked of its asides acted on.
    void followAsides(window::Windows& windows);
    /// Whether `event` was an aside's and taken as one: its pointer's and
    /// its closing; its keys are the window's typing.
    bool aside(const window::Event& event);
    /// Lent where the window waits hidden until asked (D942).
    view::WindowShowing showing_;
    bool showWhenAsked_ = false;
    window::Mode shownMode_ = window::Mode::Windowed;
    std::vector<std::pair<window::RequestId, std::uint64_t>> dialogsShown_;
    /// The pixels of the icon `window.icon` names (D951).
    std::vector<std::vector<std::uint8_t>> iconPixels_;
    /// Either, as the Host's frames tell its participants.
    std::atomic<bool> suspended_{false};
    std::vector<composition::LentCapability> lent_;
    std::optional<input_window::Bridge> bridge_;
    std::unique_ptr<host::Host> host_;
    execution::SteadyClock clock_;
    std::optional<host::HostExit> exit_;
};

} // namespace rawframe::window_host
