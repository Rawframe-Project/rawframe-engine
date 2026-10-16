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
    const auto kLine = [this](const std::string& words, std::uint32_t color) {
        if (auto made = row(held_.environmentColumn, words, color, kPanel); made.has_value()) {
            held_.environmentRows.push_back(*made);
        }
    };
    if (scene_.empty() || documentChosen()) {
        kLine("choose a scene to see what lights it and how it looks", kQuiet);
        return;
    }
    // Each of the game's components of an environment type, with the
    // scene's entities that hold it, each pressed to choose it; one the
    // scene lacks offered to add, as an entity of its own (D834).
    std::size_t shown = 0;
    std::size_t held = 0;
    for (const Catalog::Component& kComponent : catalog_.components) {
        const std::optional<std::string_view> kType = environmentOf(kComponent.type);
        if (!kType.has_value()) {
            continue;
        }
        ++shown;
        kLine(kComponent.name + " (" + std::string{*kType} + ")", kText);
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
            auto pressed = button(held_.environmentColumn, "  " + kName + " " + kComponent.name);
            if (pressed.has_value()) {
                held_.environmentRows.push_back(*pressed);
                held_.environmentParts.emplace_back(*pressed, kEntity);
            }
            ++holders;
        }
        held += holders;
        if (holders == 0 && catalog_.offers("scene.create_entity") && catalog_.offers("scene.add_component")) {
            auto add = button(held_.environmentColumn, "  Add " + kComponent.name);
            if (add.has_value()) {
                held_.environmentRows.push_back(*add);
                held_.environmentAdds.emplace_back(*add, kComponent.id);
            }
        }
    }
    if (shown == 0) {
        kLine("the game declares no sun, sky, probe, or post-process component", kQuiet);
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
