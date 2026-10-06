#pragma once

// What a client's UI takes of its local players' pointers (ADR-0034,
// SPEC-0029, D421): the UI is the topmost routing node, so a press it takes
// is never a game action's. A client host lends one as
// `rawframe.view.ui_pointing`, as it lends the players' views: the UI tells
// it how to answer while it runs, and each player's input asks it as a
// press begins, its sample function reading what the press landed on.

#include "rawframe/composition/participant.h"

#include <cstdint>
#include <functional>
#include <optional>
#include <utility>

namespace rawframe::view {

class UiPointing {
public:
    /// A press at `x`, `y`, logical pixels from the window's top left, as
    /// the UI was last laid out: none when it passes through to the game,
    /// else the press code of the node it lands on (nought for a node that
    /// blocks and says nothing).
    using Answer = std::function<std::optional<std::int64_t>(float x, float y)>;

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
        return answer_ ? answer_(x, y) : std::nullopt;
    }

private:
    Answer answer_;
};

inline constexpr composition::Capability<UiPointing> kUiPointing{"rawframe.view.ui_pointing"};

} // namespace rawframe::view
