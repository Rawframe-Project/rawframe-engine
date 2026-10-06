#pragma once

// What a client's UI takes of its local players' pointers (ADR-0034,
// SPEC-0029, D421): the UI is the topmost routing node, so a press it takes
// is never a game action's. A client host lends one as
// `rawframe.view.ui_pointing`, as it lends the players' views: the UI tells
// it how to answer while it runs, and each player's input asks it as a
// press begins, its sample function reading what the press landed on. The
// host tells it where the mouse is over the window, and the presentation
// asks what lies under it, for a present system to light (D422).

#include "rawframe/composition/participant.h"

#include <array>
#include <cstdint>
#include <functional>
#include <optional>
#include <utility>

namespace rawframe::view {

class UiPointing {
public:
    /// A press at `x`, `y`, logical pixels from the window's top left, as
    /// the UI was last laid out, or with `pressing` false only the mouse
    /// there: none when it passes through to the game, else the press code
    /// of the node it lands on (nought for a node that blocks and says
    /// nothing).
    using Answer = std::function<std::optional<std::int64_t>(float x, float y, bool pressing)>;

    UiPointing() = default;
    UiPointing(const UiPointing&) = delete;
    UiPointing& operator=(const UiPointing&) = delete;

    /// The UI's side: how presses are answered from now on; empty, as the
    /// UI goes, for none taken.
    void answer(Answer answer) noexcept {
        answer_ = std::move(answer);
    }

    /// The input's side: what a press at `x`, `y` lands on, none while no
    /// UI answers.
    [[nodiscard]] std::optional<std::int64_t> press(float x, float y) const {
        return answer_ ? answer_(x, y, true) : std::nullopt;
    }

    /// What the UI does as a press goes down at `x`, `y` (D426): a text
    /// field there takes the keyboard, one elsewhere lets it go.
    using Pressed = std::function<void(float x, float y)>;

    /// The UI's side: what a press going down does from now on; empty, as
    /// the UI goes, for nothing.
    void onPress(Pressed pressed) noexcept {
        pressed_ = std::move(pressed);
    }
    /// The host's side: a mouse button or a touch went down at `x`, `y`,
    /// told in the order of the window's records, so the keys that follow
    /// find the keyboard where the press put it.
    void pressed(float x, float y) const {
        if (pressed_) {
            pressed_(x, y);
        }
    }

    /// What the UI does as a press is let go (D431): the node it went down
    /// on is no longer pressed.
    using Released = std::function<void()>;
    void onRelease(Released released) noexcept {
        released_ = std::move(released);
    }
    /// The host's side: a mouse button or a touch was let go.
    void released() const {
        if (released_) {
            released_();
        }
    }
    /// What the UI does as a wheel turns (D441): `x`, `y` where the mouse
    /// is, logical pixels from the window's top left, and `deltaX`,
    /// `deltaY` detents, positive y away from the user.
    using Wheeled = std::function<void(float x, float y, float deltaX, float deltaY)>;
    void onWheel(Wheeled wheeled) noexcept {
        wheeled_ = std::move(wheeled);
    }
    /// The host's side: a wheel turned by `deltaX`, `deltaY` detents with
    /// the mouse over the window; told nowhere while it is off it.
    void wheeled(float deltaX, float deltaY) const {
        if (wheeled_ && pointed_) {
            wheeled_(pointer_[0], pointer_[1], deltaX, deltaY);
        }
    }

    /// The host's side: the mouse is at `x`, `y` over the window, or has
    /// left it.
    void pointAt(float x, float y) noexcept {
        pointer_ = {x, y};
        pointed_ = true;
    }
    void left() noexcept {
        pointed_ = false;
    }

    /// What a press where the mouse is now would land on: none off the
    /// window, while no UI answers, or where a press passes through.
    [[nodiscard]] std::optional<std::int64_t> hovered() const {
        return pointed_ && answer_ ? answer_(pointer_[0], pointer_[1], false) : std::nullopt;
    }
    /// Where the mouse is over the window; none off it.
    [[nodiscard]] std::optional<std::array<float, 2>> pointer() const noexcept {
        return pointed_ ? std::optional{pointer_} : std::nullopt;
    }

private:
    Answer answer_;
    Pressed pressed_;
    Released released_;
    Wheeled wheeled_;
    std::array<float, 2> pointer_{};
    bool pointed_ = false;
};

inline constexpr composition::Capability<UiPointing> kUiPointing{"rawframe.view.ui_pointing"};

} // namespace rawframe::view
