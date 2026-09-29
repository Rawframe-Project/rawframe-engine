#pragma once

// A Host driven from a window's frames (D249, D250): the process's one
// window, whose raw input the Host's participants get as the player's
// devices (`rawframe.input.feed`), and each frame every Host iteration due.
// The window system owns the loop on every platform, so a desktop client
// and a page run the same program: `window::run` returns when it stops on
// a desktop, and at once on the web, where the page's frames go on.

#include "rawframe/host/host.h"
#include "rawframe/input/feed.h"
#include "rawframe/input_window/bridge.h"
#include "rawframe/result/result.h"
#include "rawframe/window/windows.h"

#include <array>
#include <memory>
#include <optional>
#include <string>

namespace rawframe::window_host {

struct WindowHostSettings {
    std::string title = "Rawframe";
};

class WindowHost final : public window::Program {
public:
    /// `request` and what it points to outlive the WindowHost; its lent
    /// capabilities are replaced by the player's devices.
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

    host::HostRequest request_;
    WindowHostSettings settings_;
    input::Feed feed_;
    std::array<composition::LentCapability, 1> lent_;
    std::optional<input_window::Bridge> bridge_;
    std::unique_ptr<host::Host> host_;
    execution::SteadyClock clock_;
    std::optional<host::HostExit> exit_;
};

} // namespace rawframe::window_host
