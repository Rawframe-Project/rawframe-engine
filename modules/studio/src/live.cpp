#include "hints.h"
#include "shell.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <span>

namespace rawframe::studio {

namespace {

/// The most entities the Hierarchy lists live: a page the server gives.
constexpr std::int64_t kMostListed = 256;

/// A component's name after its module's: `starter.daylight` is
/// `daylight`.
std::string shortName(std::string_view name) {
    const std::size_t kDot = name.find_last_of('.');
    return std::string{kDot == std::string_view::npos ? name : name.substr(kDot + 1)};
}

/// The icon an entity is listed with, by what it holds, and its ink.
std::pair<Icon, std::uint32_t> liveIcon(const std::vector<std::string>& names) {
    const auto kHolds = [&names](std::string_view part) {
        return std::ranges::any_of(names, [part](const std::string& name) {
            return shortName(name).find(part) != std::string::npos;
        });
    };
    if (kHolds("sun") || kHolds("daylight")) {
        return {Icon::Sun, look::kWarn};
    }
    if (kHolds("sky")) {
        return {Icon::Sky, look::kAccent};
    }
    if (kHolds("lamp") || kHolds("light")) {
        return {Icon::Bulb, look::kWarn};
    }
    if (kHolds("camera")) {
        return {Icon::Camera, look::kLabelInk};
    }
    if (kHolds("player") || kHolds("stick") || kHolds("mover")) {
        return {Icon::Person, look::kEntityInk};
    }
    return {Icon::Cube, look::kEntityInk};
}

/// A field's value as the Inspector shows it: a number rounded to what
/// reads, a color by its hex.
std::string liveValue(std::string_view field, const Value& value) {
    const std::optional<double> kNumber = value.kind() == Value::Kind::Number ? value.real() : std::nullopt;
    if (!kNumber.has_value()) {
        return document::writeCompact(value);
    }
    if (field == "color" && *kNumber >= 0 && *kNumber <= 4294967295.0 && *kNumber == std::floor(*kNumber)) {
        std::array<char, 16> text{};
        std::snprintf(text.data(), text.size(), "#%08llX", static_cast<unsigned long long>(*kNumber));
        return text.data();
    }
    std::array<char, 32> text{};
    if (*kNumber == std::floor(*kNumber) && std::fabs(*kNumber) < 1e15) {
        std::snprintf(text.data(), text.size(), "%.0f", *kNumber);
        return text.data();
    }
    std::snprintf(text.data(), text.size(), std::fabs(*kNumber) >= 1e5 ? "%.4g" : "%.4f", *kNumber);
    std::string made{text.data()};
    if (made.find('.') != std::string::npos && made.find('e') == std::string::npos) {
        while (made.ends_with('0')) {
            made.pop_back();
        }
        if (made.ends_with('.')) {
            made.pop_back();
        }
    }
    return made;
}

/// A field's value as the edited Inspector shows it, where the catalog
/// types it (D995): a labelled number or a case by its name, a color by
/// its hex, an asset held by identity by the asset; else as `liveValue`.
std::string
shownLive(const Catalog::Component* type, std::string_view field, const Value& value, std::span<const Asset> assets) {
    std::string text = liveValue(field, value);
    const auto kField = type != nullptr ? std::ranges::find(type->fields, field, &Catalog::Field::name)
                                        : std::vector<Catalog::Field>::const_iterator{};
    if (type == nullptr || kField == type->fields.end()) {
        return text;
    }
    if (kField->color || !kField->labels.empty()) {
        return shownAs(*kField, text);
    }
    if (!kField->asset.empty()) {
        if (const Asset* held = assetHeld(assets, text); held != nullptr) {
            return assetShown(*held);
        }
    }
    return text;
}

} // namespace

void ShellParticipant::switchLive() {
    if (!live_.on && (!playing_.has_value() || previewedName().empty())) {
        say("play the game to inspect its World live");
        return;
    }
    live_.on = !live_.on;
    live_.chosen.clear();
    static_cast<void>(tree_->setLook(live_.button, ui::Look{.fill = live_.on ? kChosen : kRow, .radius = 4}));
    // The chosen scene shown again, its Hierarchy and Inspector the live
    // World's or its own; with none, the World's alone (D928).
    if (scene_.empty()) {
        clear(entityRows_);
        clear(componentRows_);
        if (live_.on) {
            showLive();
        }
        return;
    }
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
    asked.add("scene", Value::string(previewedName()));
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
        // As the scene's rows are (D923): its kind's icon, what it holds
        // by the components' short names, and its id on the server.
        std::vector<std::string> names;
        for (const Value& kComponent : kComponents != nullptr ? kComponents->items() : std::span<const Value>{}) {
            if (kComponent.kind() == Value::Kind::String) {
                names.push_back(*kComponent.text());
            }
        }
        std::string line;
        for (std::size_t each = 0; each < names.size() && each < 2; ++each) {
            line += (each == 0 ? "" : " \u00B7 ") + prettyField(shortName(names[each]));
        }
        if (names.size() > 2) {
            line += " +" + std::to_string(names.size() - 2);
        }
        line += "  " + *kEntity->text();
        const auto [kIcon, kInk] = liveIcon(names);
        auto made = entityRow(entitiesColumn_, line, 0, false, kIcon, kInk, look::kRowInk);
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
        static_cast<void>(tree_->setLook(kRowNode, ui::Look{.fill = kName == entity ? kChosen : 0U, .radius = 4}));
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
    asked.add("scene", Value::string(previewedName()));
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
    // As the Inspector shows an entity (D923), read only: its id and that
    // it is the server's, then each component's fields, numbers rounded.
    if (auto heading =
            box(componentsColumn_,
                ui::Layout{.direction = ui::Direction::Column, .gap = 3, .shrink = 0, .padding = {12, 12, 10, 8}},
                0);
        heading.has_value()) {
        componentRows_.push_back(*heading);
        if (auto name = box(*heading, ui::Layout{.shrink = 0}, 0); name.has_value()) {
            static_cast<void>(strongWords(*name, live_.chosen + " on the server, read only", look::kInk, 14));
        }
        if (auto quiet = box(*heading, ui::Layout{.shrink = 0}, 0); quiet.has_value()) {
            static_cast<void>(words(
                *quiet, "The World as the game plays it, read twice a second", look::kFaintInk, look::kLabelSize));
        }
    }
    std::uint64_t byName = 0;
    for (const Value& kComponent : kComponents->items()) {
        const Value* kName = kComponent.find("name");
        if (kName == nullptr || kName->kind() != Value::Kind::String) {
            continue;
        }
        auto header = box(componentsColumn_,
                          ui::Layout{.alignItems = ui::Align::Center,
                                     .gap = 8,
                                     .shrink = 0,
                                     .padding = {10, 10, 7, 6},
                                     .border = {0, 0, 1, 0}},
                          0);
        if (!header.has_value()) {
            continue;
        }
        componentRows_.push_back(*header);
        static_cast<void>(tree_->setLook(*header, ui::Look{.borderColor = look::kLine}));
        if (auto titled = box(*header, ui::Layout{.shrink = 0}, 0); titled.has_value()) {
            static_cast<void>(strongWords(*titled, prettyField(shortName(*kName->text())), look::kInk));
        }
        if (auto quiet = box(*header, ui::Layout{.shrink = 1}, 0); quiet.has_value()) {
            static_cast<void>(monoWords(*quiet, *kName->text(), look::kFaintInk, look::kSmallSize));
        }
        const Value* kFields = kComponent.find("fields");
        // Its type as the catalog tells it, so a value reads as the edited
        // Inspector shows it (D995).
        const auto kType = std::ranges::find_if(catalog_.components, [&](const Catalog::Component& each) {
            return each.name == *kName->text() || each.id == *kName->text();
        });
        for (std::size_t each = 0; kFields != nullptr && each < kFields->names().size(); ++each) {
            auto line =
                box(componentsColumn_,
                    ui::Layout{.alignItems = ui::Align::Center, .gap = 8, .shrink = 0, .padding = {14, 10, 3, 3}},
                    0);
            if (!line.has_value()) {
                break;
            }
            componentRows_.push_back(*line);
            if (auto label = box(*line, ui::Layout{.width = ui::pixels(110), .shrink = 0}, 0); label.has_value()) {
                static_cast<void>(
                    words(*label, prettyField(kFields->names()[each]), look::kLabelInk, look::kLabelSize));
            }
            if (auto value =
                    box(*line,
                        ui::Layout{.width = ui::pixels(0), .grow = 1, .padding = {8, 8, 4, 4}, .border = {1, 1, 1, 1}},
                        0);
                value.has_value()) {
                static_cast<void>(
                    tree_->setLook(*value, ui::Look{.fill = look::kWellFill, .borderColor = look::kLine, .radius = 4}));
                const std::string kRead = liveValue(kFields->names()[each], kFields->items()[each]);
                const std::string kAsShown = shownLive(kType != catalog_.components.end() ? &*kType : nullptr,
                                                       kFields->names()[each],
                                                       kFields->items()[each],
                                                       assets_);
                byName += kAsShown != kRead ? 1 : 0;
                static_cast<void>(monoWords(*value, kAsShown.substr(0, 96), look::kSoftInk, look::kSmallSize));
            }
        }
    }
    ++live_.reads;
    live_.components = kComponents->items().size();
    live_.named = byName;
}

} // namespace rawframe::studio
