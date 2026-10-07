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
#include "rawframe/view/navigation.h"
#include "rawframe/view/players.h"
#include "rawframe/view/pointing.h"
#include "rawframe/view/preview.h"
#include "rawframe/view/typing.h"
#include "rawframe/window/surfaces.h"
#include "rawframe/window/windows.h"

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

    host::HostRequest request_;
    WindowHostSettings settings_;
    input::Feed feed_;
    window::Surfaces surfaces_;
    view::PlayerViews views_;
    view::UiPointing pointing_;
    view::UiTyping typing_;
    view::UiNavigation navigation_;
    /// Whether the feed was last told a field takes text.
    bool toldEditing_ = false;
    /// Whether the players' gamepads play: `input.gamepads`, true unless
    /// told false (D467).
    bool gamepads_ = true;
    /// The caret the platform was last asked for text input at, none while
    /// it was not asked.
    std::optional<view::UiTyping::Caret> textInput_;
    window::WindowId window_;
    std::vector<composition::LentCapability> lent_;
    std::optional<input_window::Bridge> bridge_;
    std::unique_ptr<host::Host> host_;
    execution::SteadyClock clock_;
    std::optional<host::HostExit> exit_;
};

} // namespace rawframe::window_host
