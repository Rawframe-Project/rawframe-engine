#pragma once

// How a client's UI is navigated without a pointer (SPEC-0030, D430): a
// local player's navigation actions, from a gamepad or keys, move focus
// between the nodes that take presses and the text fields of their view,
// activate the one focused, and leave. A client host lends one as
// `rawframe.view.ui_navigation`: the UI tells it how to answer while it
// runs, and the first local player's input asks it as its navigation
// actions are pressed, before its sample function reads what was pressed.

#include "rawframe/composition/participant.h"

#include <cstdint>
#include <functional>
#include <optional>
#include <utility>

namespace rawframe::view {

/// Where focus goes: to the nearest node in a direction on the screen, or
/// to the next or the one before in reading order, round from the last.
enum class NavigationMove : std::uint8_t {
    Up,
    Down,
    Left,
    Right,
    Next,
    Previous
};

class UiNavigation {
public:
    /// How the UI answers, each empty while none does.
    struct Answers {
        /// Focus to the first node of the first view that has one, unless
        /// a node holds it already; whether one holds it after.
        std::function<bool()> enter;
        /// Focus moved; whether it moved.
        std::function<bool(NavigationMove move)> move;
        /// The focused node activated: its press code for a node that
        /// takes presses; none for a text field, which takes the keyboard.
        std::function<std::optional<std::int64_t>()> activate;
        /// The keyboard taken back from a field, focus staying on it, or
        /// with no keyboard held, focus let go.
        std::function<void()> dismiss;
        /// Whether a node holds focus.
        std::function<bool()> focused;
    };

    UiNavigation() = default;
    UiNavigation(const UiNavigation&) = delete;
    UiNavigation& operator=(const UiNavigation&) = delete;

    /// The UI's side: how it answers from now on; empty, as the UI goes.
    void answer(Answers answers) noexcept {
        answers_ = std::move(answers);
    }

    /// The input's side, each doing nothing while no UI answers.
    bool enter() const {
        return answers_.enter ? answers_.enter() : false;
    }
    bool move(NavigationMove move) const {
        return answers_.move ? answers_.move(move) : false;
    }
    [[nodiscard]] std::optional<std::int64_t> activate() const {
        return answers_.activate ? answers_.activate() : std::nullopt;
    }
    void dismiss() const {
        if (answers_.dismiss) {
            answers_.dismiss();
        }
    }
    [[nodiscard]] bool focused() const {
        return answers_.focused ? answers_.focused() : false;
    }

private:
    Answers answers_;
};

inline constexpr composition::Capability<UiNavigation> kUiNavigation{"rawframe.view.ui_navigation"};

} // namespace rawframe::view
