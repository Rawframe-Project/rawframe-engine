#pragma once

// Windows beside the one shown, where an editor shows a panel apart
// (D988): the editor asks for one by a key of its own, and the window
// host opens it, tells which window it is (`window::WindowId`'s value, as
// `ui::UiFrames::Aside` names it), and gathers what its pointer does there
// and when it is closed. Its keys reach the editor as the window's typing
// does. The window host lends one as `rawframe.view.window_asides`; a host
// with no windows lends none.

#include "rawframe/composition/participant.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace rawframe::view {

class WindowAsides {
public:
    /// What the editor asked: a window opened, or closed.
    struct Asked {
        std::uint64_t key = 0;
        bool close = false;
        std::string title;
        /// Its size in logical pixels.
        float width = 0;
        float height = 0;
        /// Where on the desktop, its top left in logical pixels; none for
        /// where the platform puts it (D994).
        std::optional<std::array<float, 2>> place;
    };
    /// What happened in an aside, its pointer at `x`, `y` in its logical
    /// pixels.
    struct Happened {
        enum class Kind : std::uint8_t {
            Opened,
            Moved,
            Pressed,
            Released,
            Wheeled,
            Left,
            /// The window moved or changed size: `x`, `y` its place on the
            /// desktop, `width`, `height` its logical size (D994).
            Placed,
            Closed
        };
        std::uint64_t key = 0;
        Kind kind = Kind::Moved;
        /// Opened: the window's value.
        std::uint64_t window = 0;
        float x = 0;
        float y = 0;
        /// The same point from the top left of the window the editor shows
        /// itself in, by the two windows' places on the desktop (D992): a
        /// press there let go over that window is let go there.
        float mainX = 0;
        float mainY = 0;
        float deltaX = 0;
        float deltaY = 0;
        /// Placed: its logical size.
        float width = 0;
        float height = 0;
        /// Pressed: with the secondary button.
        bool secondary = false;
    };

    /// The asides open at most, and what is held at most until taken.
    static constexpr std::size_t kMostOpen = 8;
    static constexpr std::size_t kMostHeld = 256;

    /// The editor's side: a window opened for `key`, titled and sized so,
    /// and placed there on the desktop where the platform lets a program
    /// place its windows.
    void open(std::uint64_t key,
              std::string title,
              float width,
              float height,
              std::optional<std::array<float, 2>> place = std::nullopt) {
        asked_.push_back(
            Asked{.key = key, .title = std::move(title), .width = width, .height = height, .place = place});
    }
    /// The editor's side: `key`'s window closed, as its close button does.
    void close(std::uint64_t key) {
        asked_.push_back(Asked{.key = key, .close = true});
    }
    /// The editor's side: what happened since it last took, oldest first.
    [[nodiscard]] std::vector<Happened> take() {
        return std::exchange(happened_, {});
    }

    /// The host's side: what the editor asked since it last looked.
    [[nodiscard]] std::vector<Asked> asked() {
        return std::exchange(asked_, {});
    }
    /// The host's side: what happened in an aside. A pointer's moves past
    /// the limit are let go; an aside's opening and closing never are.
    void happened(const Happened& happened) {
        if (happened_.size() >= kMostHeld && happened.kind != Happened::Kind::Opened &&
            happened.kind != Happened::Kind::Closed) {
            return;
        }
        happened_.push_back(happened);
    }

private:
    std::vector<Asked> asked_;
    std::vector<Happened> happened_;
};

inline constexpr composition::Capability<WindowAsides> kWindowAsides{"rawframe.view.window_asides"};

} // namespace rawframe::view
