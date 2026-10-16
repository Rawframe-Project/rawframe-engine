#include "shell.h"

namespace rawframe::studio {

namespace {

/// The most entities the Hierarchy lists live: a page the server gives.
constexpr std::int64_t kMostListed = 256;

} // namespace

void ShellParticipant::switchLive() {
    if (!live_.on && (!playing_.has_value() || scene_.empty())) {
        say("play the game to inspect its World live");
        return;
    }
    live_.on = !live_.on;
    live_.chosen.clear();
    static_cast<void>(tree_->setLook(live_.button, ui::Look{.fill = live_.on ? kChosen : kRow, .radius = 4}));
    // The chosen scene shown again, its Hierarchy and Inspector the live
    // World's or its own.
    showScene(sceneAt_);
    if (!live_.on) {
        say("the scene again: " + scene_);
    }
}

void ShellParticipant::showLive() {
    live_.rows.clear();
    Value asked = Value::object();
    asked.add("kind", Value::string("authoring.live_entities"));
    asked.add("id", Value::integer(next()));
    asked.add("scene", Value::string(scene_));
    asked.add("limit", Value::integer(kMostListed));
    const std::string kReply = ask(asked);
    const auto kParsed = document::parse(kReply);
    const Value* kAnswer = kParsed.has_value() ? kParsed->find("answer") : nullptr;
    const Value* kListed = kAnswer != nullptr ? kAnswer->find("entities") : nullptr;
    if (kListed == nullptr || kListed->kind() != Value::Kind::Array) {
        say("live: " + answeredOf(kReply).message);
        return;
    }
    for (const Value& kEach : kListed->items()) {
        const Value* kEntity = kEach.find("entity");
        const Value* kComponents = kEach.find("components");
        if (kEntity == nullptr || kEntity->text() == nullptr || kEntity->kind() != Value::Kind::String) {
            continue;
        }
        // Its name and what it holds, the first few components by name.
        std::string line = *kEntity->text() + " ";
        std::size_t named = 0;
        for (const Value& kComponent : kComponents != nullptr ? kComponents->items() : std::span<const Value>{}) {
            if (kComponent.kind() == Value::Kind::String && named < 3) {
                line += (named == 0 ? " " : ", ") + *kComponent.text();
            }
            ++named;
        }
        if (named > 3) {
            line += ", +" + std::to_string(named - 3);
        }
        auto made = row(entitiesColumn_, line, kText, kRow, true);
        if (!made.has_value()) {
            break;
        }
        entityRows_.push_back(*made);
        live_.rows.emplace_back(*made, *kEntity->text());
    }
    ++live_.listed;
    const Value* kMore = kAnswer->find("more");
    const bool kMoreThere = kMore != nullptr && kMore->truth().value_or(false);
    // The one chosen still, as the scene is shown again after an edit, or
    // the first at once, so the Inspector says what the World holds.
    const bool kStill = std::ranges::contains(live_.rows, live_.chosen, &std::pair<ui::Node, std::string>::second);
    if (!live_.rows.empty()) {
        showLiveEntity(kStill ? live_.chosen : live_.rows.front().second);
    }
    say("live: " + std::to_string(live_.rows.size()) + (kMoreThere ? "+" : "") + " entities on the server");
}

void ShellParticipant::showLiveEntity(const std::string& entity) {
    live_.chosen = entity;
    live_.nextRead = 0;
    for (const auto& [kRowNode, kName] : live_.rows) {
        static_cast<void>(tree_->setLook(kRowNode, ui::Look{.fill = kName == entity ? kChosen : kRow, .radius = 4}));
    }
    readLive(0);
}

bool ShellParticipant::pressLive(ui::Node node) {
    if (node == live_.button && live_.button != ui::Node{}) {
        switchLive();
        return true;
    }
    if (!live_.on) {
        return false;
    }
    const auto kListed = std::ranges::find(live_.rows, node, &std::pair<ui::Node, std::string>::first);
    if (kListed == live_.rows.end()) {
        return false;
    }
    showLiveEntity(kListed->second);
    return true;
}

void ShellParticipant::readLive(double seconds) {
    if (!live_.on || live_.chosen.empty() || seconds < live_.nextRead) {
        return;
    }
    // Let go once the game has.
    if (!playing_.has_value()) {
        switchLive();
        return;
    }
    live_.nextRead = seconds + 0.5;
    Value asked = Value::object();
    asked.add("kind", Value::string("authoring.live_entity"));
    asked.add("id", Value::integer(next()));
    asked.add("scene", Value::string(scene_));
    asked.add("entity", Value::string(live_.chosen));
    const std::string kReply = ask(asked);
    const auto kParsed = document::parse(kReply);
    const Value* kAnswer = kParsed.has_value() ? kParsed->find("answer") : nullptr;
    const Value* kComponents = kAnswer != nullptr ? kAnswer->find("components") : nullptr;
    clear(componentRows_);
    if (kComponents == nullptr || kComponents->kind() != Value::Kind::Array) {
        if (auto gone = row(componentsColumn_, live_.chosen + ": " + answeredOf(kReply).message, kQuiet, kPanel);
            gone.has_value()) {
            componentRows_.push_back(*gone);
        }
        return;
    }
    if (auto heading = row(componentsColumn_, live_.chosen + " on the server, read only", kQuiet, kPanel);
        heading.has_value()) {
        componentRows_.push_back(*heading);
    }
    for (const Value& kComponent : kComponents->items()) {
        const Value* kName = kComponent.find("name");
        if (kName == nullptr || kName->kind() != Value::Kind::String) {
            continue;
        }
        if (auto header = row(componentsColumn_, *kName->text(), kText, kHeaderFill); header.has_value()) {
            componentRows_.push_back(*header);
        }
        const Value* kFields = kComponent.find("fields");
        for (std::size_t each = 0; kFields != nullptr && each < kFields->names().size(); ++each) {
            const std::string kLine =
                "  " + kFields->names()[each] + "  " + document::writeCompact(kFields->items()[each]).substr(0, 96);
            if (auto field = row(componentsColumn_, kLine, kText, kPanel); field.has_value()) {
                componentRows_.push_back(*field);
            }
        }
    }
    ++live_.reads;
    live_.components = kComponents->items().size();
}

} // namespace rawframe::studio
