#pragma once

// An editor's asking to close its window, as its File menu's Quit does:
// the window host lends one as `rawframe.view.window_closing` and
// closes the window, as a press on its close button does, the next frame;
// a host with no window lends none.

#include "rawframe/composition/participant.h"

#include <utility>

namespace rawframe::view {

class WindowClosing {
public:
    /// The editor's side: the window closed when the host next looks.
    void ask() noexcept {
        asked_ = true;
    }
    /// The host's side: whether it was asked since it last looked.
    [[nodiscard]] bool asked() noexcept {
        return std::exchange(asked_, false);
    }

private:
    bool asked_ = false;
};

inline constexpr composition::Capability<WindowClosing> kWindowClosing{"rawframe.view.window_closing"};

} // namespace rawframe::view
