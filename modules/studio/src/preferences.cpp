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

bool isChord(std::string_view text) noexcept {
    if (text == "Delete") {
        return true;
    }
    bool control = false;
    for (const std::string_view kHeld : {"Ctrl+", "Shift+", "Alt+"}) {
        if (text.starts_with(kHeld)) {
            control = control || kHeld == "Ctrl+";
            text.remove_prefix(kHeld.size());
        }
    }
    if (text.size() == 1) {
        // A letter with Ctrl but A, which stays a field's select all.
        return control && ((text[0] > 'A' && text[0] <= 'Z') || (text[0] >= '0' && text[0] <= '9'));
    }
    if (text.size() < 2 || text.size() > 3 || text[0] != 'F' || text[1] == '0') {
        return false;
    }
    int number = 0;
    for (const char kDigit : text.substr(1)) {
        if (kDigit < '0' || kDigit > '9') {
            return false;
        }
        number = number * 10 + (kDigit - '0');
    }
    return number >= 1 && number <= 12 && number != 11;
}

std::optional<std::vector<std::string>> chordsOf(std::string_view text) {
    std::vector<std::string> chords;
    std::size_t start = 0;
    while (start < text.size()) {
        const std::size_t kEnd = std::min(text.find(',', start), text.size());
        std::string_view chord = text.substr(start, kEnd - start);
        while (!chord.empty() && chord.front() == ' ') {
            chord.remove_prefix(1);
        }
        while (!chord.empty() && chord.back() == ' ') {
            chord.remove_suffix(1);
        }
        if (!chord.empty()) {
            if (!isChord(chord)) {
                return std::nullopt;
            }
            if (std::ranges::find(chords, chord) == chords.end()) {
                chords.emplace_back(chord);
            }
        }
        start = kEnd + 1;
    }
    return chords;
}

std::string chordsText(const std::vector<std::string>& chords) {
    std::string text;
    for (const std::string& chord : chords) {
        text += (text.empty() ? "" : ", ") + chord;
    }
    return text;
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
    if (const Value* keys = kParsed->find("keys"); keys != nullptr) {
        if (keys->kind() != Value::Kind::Object) {
            return std::nullopt;
        }
        for (std::size_t at = 0; at < keys->names().size(); ++at) {
            const Value& kChords = keys->items()[at];
            if (kChords.kind() != Value::Kind::Array) {
                return std::nullopt;
            }
            std::vector<std::string> chords;
            for (const Value& each : kChords.items()) {
                if (each.kind() != Value::Kind::String || !isChord(*each.text())) {
                    return std::nullopt;
                }
                chords.push_back(*each.text());
            }
            preferences.keys.emplace(keys->names()[at], std::move(chords));
        }
    }
    if (const Value* editor = kParsed->find("editor"); editor != nullptr) {
        if (editor->kind() != Value::Kind::String) {
            return std::nullopt;
        }
        preferences.editor = *editor->text();
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
    if (!preferences.keys.empty()) {
        Value keys = Value::object();
        for (const auto& [kCommand, kChords] : preferences.keys) {
            Value chords = Value::array();
            for (const std::string& chord : kChords) {
                chords.push(Value::string(chord));
            }
            keys.add(kCommand, std::move(chords));
        }
        written.add("keys", std::move(keys));
    }
    if (!preferences.editor.empty()) {
        written.add("editor", Value::string(preferences.editor));
    }
    return document::write(written);
}

} // namespace rawframe::studio
