#include "shell.h"

#include <array>

namespace rawframe::studio {

namespace {

/// The engine's types that light a scene and set how it looks: a sun and
/// a sky, reflection probes, and the post-process passes of
/// rawframe/model.kest.
constexpr std::array<std::string_view, 12> kEnvironment = {"Sun",
                                                           "Sky",
                                                           "ReflectionProbe",
                                                           "AutoExposure",
                                                           "Grading",
                                                           "Bloom",
                                                           "AmbientOcclusion",
                                                           "ScreenSpaceReflections",
                                                           "ContactShadows",
                                                           "DepthOfField",
                                                           "MotionBlur",
                                                           "PostProcess"};

/// The engine type `type` names, if it is one of the environment's: a
/// game's line says `rawframe.model.Sun` or, inside model's own scope, `Sun`.
std::optional<std::string_view> environmentOf(std::string_view type) {
    constexpr std::string_view kModel = "rawframe.model.";
    const std::string_view kShort = type.starts_with(kModel) ? type.substr(kModel.size()) : type;
    const auto kFound = std::ranges::find(kEnvironment, kShort);
    return kFound != kEnvironment.end() ? std::optional{*kFound} : std::nullopt;
}

} // namespace

result::Status ShellParticipant::makeEnvironment() {
    RAWFRAME_TRY_ASSIGN(held_.environmentColumn, pane("Environment"));
    RAWFRAME_TRY_ASSIGN(
        held_.environmentList,
        box(held_.environmentColumn, ui::Layout{.direction = ui::Direction::Column, .gap = 4, .shrink = 0}, 0));
    RAWFRAME_TRY_ASSIGN(
        held_.viewCamera,
        box(held_.environmentColumn,
            ui::Layout{.direction = ui::Direction::Column, .gap = 4, .shrink = 0, .margin = {0, 0, 10, 0}},
            0));
    showEnvironment();
    return {};
}

void ShellParticipant::showEnvironment() {
    for (const ui::Node kLeft : held_.environmentRows) {
        static_cast<void>(tree_->remove(kLeft));
    }
    held_.environmentRows.clear();
    held_.environmentParts.clear();
    held_.environmentAdds.clear();
    const ui::Node kColumn = held_.environmentList;
    const auto kKept = [this](result::Result<ui::Node> made) {
        if (made.has_value()) {
            held_.environmentRows.push_back(*made);
        }
        return made;
    };
    const auto kWords = [&](std::string_view text, std::uint32_t ink, float size, bool strong) {
        if (auto made = kKept(box(kColumn, ui::Layout{.shrink = 0, .padding = {12, 12, 4, 2}}, 0)); made.has_value()) {
            static_cast<void>(strong ? strongWords(*made, text, ink, size) : wrappedWords(*made, text, ink, size));
        }
    };
    if (scene_.empty() || documentChosen()) {
        kWords("Open a scene to see what lights it and how it looks.", look::kLabelInk, look::kBodySize, false);
        return;
    }
    // As the design's Scene settings: the scene, then each of the
    // game's components that light it or set its look, with the entities
    // holding it, each pressed to choose it, or offered to add.
    kWords(documentLabel(scene_), look::kInk, 14, true);
    kWords("environment of this scene", look::kFaintInk, look::kLabelSize, false);
    std::size_t shown = 0;
    std::size_t held = 0;
    for (const Catalog::Component& kComponent : catalog_.components) {
        const std::optional<std::string_view> kType = environmentOf(kComponent.type);
        // One a client makes as it draws is never in a scene.
        if (!kType.has_value() || kComponent.presented) {
            continue;
        }
        ++shown;
        if (auto heading = kKept(box(kColumn,
                                     ui::Layout{.alignItems = ui::Align::Center,
                                                .gap = 8,
                                                .shrink = 0,
                                                .padding = {12, 12, 8, 6},
                                                .margin = {0, 0, 8, 0},
                                                .border = {0, 0, 1, 0}},
                                     0));
            heading.has_value()) {
            static_cast<void>(tree_->setLook(*heading, ui::Look{.borderColor = 0x24262CFFU}));
            const Icon kIcon = *kType == "Sun"               ? Icon::Sun
                               : *kType == "Sky"             ? Icon::Sky
                               : *kType == "ReflectionProbe" ? Icon::Sphere
                                                             : Icon::Camera;
            static_cast<void>(icon(*heading, kIcon, look::kWarn, 14));
            if (auto title = box(*heading, ui::Layout{.shrink = 0}, 0); title.has_value()) {
                static_cast<void>(strongWords(*title, std::string{*kType}, look::kInk));
            }
            if (auto owner = box(*heading, ui::Layout{.shrink = 1}, 0); owner.has_value()) {
                static_cast<void>(monoWords(*owner, kComponent.name, look::kFaintInk, look::kSmallSize));
            }
        }
        const std::optional<Value> kFound = firstAnswer(ask(findRecord(next(), scene_, "", kComponent.id)));
        const Value* kEntities = kFound.has_value() ? kFound->find("entities") : nullptr;
        std::size_t holders = 0;
        for (const Value& kEach : kEntities != nullptr && kEntities->kind() == Value::Kind::Array
                                      ? kEntities->items()
                                      : std::span<const Value>{}) {
            const Value* kId = kEach.find("id");
            const auto kListed =
                kId != nullptr && kId->text() != nullptr ? std::ranges::find(entities_, *kId->text()) : entities_.end();
            if (kListed == entities_.end() || held_.environmentRows.size() >= EditorsHeld::kMostRows) {
                continue;
            }
            const std::size_t kEntity = static_cast<std::size_t>(kListed - entities_.begin());
            const std::string kName = names_[kEntity].empty() ? kId->text()->substr(0, 8) : names_[kEntity];
            if (auto pressed = kKept(box(kColumn,
                                         ui::Layout{.alignItems = ui::Align::Center,
                                                    .gap = 8,
                                                    .shrink = 0,
                                                    .padding = {14, 12, 5, 5},
                                                    .margin = {6, 6, 0, 0}},
                                         0));
                pressed.has_value()) {
                static_cast<void>(tree_->setLook(*pressed, ui::Look{.radius = 4}));
                static_cast<void>(icon(*pressed, Icon::Cube, look::kEntityInk, 13));
                if (auto label = box(*pressed, ui::Layout{.grow = 1}, 0); label.has_value()) {
                    static_cast<void>(words(*label, kName, look::kRowInk, look::kBodySize));
                    static_cast<void>(
                        tree_->setInteraction(*label, ui::Interaction{.hits = ui::Interaction::Hits::Nothing}));
                }
                if (auto edit = box(*pressed, ui::Layout{.shrink = 0}, 0); edit.has_value()) {
                    static_cast<void>(words(*edit, "Edit", look::kAccent, look::kLabelSize));
                    static_cast<void>(
                        tree_->setInteraction(*edit, ui::Interaction{.hits = ui::Interaction::Hits::Nothing}));
                }
                static_cast<void>(tree_->setRole(*pressed, ui::Role::Button));
                static_cast<void>(tree_->setName(*pressed, kName + " " + kComponent.name));
                held_.environmentParts.emplace_back(*pressed, kEntity);
            }
            ++holders;
        }
        held += holders;
        if (holders == 0 && catalog_.offers("scene.create_entity") && catalog_.offers("scene.add_component")) {
            if (auto line = kKept(box(kColumn, ui::Layout{.shrink = 0, .padding = {12, 12, 2, 4}}, 0));
                line.has_value()) {
                if (auto add = textButton(*line, Icon::Plus, "Add " + std::string{*kType}); add.has_value()) {
                    static_cast<void>(tree_->setName(*add, "Add " + kComponent.name));
                    held_.environmentAdds.emplace_back(*add, kComponent.id);
                }
            }
        }
    }
    if (shown == 0) {
        kWords("This game keeps no sun, sky, probe, or post-process in its scenes; its scripts set how it looks.",
               look::kLabelInk,
               look::kLabelSize,
               false);
    }
    emitter_.log(diagnostics::Severity::Info,
                 kEnvironmentShown,
                 "the components that light the scene and set its look",
                 {diagnostics::field("components", shown),
                  diagnostics::field("held", held),
                  diagnostics::field("offered", held_.environmentAdds.size())});
}

bool ShellParticipant::pressEnvironment(ui::Node node) {
    const auto kAdd = std::ranges::find(held_.environmentAdds, node, &std::pair<ui::Node, std::string>::first);
    if (kAdd == held_.environmentAdds.end()) {
        return false;
    }
    // An entity named for the component, holding it at its defaults: the
    // Inspector then sets its fields, as for any entity.
    const std::string kIdentity = mintedIdentity();
    const auto kComponent = std::ranges::find(catalog_.components, kAdd->second, &Catalog::Component::id);
    if (kComponent == catalog_.components.end()) {
        return true;
    }
    std::vector<Value> operations;
    Value made = Value::object();
    made.add("operation", Value::string("scene.create_entity"));
    made.add("entity", Value::string(kIdentity));
    made.add("name", Value::string(kComponent->name));
    operations.push_back(std::move(made));
    Value added = Value::object();
    added.add("operation", Value::string("scene.add_component"));
    added.add("entity", Value::string(kIdentity));
    added.add("component", Value::string(kComponent->id));
    operations.push_back(std::move(added));
    commitAll(std::move(operations), kComponent->name + " added", kIdentity);
    return true;
}

} // namespace rawframe::studio
