#pragma once

// The engine's navigation actions (SPEC-0029, SPEC-0030, D430): how a
// player moves focus through a UI without a pointer, from the keyboard and
// the gamepad, declared by the engine, not each game. They are a context of
// their own above every context a game declares, so while the UI holds
// focus their controls are its alone, and a game's actions never see them.

#include "rawframe/input/actions.h"
#include "rawframe/result/result.h"

#include <array>
#include <cstddef>
#include <cstdint>

namespace rawframe::input {

enum class NavigationAction : std::uint8_t {
    Up,
    Down,
    Left,
    Right,
    Next,
    Previous,
    Activate,
    Dismiss
};

inline constexpr std::size_t kNavigationActions = 8;

/// Where the navigation actions are in a set they were added to.
struct Navigation {
    std::size_t context = 0;
    /// Each action's index, in NavigationAction's order.
    std::array<std::size_t, kNavigationActions> actions{};
};

/// `set` with the navigation actions added, in a context of the highest
/// priority, gated while a text field holds the keyboard: the arrow keys and
/// the d-pad move; Tab and the right shoulder go to the next, Shift+Tab and
/// the left shoulder to the one before; Enter, Space, and the south face
/// button activate; Escape and the east face button dismiss. Refuses
/// (`AlreadyExists`) a set that already has an action or context of their
/// names or identities.
[[nodiscard]] result::Result<Navigation> addNavigation(ActionSet& set);

} // namespace rawframe::input
