#pragma once

// Studio's docks' panes (D980): the bottom dock's, one shown at a time, of
// which the author docks some on the right beside the Inspector.

#include "rawframe/ui/frames.h"
#include "rawframe/ui/tree.h"
#include "rawframe/view/asides.h"

#include <array>
#include <cstdint>
#include <optional>
#include <string_view>
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

/// The command that shows a pane apart (D988).
inline constexpr std::string_view kOpenApart = "Open panel in a window";

constexpr Dock dockOwner(Dock pane) {
    return pane == Dock::Frame ? Dock::Profiler : pane;
}

/// A pane shown apart in a window of its own (D988): its key, the window
/// once opened, the root it is laid out in, the window's size in device
/// pixels, what it drew, and where its pointer is, laid out.
struct TornPane {
    Dock pane = Dock::Assets;
    std::uint64_t key = 0;
    std::uint64_t window = 0;
    ui::Node root{};
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    ui::DrawList list;
    std::array<float, 2> pointer{};
};

/// The panes docked on the right, the one the right dock shows if any, the
/// two docks' tab strips, and a tab held to be dragged to the other dock;
/// the panes shown apart, the windows the host opens for them, what each
/// drew for the device, the pane a tab's context menu names, and the next
/// key (D988).
struct Docking {
    std::vector<Dock> right;
    std::optional<Dock> shown;
    ui::Node dockTabs{};
    ui::Node rightTabs{};
    std::optional<Dock> held;
    std::vector<TornPane> torn;
    view::WindowAsides* windows = nullptr;
    std::vector<ui::UiFrames::Aside> asides;
    std::optional<Dock> contextPane;
    std::uint64_t nextKey = 1;
    /// The pane apart the last press was in, by its key, nought for
    /// Studio's own window: a menu or choices it opens show there (D990).
    std::uint64_t pressedApart = 0;
};

/// The pane apart the last press was in, if it is still apart.
inline const TornPane* pressedApart(const Docking& docking) {
    for (const TornPane& kEach : docking.torn) {
        if (kEach.key == docking.pressedApart) {
            return &kEach;
        }
    }
    return nullptr;
}

} // namespace rawframe::studio
