// A pane shown apart (D988): the author tears a dock's pane out into a
// window of its own, as a large editor's panels are, its tab's context
// menu's "Open panel in a window" or its tab dragged out of the window. It
// shows there until that window is closed, then goes back to its dock.

#include "shell.h"

#include <algorithm>
#include <string>

namespace rawframe::studio {

namespace {

/// The size a window apart opens at, in logical pixels.
constexpr float kApartWidth = 720;
constexpr float kApartHeight = 480;

} // namespace

void ShellParticipant::tearOut(Dock pane) {
    pane = dockOwner(pane);
    if (docking_.windows == nullptr || !movable(pane) || std::ranges::contains(docking_.torn, pane, &TornPane::pane)) {
        return;
    }
    // One pane always stays under the scene.
    const auto kLeftBelow = [this](Dock other) {
        return !dockedRight(other) && !std::ranges::contains(docking_.torn, other, &TornPane::pane);
    };
    if (kLeftBelow(pane) && std::ranges::count_if(kMovablePanes, kLeftBelow) <= 1) {
        ++refused_;
        say("one panel stays under the scene");
        return;
    }
    // The dock showing it shows another.
    if (docking_.shown.has_value() && dockOwner(*docking_.shown) == pane) {
        showRight(false);
    }
    if (dockOwner(dockShown_) == pane) {
        const auto kNext = std::ranges::find_if(kMovablePanes, [&](Dock other) {
            return other != pane && kLeftBelow(other);
        });
        showDock(*kNext);
    }
    const ui::Node kColumn = paneNode(pane);
    auto root = box(std::nullopt,
                    ui::Layout{.width = ui::share(1), .height = ui::share(1), .direction = ui::Direction::Column},
                    kBackground);
    if (!root.has_value() || !tree_->detach(kColumn).has_value() || !tree_->attach(*root, kColumn).has_value()) {
        return;
    }
    const std::uint64_t kKey = docking_.nextKey++;
    docking_.torn.push_back(TornPane{.pane = pane, .key = kKey, .root = *root});
    arrangeTabs();
    lightTabs();
    const std::string kName{tabOf(pane)};
    docking_.windows->open(kKey, kName + " - Rawframe Studio", kApartWidth, kApartHeight);
    // What it shows is read in from now, as a dock's shown pane is.
    showDock(pane);
    emitter_.log(diagnostics::Severity::Info,
                 kDocked,
                 "a panel docked",
                 {diagnostics::field("panel", std::string_view{kName}),
                  diagnostics::field("side", std::string_view{"apart"}),
                  diagnostics::field("right", static_cast<std::uint64_t>(docking_.right.size()))});
    say(kName + " opened in a window");
}

void ShellParticipant::followAsides(double seconds) {
    docking_.asides.clear();
    if (docking_.windows == nullptr) {
        return;
    }
    using Kind = view::WindowAsides::Happened::Kind;
    for (const view::WindowAsides::Happened& kHappened : docking_.windows->take()) {
        const auto kTorn = std::ranges::find(docking_.torn, kHappened.key, &TornPane::key);
        if (kTorn == docking_.torn.end()) {
            continue;
        }
        const float kX = kHappened.x * laidOutPerPointer();
        const float kY = kHappened.y * laidOutPerPointer();
        switch (kHappened.kind) {
        case Kind::Opened:
            kTorn->window = kHappened.window;
            break;
        case Kind::Moved:
            kTorn->pointer = {kX, kY};
            break;
        case Kind::Pressed: {
            // Pressed where it lands in the pane, as in the window; a
            // secondary press chooses a row and opens no menu.
            kTorn->pointer = {kX, kY};
            const bool kMoving = beforePress();
            const auto kHit = tree_->hit(kTorn->root, kX, kY);
            if (kHit.has_value() && kHit->node.has_value() && (!kHappened.secondary || choosable(*kHit->node))) {
                pressOn(*kHit->node, kMoving);
            }
            break;
        }
        case Kind::Wheeled:
            static_cast<void>(tree_->layOut(kTorn->root,
                                            static_cast<float>(kTorn->width) / scale_,
                                            static_cast<float>(kTorn->height) / scale_,
                                            seconds));
            static_cast<void>(tree_->wheel(
                kTorn->root, kTorn->pointer[0], kTorn->pointer[1], kHappened.deltaX, kHappened.deltaY, seconds));
            break;
        case Kind::Closed: {
            // Back to its dock, shown there.
            const Dock kPane = kTorn->pane;
            static_cast<void>(tree_->detach(paneNode(kPane)));
            static_cast<void>(tree_->remove(kTorn->root));
            docking_.windows->close(kTorn->key);
            docking_.torn.erase(kTorn);
            arrangeTabs();
            showDock(kPane);
            say(std::string{tabOf(kPane)} + " back in its dock");
            break;
        }
        case Kind::Released:
        case Kind::Left:
            break;
        }
    }
    for (TornPane& torn : docking_.torn) {
        if (torn.window == 0 || torn.width == 0 || torn.height == 0 ||
            !tree_
                 ->layOut(torn.root,
                          static_cast<float>(torn.width) / scale_,
                          static_cast<float>(torn.height) / scale_,
                          seconds)
                 .has_value()) {
            continue;
        }
        torn.list = {};
        if (tree_->draw(torn.root, scale_, torn.list).has_value()) {
            if (edit_ != nullptr) {
                static_cast<void>(edit_->decorate(torn.root, torn.list));
            }
            docking_.asides.push_back(Aside{.window = torn.window, .drawn = &torn.list});
        }
    }
}

void ShellParticipant::closeAsides() {
    for (const TornPane& kTorn : docking_.torn) {
        static_cast<void>(tree_->remove(kTorn.root));
        if (docking_.windows != nullptr) {
            docking_.windows->close(kTorn.key);
        }
    }
    docking_.torn.clear();
    docking_.asides.clear();
}

std::span<const ui::UiFrames::Aside> ShellParticipant::asides() const noexcept {
    return docking_.asides;
}

void ShellParticipant::resizeAside(std::uint64_t window, std::uint32_t width, std::uint32_t height) noexcept {
    const auto kTorn = std::ranges::find(docking_.torn, window, &TornPane::window);
    if (kTorn != docking_.torn.end() && width != 0 && height != 0) {
        kTorn->width = width;
        kTorn->height = height;
    }
}

} // namespace rawframe::studio
