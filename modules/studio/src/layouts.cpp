#include "shell.h"
#include "surfaces.h"

#include <array>
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace rawframe::studio {

namespace {

/// A bottom-dock tab by the name the surface table places there, so a
/// layout names no dock the table does not.
consteval std::string_view bottom(std::string_view name) {
    return placed(name, SurfaceKind::Panel, "Bottom");
}

} // namespace

std::string_view ShellParticipant::tabOf(Dock pane) {
    // In the order of the dock's panes.
    constexpr std::array<std::string_view, 11> kTabs = {bottom("Assets"),
                                                        bottom("History"),
                                                        bottom("Console"),
                                                        bottom("Network"),
                                                        placed("Environment", SurfaceKind::Panel, "Right"),
                                                        bottom("Profiler"),
                                                        bottom("Profiler"),
                                                        bottom("Mixer"),
                                                        bottom("Changes"),
                                                        bottom("Curves"),
                                                        bottom("Extension panels")};
    static_assert(kTabs.size() == static_cast<std::size_t>(Dock::Panels) + 1);
    return kTabs.at(static_cast<std::size_t>(pane));
}

std::optional<ShellParticipant::Dock> ShellParticipant::dockOf(std::string_view tab) {
    for (std::size_t at = 0; at <= static_cast<std::size_t>(Dock::Panels); ++at) {
        if (tabOf(static_cast<Dock>(at)) == tab) {
            return static_cast<Dock>(at);
        }
    }
    return std::nullopt;
}

std::vector<Layout> ShellParticipant::layouts() const {
    // Studio's six: the scene's docks as they open; an
    // animation's with its history beside the timeline; materials' with a
    // wide Inspector for their parameters; a UI's with the Hierarchy and
    // the Inspector wide for its nodes; a network test's with the Network
    // panel tall; profiling's with the Profiler.
    std::vector<Layout> made = {
        Layout{.name = "Scene", .hierarchyWidth = 360, .inspectorWidth = 360, .dockHeight = 0, .dock = "Assets"},
        Layout{.name = "Animation", .hierarchyWidth = 300, .inspectorWidth = 320, .dockHeight = 280, .dock = "History"},
        Layout{.name = "Materials and effects",
               .hierarchyWidth = 280,
               .inspectorWidth = 440,
               .dockHeight = 0,
               .dock = "Assets"},
        Layout{.name = "UI", .hierarchyWidth = 420, .inspectorWidth = 420, .dockHeight = 0, .dock = "Assets"},
        Layout{
            .name = "Network test", .hierarchyWidth = 300, .inspectorWidth = 320, .dockHeight = 320, .dock = "Network"},
        Layout{
            .name = "Profiling", .hierarchyWidth = 300, .inspectorWidth = 320, .dockHeight = 340, .dock = "Profiler"}};
    made.insert(made.end(), preferences_.layouts.begin(), preferences_.layouts.end());
    return made;
}

void ShellParticipant::chooseLayout(const Layout& layout) {
    preferences_.hierarchyWidth = layout.hierarchyWidth;
    preferences_.inspectorWidth = layout.inspectorWidth;
    preferences_.dockHeight = layout.dockHeight;
    preferences_ = held(preferences_);
    keepPreferences();
    // The same docks, at the layout's sizes, its tab shown.
    rebuild();
    if (const std::optional<Dock> kPane = dockOf(layout.dock); kPane.has_value()) {
        showDock(*kPane);
    }
    ++held_.layoutsChosen;
    // The tab shown, by the name the surface table places it under.
    emitter_.log(diagnostics::Severity::Info,
                 kLayoutChosen,
                 "a layout of the docks put in place",
                 {diagnostics::field("layout", std::string_view{layout.name}),
                  diagnostics::field("dock", tabOf(dockShown_)),
                  diagnostics::field("chosen", held_.layoutsChosen)});
    say("layout " + layout.name);
}

void ShellParticipant::saveLayout() {
    if (preferences_.layouts.size() == kMostSavedLayouts) {
        say("at most " + std::to_string(kMostSavedLayouts) + " layouts are kept");
        return;
    }
    Layout made{.name = "Saved " + std::to_string(preferences_.layouts.size() + 1),
                .hierarchyWidth = preferences_.hierarchyWidth,
                .inspectorWidth = preferences_.inspectorWidth,
                .dockHeight = preferences_.dockHeight,
                .dock = std::string{tabOf(dockShown_)}};
    preferences_.layouts.push_back(made);
    keepPreferences();
    // Its command in the Window menu from now on.
    rebuild();
    emitter_.log(diagnostics::Severity::Info,
                 kLayoutChosen,
                 "the docks' layout saved as the author's own",
                 {diagnostics::field("layout", std::string_view{made.name}),
                  diagnostics::field("dock", std::string_view{made.dock}),
                  diagnostics::field("chosen", held_.layoutsChosen)});
    say("saved layout " + made.name);
}

} // namespace rawframe::studio
