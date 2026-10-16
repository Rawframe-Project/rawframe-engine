#include "shell.h"

#include <cstdlib>

namespace rawframe::studio {

namespace {

/// The word a line begins with, or none for a comment or a blank line.
std::string wordOf(const std::string& line) {
    const std::size_t kStart = line.find_first_not_of(" \t");
    if (kStart == std::string::npos || line[kStart] == '#') {
        return {};
    }
    return line.substr(kStart, line.find_first_of(" \t", kStart) - kStart);
}

/// What follows the word.
std::string restOf(const std::string& line) {
    const std::string kWord = wordOf(line);
    const std::size_t kAfter = line.find(kWord) + kWord.size();
    const std::size_t kRest = line.find_first_not_of(" \t", kAfter);
    return kRest == std::string::npos ? std::string{} : line.substr(kRest);
}

} // namespace

std::string ShellParticipant::documentLabel(const std::string& path) const {
    const auto kAt = std::ranges::find(scenes_, path);
    const bool kSettings =
        kAt != scenes_.end() && listed_[static_cast<std::size_t>(kAt - scenes_.begin())] == Listed::Settings;
    return kSettings ? "Project settings" : path;
}

void ShellParticipant::showSettings() {
    const std::optional<Value> kRead = read(scene_, "lines.read");
    const Value* kLines = kRead.has_value() ? kRead->find("lines") : nullptr;
    settingsLines_.clear();
    settingsHeld_.clear();
    if (kLines == nullptr || kLines->kind() != Value::Kind::Array) {
        return;
    }
    for (const Value& kLine : kLines->items()) {
        settingsLines_.push_back(kLine.text() != nullptr ? *kLine.text() : std::string{});
    }
    // Each word the lines begin with a row, in the order it first comes,
    // holding the places of its lines; comments and blanks are no row.
    std::vector<std::string> words;
    for (std::size_t each = 0; each < settingsLines_.size(); ++each) {
        const std::string kWord = wordOf(settingsLines_[each]);
        if (kWord.empty()) {
            continue;
        }
        const auto kKnown = std::ranges::find(words, kWord);
        if (kKnown == words.end()) {
            words.push_back(kWord);
            settingsHeld_.push_back({each});
        } else {
            settingsHeld_[static_cast<std::size_t>(kKnown - words.begin())].push_back(each);
        }
    }
    for (std::size_t each = 0; each < words.size() && entityRows_.size() < kMostDataRows; ++each) {
        const std::size_t kCount = settingsHeld_[each].size();
        const std::string kLabel = kCount > 1 ? words[each] + " (" + std::to_string(kCount) + ")" : words[each];
        auto added = row(entitiesColumn_, kLabel, kText, kRow, true);
        if (!added.has_value()) {
            return;
        }
        entityRows_.push_back(*added);
        entities_.push_back(words[each]);
        names_.push_back(kLabel);
        brought_.push_back(false);
        removed_.push_back(false);
        places_.push_back(std::nullopt);
    }
}

void ShellParticipant::showSettingsGroup(std::size_t at) {
    endEdit();
    fields_.clear();
    entityAt_ = at;
    if (entity_ != entities_[at]) {
        static_cast<void>(tree_->scrollTo(componentsColumn_, 0, 0));
    }
    entity_ = entities_[at];
    for (std::size_t each = 0; each < entityRows_.size(); ++each) {
        static_cast<void>(
            tree_->setLook(entityRows_[each], ui::Look{.fill = each == at ? kChosen : kRow, .radius = 4}));
    }
    clear(componentRows_);
    actions_.clear();
    components_ = 0;
    if (at >= settingsHeld_.size()) {
        return;
    }
    auto heading = row(componentsColumn_, entity_ + " lines", kQuiet, kPanel);
    if (heading.has_value()) {
        componentRows_.push_back(*heading);
    }
    // Each line a field holding what follows its word, numbered where the
    // word begins several; an empty field takes the line away.
    const std::vector<std::size_t>& kHeld = settingsHeld_[at];
    for (std::size_t each = 0; each < kHeld.size(); ++each) {
        const std::string kLabel = kHeld.size() > 1 ? entity_ + " " + std::to_string(each + 1) : entity_;
        auto line = fieldRow(kLabel, restOf(settingsLines_[kHeld[each]]));
        if (!line.has_value()) {
            return;
        }
        componentRows_.push_back(line->first);
        fields_.push_back(FieldRow{.value = line->second,
                                   .role = FieldRow::Role::SettingLine,
                                   .field = std::to_string(kHeld[each]),
                                   .kind = entity_});
    }
    // Another line of the word, put in after its last.
    auto another = fieldRow("another " + entity_, "");
    if (another.has_value()) {
        componentRows_.push_back(another->first);
        fields_.push_back(FieldRow{.value = another->second,
                                   .role = FieldRow::Role::NewSetting,
                                   .field = std::to_string(kHeld.back() + 1),
                                   .kind = entity_});
    }
    ++components_;
}

void ShellParticipant::setSettingLine(const FieldRow& row, const std::string& text) {
    Value operation = Value::object();
    operation.add("operation", Value::string("lines.set"));
    operation.add("line", Value::integer(std::strtoll(row.field.c_str(), nullptr, 10)));
    operation.add("text", text.empty() ? Value{} : Value::string(row.kind + " " + text));
    commit(std::move(operation), row.kind + (text.empty() ? " line removed" : " set to " + text), entity_);
}

void ShellParticipant::putSettingLine(const FieldRow& row, const std::string& text) {
    if (text.empty()) {
        return;
    }
    Value operation = Value::object();
    operation.add("operation", Value::string("lines.insert"));
    operation.add("line", Value::integer(std::strtoll(row.field.c_str(), nullptr, 10)));
    operation.add("text", Value::string(row.kind + " " + text));
    commit(std::move(operation), row.kind + " " + text + " added", entity_);
}

} // namespace rawframe::studio
