#pragma once

// An editor's asking to show its window once it has drawn into it (D942):
// a host told `window.shown_when_asked` makes its window hidden and lends
// one as `rawframe.view.window_showing`, showing the window the frame after
// it is asked, so a program with nothing to draw with never stands as a
// blank window; a host with no window lends none.

#include "rawframe/composition/participant.h"

#include <utility>

namespace rawframe::view {

class WindowShowing {
public:
    /// The editor's side: the window shown when the host next looks.
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

inline constexpr composition::Capability<WindowShowing> kWindowShowing{"rawframe.view.window_showing"};

} // namespace rawframe::view
