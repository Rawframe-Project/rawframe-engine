#include "preferences.h"

#include "rawframe/document/json.h"

#include <algorithm>
#include <cmath>

namespace rawframe::studio {

namespace {

using document::Value;

constexpr std::string_view kKind = "rawframe.studio.preferences";

/// The bounds a size is held in: a dock never narrower than its words need,
/// nor so wide the center is lost.
constexpr float kNarrowest = 200;
constexpr float kWidest = 800;
constexpr float kLowestDock = 120;
constexpr float kTallestDock = 1200;

} // namespace

Preferences opened(Preferences preferences, const std::string& description) {
    std::erase(preferences.recent, description);
    preferences.recent.insert(preferences.recent.begin(), description);
    if (preferences.recent.size() > kRecentGames) {
        preferences.recent.resize(kRecentGames);
    }
    return preferences;
}

Preferences held(Preferences preferences) {
    preferences.hierarchyWidth = std::clamp(preferences.hierarchyWidth, kNarrowest, kWidest);
    preferences.inspectorWidth = std::clamp(preferences.inspectorWidth, kNarrowest, kWidest);
    if (preferences.dockHeight != 0) {
        preferences.dockHeight = std::clamp(preferences.dockHeight, kLowestDock, kTallestDock);
    }
    return preferences;
}

std::optional<Preferences> preferencesOf(std::string_view document) {
    // A small document: a few sizes, never megabytes.
    const auto kParsed = document::parse(document, document::ReadLimits{.maximumBytes = 64 * 1024, .maximumDepth = 4});
    if (!kParsed.has_value() || kParsed->kind() != Value::Kind::Object) {
        return std::nullopt;
    }
    const Value* kind = kParsed->find("kind");
    const Value* version = kParsed->find("formatVersion");
    if (kind == nullptr || kind->kind() != Value::Kind::String || *kind->text() != kKind || version == nullptr ||
        version->integer() != 1) {
        return std::nullopt;
    }
    Preferences preferences;
    for (const auto& [kName, kSize] : {std::pair{"hierarchyWidth", &preferences.hierarchyWidth},
                                       std::pair{"inspectorWidth", &preferences.inspectorWidth},
                                       std::pair{"dockHeight", &preferences.dockHeight}}) {
        const Value* given = kParsed->find(kName);
        if (given == nullptr) {
            continue;
        }
        const std::optional<double> kNumber = given->kind() == Value::Kind::Number ? given->real() : std::nullopt;
        if (!kNumber.has_value() || !std::isfinite(*kNumber)) {
            return std::nullopt;
        }
        *kSize = static_cast<float>(*kNumber);
    }
    if (const Value* recent = kParsed->find("recent"); recent != nullptr) {
        if (recent->kind() != Value::Kind::Array) {
            return std::nullopt;
        }
        for (const Value& each : recent->items()) {
            if (each.kind() != Value::Kind::String || each.text()->empty()) {
                return std::nullopt;
            }
            if (preferences.recent.size() < kRecentGames) {
                preferences.recent.push_back(*each.text());
            }
        }
    }
    return held(preferences);
}

std::string writtenPreferences(const Preferences& preferences) {
    const auto kSize = [](float size) {
        return Value::integer(std::lround(size));
    };
    Value written = Value::object();
    written.add("kind", Value::string(std::string{kKind}));
    written.add("formatVersion", Value::integer(1));
    written.add("hierarchyWidth", kSize(preferences.hierarchyWidth));
    written.add("inspectorWidth", kSize(preferences.inspectorWidth));
    written.add("dockHeight", kSize(preferences.dockHeight));
    Value recent = Value::array();
    for (const std::string& each : preferences.recent) {
        recent.push(Value::string(each));
    }
    written.add("recent", std::move(recent));
    return document::write(written);
}

} // namespace rawframe::studio
