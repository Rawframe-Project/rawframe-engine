#include "shell.h"

#include <algorithm>
#include <map>

namespace rawframe::studio {

namespace {

constexpr std::string_view kLayerComponent = "rawframe.world.layer";

} // namespace

result::Result<ui::Node>
ShellParticipant::tabbedColumn(ui::Node parent, std::string_view heading, std::string_view other, ui::Node& tab) {
    // The left dock's height, scrolling what does not fit (D441), as
    // column() lays a column out across the window's width.
    RAWFRAME_TRY_ASSIGN(const ui::Node kColumn,
                        box(parent,
                            ui::Layout{.height = ui::pixels(0),
                                       .minHeight = ui::pixels(0),
                                       .direction = ui::Direction::Column,
                                       .gap = 4,
                                       .grow = 1,
                                       .padding = {8, 8, 8, 8},
                                       .scroll = ui::Scroll::Vertical},
                            kPanel));
    RAWFRAME_TRY_ASSIGN(
        const ui::Node kHeading,
        box(kColumn, ui::Layout{.height = ui::pixels(22), .alignItems = ui::Align::Center, .shrink = 0}, 0));
    RAWFRAME_TRY_ASSIGN(const ui::Node kTitle, box(kHeading, ui::Layout{.grow = 1}, 0));
    RAWFRAME_TRY(words(kTitle, heading, kQuiet, 13));
    // The other panel of the dock, named so a press finds it (D788).
    RAWFRAME_TRY_ASSIGN(tab, box(kHeading, ui::Layout{.padding = {6, 2, 6, 2}}, kRow));
    RAWFRAME_TRY(pressWords(tab, other, kText, 13));
    RAWFRAME_TRY(tree_->setRole(kColumn, ui::Role::ScrollView));
    RAWFRAME_TRY(tree_->setName(kColumn, heading));
    return kColumn;
}

void ShellParticipant::showLeft(bool layers) {
    if (layers != layersShown_) {
        static_cast<void>(tree_->detach(layers ? entitiesColumn_ : layersColumn_));
        static_cast<void>(tree_->attach(leftDock_, layers ? layersColumn_ : entitiesColumn_));
        layersShown_ = layers;
    }
    if (layers) {
        showLayers();
    }
}

std::string ShellParticipant::layerName(std::uint32_t layer) const {
    // The game's names for its layers, from its field line (D783).
    if (const Catalog::Component* kLayerType = catalog_.component(kLayerComponent); kLayerType != nullptr) {
        for (const Catalog::Field& kLayerField : kLayerType->fields) {
            for (const auto& [kValue, kName] : kLayerField.labels) {
                if (kLayerField.name == "layer" && kValue == static_cast<std::int64_t>(layer)) {
                    return kName;
                }
            }
        }
    }
    return "layer " + std::to_string(layer);
}

void ShellParticipant::showLayers() {
    clear(layerRows_);
    layerPresses_.clear();
    if (scene_.empty()) {
        return;
    }
    // Every layer the game names, then any other an entity is on, each
    // with the entities on it.
    std::map<std::uint32_t, std::vector<std::string>> members;
    if (const Catalog::Component* kLayerType = catalog_.component(kLayerComponent); kLayerType != nullptr) {
        for (const Catalog::Field& kLayerField : kLayerType->fields) {
            for (const auto& [kValue, kName] : kLayerField.labels) {
                if (kLayerField.name == "layer" && kValue > 0) {
                    members[static_cast<std::uint32_t>(kValue)];
                }
            }
        }
    }
    for (std::size_t at = 0; at < entityLayers_.size() && at < entities_.size(); ++at) {
        if (entityLayers_[at] != 0) {
            members[entityLayers_[at]].push_back(entities_[at]);
        }
    }
    if (members.empty()) {
        if (auto none = row(layersColumn_, "no layers: put entities on one with rawframe.world.layer", kQuiet, kPanel);
            none.has_value()) {
            layerRows_.push_back(*none);
        }
        return;
    }
    for (const auto& [kLayer, kEntities] : members) {
        const bool kLocked = std::ranges::contains(locked_, kLayer);
        auto line = box(layersColumn_, ui::Layout{.height = ui::pixels(28), .gap = 6, .shrink = 0}, 0);
        if (!line.has_value()) {
            return;
        }
        layerRows_.push_back(*line);
        // Its name and count, pressed to choose its entities: the named
        // selection; then whether the preview shows it, and its lock.
        auto chosen = box(*line, ui::Layout{.grow = 1, .padding = {8, 4, 8, 4}}, kRow);
        auto hide = box(*line, ui::Layout{.padding = {10, 4, 10, 4}}, kRow);
        auto lock = box(*line, ui::Layout{.padding = {10, 4, 10, 4}}, kRow);
        if (!chosen.has_value() || !hide.has_value() || !lock.has_value()) {
            return;
        }
        const bool kHidden = std::ranges::contains(hiddenLayers_, kLayer);
        static_cast<void>(
            pressWords(*hide, kHidden ? "Show " + layerName(kLayer) : "Hide " + layerName(kLayer), kText, 14));
        static_cast<void>(
            pressWords(*chosen,
                       layerName(kLayer) + "  " + std::to_string(kEntities.size()) + (kLocked ? "  (locked)" : ""),
                       kLocked ? kQuiet : kText,
                       14));
        static_cast<void>(
            pressWords(*lock, kLocked ? "Unlock " + layerName(kLayer) : "Lock " + layerName(kLayer), kText, 14));
        layerPresses_.push_back({*chosen, LayerPress{.layer = kLayer, .does = LayerPress::Does::Choose}});
        layerPresses_.push_back({*hide, LayerPress{.layer = kLayer, .does = LayerPress::Does::Hide}});
        layerPresses_.push_back({*lock, LayerPress{.layer = kLayer, .does = LayerPress::Does::Lock}});
    }
}

bool ShellParticipant::pressLayers(ui::Node node) {
    if (node == layersTab_ || node == hierarchyTab_) {
        endEdit();
        showLeft(node == layersTab_);
        return true;
    }
    const auto kPress = std::ranges::find(layerPresses_, node, &std::pair<ui::Node, LayerPress>::first);
    if (kPress == layerPresses_.end()) {
        return false;
    }
    const LayerPress kLayer = kPress->second;
    if (kLayer.does == LayerPress::Does::Hide) {
        // Left out of the preview, or shown again; how an author looks,
        // kept while Studio runs as a lock is.
        if (const auto kThere = std::ranges::find(hiddenLayers_, kLayer.layer); kThere != hiddenLayers_.end()) {
            hiddenLayers_.erase(kThere);
            say(layerName(kLayer.layer) + " shown");
        } else {
            hiddenLayers_.push_back(kLayer.layer);
            say(layerName(kLayer.layer) + " hidden");
        }
        tellHidden();
        showLayers();
        return true;
    }
    if (kLayer.does == LayerPress::Does::Lock) {
        // Kept while Studio runs: how an author works, not the game's.
        if (const auto kThere = std::ranges::find(locked_, kLayer.layer); kThere != locked_.end()) {
            locked_.erase(kThere);
            say(layerName(kLayer.layer) + " unlocked");
        } else {
            locked_.push_back(kLayer.layer);
            ++locks_;
            say(layerName(kLayer.layer) + " locked");
        }
        showLayers();
        return true;
    }
    // Its entities chosen, as the Hierarchy's Control presses choose them
    // (D787).
    selected_.clear();
    std::optional<std::size_t> first;
    for (std::size_t at = 0; at < entityLayers_.size() && at < entities_.size(); ++at) {
        if (entityLayers_[at] == kLayer.layer) {
            selected_.push_back(entities_[at]);
            first = first.value_or(at);
        }
    }
    if (!first.has_value()) {
        say(layerName(kLayer.layer) + " has no entities");
        return true;
    }
    anchor_ = first;
    ++layersChosen_;
    if (selected_.size() > 1) {
        showSelection(*first);
    } else {
        showEntity(*first);
    }
    say(layerName(kLayer.layer) + " chosen");
    return true;
}

void ShellParticipant::tellHidden() {
    if (scene_.empty()) {
        return;
    }
    const std::string kReply = ask(hideLayersRecord(next(), scene_, hiddenLayers_));
    if (kReply.find("\"hidden\":true") != std::string::npos) {
        ++layersHidden_;
    }
}

std::optional<std::string> ShellParticipant::lockedOf(const std::string& entity) const {
    const auto kAt = std::ranges::find(entities_, entity);
    if (kAt == entities_.end()) {
        return std::nullopt;
    }
    const auto kPlace = static_cast<std::size_t>(kAt - entities_.begin());
    if (kPlace < entityLayers_.size() && std::ranges::contains(locked_, entityLayers_[kPlace])) {
        return layerName(entityLayers_[kPlace]);
    }
    return std::nullopt;
}

} // namespace rawframe::studio
