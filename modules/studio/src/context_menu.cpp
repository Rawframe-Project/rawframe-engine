// A context menu (D973), as the large editors open one where a secondary
// press lands: the Hierarchy's making and editing of entities, the Assets'
// files, a material graph's nodes. Every row is a command or button that
// is already there, so a context menu does nothing a menu or a button does
// not do, and nothing a headless tool cannot.

#include "shell.h"

#include <algorithm>
#include <string>
#include <utility>
#include <vector>

namespace rawframe::studio {

namespace {

/// A row of a context menu: a command by its name, or a button by its
/// words.
struct ContextRow {
    std::string name;
    ui::Node button{};
};

constexpr float kContextWidth = 220;
constexpr float kContextRow = 29;

bool inside(const ui::Rect& rect, float x, float y) noexcept {
    return x >= rect.x && y >= rect.y && x < rect.x + rect.width && y < rect.y + rect.height;
}

} // namespace

void ShellParticipant::showContext(float x, float y) {
    closePopup();
    std::vector<ContextRow> rows;
    const auto kCommand = [this, &rows](std::string_view name) {
        if (std::ranges::any_of(commands_, [name](const Command& each) {
                return each.name == name;
            })) {
            rows.push_back(ContextRow{.name = std::string{name}});
        }
    };
    const auto kButton = [this, &rows](std::string_view name, ui::Node button) {
        if (button != ui::Node{} && tree_->contains(button)) {
            rows.push_back(ContextRow{.name = std::string{name}, .button = button});
        }
    };
    // What the press met, in the window it was in: a pane apart holds only
    // its own (D990).
    const TornPane* kApart = pressedApart(docking_);
    const auto kMet = [&](ui::Node node) {
        return tree_->contains(node) && tree_->placeOf(kApart != nullptr ? kApart->root : root_, node).has_value() &&
               inside(tree_->placedRectOf(node), x, y);
    };
    const bool kInHierarchy = kMet(entitiesColumn_);
    const bool kInAssets = kMet(scenesColumn_);
    const bool kInCenter = kMet(centerColumn_);
    // A dock's tab: its pane shown apart, in a window of its own (D988).
    const auto kTab = std::ranges::find_if(kMovablePanes, [&](Dock pane) {
        return kMet(tabNode(pane));
    });
    docking_.contextPane.reset();
    if (kTab != kMovablePanes.end()) {
        docking_.contextPane = *kTab;
        kCommand(kOpenApart);
    } else if (kInAssets) {
        // Its files, as the Assets' own buttons do them (D929).
        kButton("Import files", browser_.import);
        kButton("New folder", browser_.newFolder);
        kButton("Rename", browser_.rename);
        kButton("Delete", browser_.remove);
    } else if (chosen(Listed::Graph) && (kInHierarchy || kInCenter)) {
        // A material graph's nodes (D728): a node made, and the one chosen
        // removed.
        kButton("New node", newNode_);
        kButton("Delete node", deleteNode_);
    } else if (kInHierarchy || kInCenter) {
        // The parts a scene is given (D917), then what is done to the
        // entity chosen.
        for (const std::string& kPart : partCommands()) {
            kCommand(kPart);
        }
        kCommand("Rename");
        kCommand("Copy");
        kCommand("Paste");
        kCommand("Duplicate");
        kCommand("Delete");
        kCommand("Move to");
        kCommand("Up");
        kCommand("Down");
    }
    if (rows.empty()) {
        return;
    }
    const std::string_view kPart = kTab != kMovablePanes.end() ? "tab"
                                   : kInAssets                 ? "assets"
                                   : chosen(Listed::Graph)     ? "graph"
                                   : kInHierarchy              ? "hierarchy"
                                                               : "center";
    emitter_.log(
        diagnostics::Severity::Info,
        kContextMenu,
        "a context menu opened",
        {diagnostics::field("part", kPart), diagnostics::field("rows", static_cast<std::uint64_t>(rows.size()))});
    // Inside the window, turned back from its right and bottom edges: the
    // pane apart's where the press was there (D990).
    const float kWide = static_cast<float>(kApart != nullptr ? kApart->width : width_) / scale_;
    const float kTall = static_cast<float>(kApart != nullptr ? kApart->height : height_) / scale_;
    const float kHigh = static_cast<float>(rows.size()) * kContextRow + 10;
    const float kLeft = x + kContextWidth > kWide ? std::max(0.0F, x - kContextWidth) : x;
    const float kTop = y + kHigh > kTall ? std::max(0.0F, y - kHigh) : y;
    auto popup = box(kApart != nullptr ? kApart->root : root_,
                     ui::Layout{.width = ui::pixels(kContextWidth),
                                .direction = ui::Direction::Column,
                                .gap = 1,
                                .padding = {5, 5, 5, 5},
                                .border = {1, 1, 1, 1},
                                .placement = {.absolute = true, .x = ui::pixels(kLeft), .y = ui::pixels(kTop)}},
                     0);
    if (!popup.has_value()) {
        return;
    }
    static_cast<void>(tree_->setLook(*popup, kMenuLook));
    static_cast<void>(tree_->setRole(*popup, ui::Role::Group));
    static_cast<void>(tree_->setName(*popup, "Context menu"));
    popup_ = *popup;
    for (const ContextRow& kListed : rows) {
        const auto kCommandAt = std::ranges::find(commands_, kListed.name, &Command::name);
        const Command kLined =
            kListed.button == ui::Node{} && kCommandAt != commands_.end() ? *kCommandAt : Command{.name = kListed.name};
        auto line = menuLine(*popup, kLined, false);
        if (!line.has_value()) {
            return;
        }
        if (kListed.button != ui::Node{}) {
            contextRows_.emplace_back(*line, kListed.button);
        } else {
            popupRows_.emplace_back(*line, static_cast<std::size_t>(kCommandAt - commands_.begin()));
        }
    }
}

bool ShellParticipant::pressContext(ui::Node node) {
    const auto kFound = std::ranges::find(contextRows_, node, &std::pair<ui::Node, ui::Node>::first);
    if (kFound == contextRows_.end()) {
        return false;
    }
    const ui::Node kButton = kFound->second;
    closePopup();
    pressOn(kButton, false);
    return true;
}

} // namespace rawframe::studio
