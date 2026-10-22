#pragma once

// Studio's docks' panes (D980): the bottom dock's, one shown at a time, of
// which the author docks some on the right beside the Inspector.

#include "rawframe/ui/tree.h"

#include <array>
#include <cstdint>
#include <optional>
#include <vector>

namespace rawframe::studio {

/// The bottom dock's panes.
enum class Dock : std::uint8_t {
    Assets,
    History,
    Console,
    Network,
    Environment,
    Profiler,
    Frame,
    Mixer,
    Changes,
    Curves,
    Panels
};

/// The panes that move between the docks, in their tabs' order: the
/// Environment is the right's own Scene settings (D921), and the Frame
/// view docks where the Profiler is (D928).
inline constexpr std::array kMovablePanes = {Dock::Assets,
                                             Dock::Console,
                                             Dock::History,
                                             Dock::Network,
                                             Dock::Profiler,
                                             Dock::Mixer,
                                             Dock::Changes,
                                             Dock::Curves,
                                             Dock::Panels};

constexpr Dock dockOwner(Dock pane) {
    return pane == Dock::Frame ? Dock::Profiler : pane;
}

/// The panes docked on the right, the one the right dock shows if any, the
/// two docks' tab strips, and a tab held to be dragged to the other dock.
struct Docking {
    std::vector<Dock> right;
    std::optional<Dock> shown;
    ui::Node dockTabs{};
    ui::Node rightTabs{};
    std::optional<Dock> held;
};

} // namespace rawframe::studio
