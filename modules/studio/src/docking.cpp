#include "docking.h"

#include "shell.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace rawframe::studio {

ui::Node ShellParticipant::tabNode(Dock pane) const {
    switch (dockOwner(pane)) {
    case Dock::Assets:
        return assetsTab_;
    case Dock::History:
        return historyTab_;
    case Dock::Console:
        return consoleTab_;
    case Dock::Network:
        return networkTab_;
    case Dock::Environment:
        return environmentTab_;
    case Dock::Mixer:
        return held_.mixerTab;
    case Dock::Changes:
        return held_.changesTab;
    case Dock::Curves:
        return held_.curvesTab;
    case Dock::Panels:
        return held_.panelsTab;
    default:
        return profilerTab_;
    }
}

ui::Node ShellParticipant::paneNode(Dock pane) const {
    switch (pane) {
    case Dock::Assets:
        return scenesColumn_;
    case Dock::History:
        return historyColumn_;
    case Dock::Console:
        return consoleColumn_;
    case Dock::Network:
        return networkColumn_;
    case Dock::Environment:
        return held_.environmentColumn;
    case Dock::Profiler:
        return held_.profilerColumn;
    case Dock::Frame:
        return held_.frameColumn;
    case Dock::Mixer:
        return held_.mixerColumn;
    case Dock::Changes:
        return held_.changesColumn;
    case Dock::Curves:
        return held_.curvesColumn;
    case Dock::Panels:
        return held_.panelsColumn;
    }
    return scenesColumn_;
}

bool ShellParticipant::movable(Dock pane) {
    return std::ranges::contains(kMovablePanes, pane);
}

bool ShellParticipant::dockedRight(Dock pane) const {
    return std::ranges::contains(docking_.right, dockOwner(pane));
}

bool ShellParticipant::paneShown(Dock pane) const {
    // In the bottom dock, or the right one where it is docked there, so a
    // pane docked on the right follows what it shows as it does below.
    // Or apart, in a window of its own (D988).
    return dockShown_ == pane || docking_.shown == pane ||
           std::ranges::contains(docking_.torn, dockOwner(pane), &TornPane::pane);
}

ShellParticipant::Dock ShellParticipant::readRightPanes() {
    docking_.right.clear();
    docking_.shown.reset();
    docking_.held.reset();
    // One pane always stays under the scene.
    for (const std::string& kName : preferences_.rightPanels) {
        if (const std::optional<Dock> kPane = dockOf(kName); kPane.has_value() && movable(*kPane) &&
                                                             !dockedRight(*kPane) &&
                                                             docking_.right.size() + 1 < kMovablePanes.size()) {
            docking_.right.push_back(*kPane);
        }
    }
    const auto kLeft = std::ranges::find_if(kMovablePanes, [this](Dock pane) {
        return !dockedRight(pane);
    });
    return kLeft == kMovablePanes.end() ? Dock::Assets : *kLeft;
}

void ShellParticipant::showDock(Dock pane) {
    // The Environment is the right dock's Scene settings (D921).
    if (pane == Dock::Environment) {
        showRight(true);
        return;
    }
    if (std::ranges::contains(docking_.torn, dockOwner(pane), &TornPane::pane)) {
        // Shown apart, where it stays (D988).
    } else if (dockedRight(pane)) {
        // Docked on the right, beside the Inspector (D980).
        if (tree_->contains(held_.rightDock)) {
            showInRight(paneNode(pane));
            docking_.shown = pane;
        }
    } else if (pane != dockShown_) {
        static_cast<void>(tree_->detach(paneNode(dockShown_)));
        static_cast<void>(tree_->attach(bottomDock_, paneNode(pane)));
        dockShown_ = pane;
    }
    lightTabs();
    if (pane == Dock::Console) {
        showConsole();
    } else if (pane == Dock::Mixer) {
        showMixer();
    } else if (pane == Dock::Curves) {
        showCurves();
    } else if (pane == Dock::Changes) {
        readChanges();
    } else if (pane == Dock::Panels) {
        showExtensionPanels();
    }
}

void ShellParticipant::showRight(bool sceneSettings) {
    // The right dock's two tabs: the Inspector, or the scene's settings,
    // the Environment panel (D921).
    if (tree_->contains(held_.rightDock)) {
        showInRight(sceneSettings ? held_.environmentColumn : componentsColumn_);
    }
    held_.sceneSettingsShown = sceneSettings;
    docking_.shown.reset();
    lightTabs();
    if (sceneSettings) {
        showEnvironment();
    }
}

void ShellParticipant::showInRight(ui::Node shown) {
    const ui::Node kShowing = docking_.shown.has_value() ? paneNode(*docking_.shown)
                              : held_.sceneSettingsShown ? held_.environmentColumn
                                                         : componentsColumn_;
    if (kShowing == shown && tree_->contains(shown)) {
        return;
    }
    static_cast<void>(tree_->detach(kShowing));
    static_cast<void>(tree_->attach(held_.rightDock, shown));
}

void ShellParticipant::arrangeTabs() {
    // Each movable tab again at its strip's end, in the docks' order: the
    // bottom strip holds only these; the right's follow its own two.
    if (!tree_->contains(docking_.dockTabs) || !tree_->contains(docking_.rightTabs)) {
        return;
    }
    for (const Dock kPane : kMovablePanes) {
        static_cast<void>(tree_->detach(tabNode(kPane)));
    }
    // A pane shown apart has its window's own title (D988).
    for (const Dock kPane : kMovablePanes) {
        if (!std::ranges::contains(docking_.torn, kPane, &TornPane::pane)) {
            static_cast<void>(
                tree_->attach(dockedRight(kPane) ? docking_.rightTabs : docking_.dockTabs, tabNode(kPane)));
        }
    }
}

void ShellParticipant::lightTabs() {
    const auto kLight = [this](ui::Node tab, bool on) {
        if (tab != ui::Node{} && tree_->contains(tab)) {
            setTab(tab, on);
        }
    };
    for (const Dock kPane : kMovablePanes) {
        const std::optional<Dock> kTabShows = dockedRight(kPane) ? docking_.shown : std::optional<Dock>{dockShown_};
        kLight(tabNode(kPane), kTabShows.has_value() && dockOwner(*kTabShows) == kPane);
    }
    kLight(held_.inspectorTab, !docking_.shown.has_value() && !held_.sceneSettingsShown);
    kLight(held_.sceneSettingsTab, !docking_.shown.has_value() && held_.sceneSettingsShown);
}

void ShellParticipant::dockPane(Dock pane, bool right) {
    pane = dockOwner(pane);
    if (!movable(pane) || dockedRight(pane) == right) {
        return;
    }
    const std::string_view kName = tabOf(pane);
    if (right) {
        if (docking_.right.size() + 1 >= kMovablePanes.size()) {
            ++refused_;
            say("one panel stays under the scene");
            return;
        }
        // The bottom dock shows the next pane left there.
        if (dockOwner(dockShown_) == pane) {
            const auto kNext = std::ranges::find_if(kMovablePanes, [this, pane](Dock other) {
                return other != pane && !dockedRight(other);
            });
            showDock(*kNext);
        }
        docking_.right.push_back(pane);
    } else {
        // The right dock shows the Inspector again.
        if (docking_.shown.has_value() && dockOwner(*docking_.shown) == pane) {
            showRight(false);
        }
        std::erase(docking_.right, pane);
    }
    arrangeTabs();
    showDock(pane);
    preferences_.rightPanels.clear();
    for (const Dock kPane : docking_.right) {
        preferences_.rightPanels.emplace_back(tabOf(kPane));
    }
    keepPreferences();
    emitter_.log(diagnostics::Severity::Info,
                 kDocked,
                 "a panel docked",
                 {diagnostics::field("panel", kName),
                  diagnostics::field("side", std::string_view{right ? "right" : "bottom"}),
                  diagnostics::field("right", static_cast<std::uint64_t>(docking_.right.size()))});
    say(std::string{kName} + (right ? " docked on the right" : " docked under the scene"));
}

void ShellParticipant::dropTab() {
    const std::optional<Dock> kHeld = std::exchange(docking_.held, std::nullopt);
    const std::optional<std::array<float, 2>> kPointer = releasedAt_.has_value() ? releasedAt_ : pointerLaidOut();
    if (!kHeld.has_value() || !kPointer.has_value() ||
        std::hypot((*kPointer)[0] - pressedAt_[0], (*kPointer)[1] - pressedAt_[1]) < kDragAfter) {
        return;
    }
    const auto kOver = [this, &kPointer](ui::Node dock) {
        if (!tree_->contains(dock)) {
            return false;
        }
        const ui::Rect kAt = tree_->placedRectOf(dock);
        return (*kPointer)[0] >= kAt.x && (*kPointer)[1] >= kAt.y && (*kPointer)[0] < kAt.x + kAt.width &&
               (*kPointer)[1] < kAt.y + kAt.height;
    };
    if (kOver(held_.rightDock)) {
        dockPane(*kHeld, true);
    } else if (kOver(bottomDock_)) {
        dockPane(*kHeld, false);
    } else if ((*kPointer)[0] < 0 || (*kPointer)[1] < 0 || (*kPointer)[0] >= static_cast<float>(width_) / scale_ ||
               (*kPointer)[1] >= static_cast<float>(height_) / scale_) {
        // Let go outside the window: shown apart in one of its own (D988).
        tearOut(*kHeld);
    }
}

} // namespace rawframe::studio
