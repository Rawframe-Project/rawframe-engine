#include "shell.h"

#include <array>
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

std::string sectionName(std::string_view word) {
    // A section of the game description by what it holds (D922).
    constexpr std::array<std::pair<std::string_view, std::string_view>, 24> kNames = {{
        {"program", "Program"},           {"physics3d", "Physics"},
        {"physics2d", "2D physics"},      {"component", "Components"},
        {"system", "Systems"},            {"scene", "Scenes"},
        {"field", "Field hints"},         {"replicate", "Replication"},
        {"player", "Players"},            {"input", "Input"},
        {"actions", "Actions"},           {"sample", "Input sampling"},
        {"presentation", "Presentation"}, {"present", "Present systems"},
        {"spawn", "Spawned at start"},    {"material", "Materials"},
        {"texture", "Textures"},          {"mesh", "Meshes"},
        {"sound", "Sounds"},              {"collision", "Collision"},
        {"tick", "Tick rate"},            {"entity", "Entity references"},
        {"mod", "Mods"},                  {"export", "Export targets"},
    }};
    const auto kFound = std::ranges::find(kNames, word, &std::pair<std::string_view, std::string_view>::first);
    if (kFound != kNames.end()) {
        return std::string{kFound->second};
    }
    std::string made{word};
    if (!made.empty() && made[0] >= 'a' && made[0] <= 'z') {
        made[0] = static_cast<char>(made[0] - 'a' + 'A');
    }
    return made;
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
    held_.settingsLines.clear();
    held_.settings.clear();
    if (kLines == nullptr || kLines->kind() != Value::Kind::Array) {
        return;
    }
    for (const Value& kLine : kLines->items()) {
        held_.settingsLines.push_back(kLine.text() != nullptr ? *kLine.text() : std::string{});
    }
    // Each word the lines begin with a row, in the order it first comes,
    // holding the places of its lines; comments and blanks are no row.
    std::vector<std::string> sections;
    for (std::size_t each = 0; each < held_.settingsLines.size(); ++each) {
        const std::string kWord = wordOf(held_.settingsLines[each]);
        if (kWord.empty()) {
            continue;
        }
        const auto kKnown = std::ranges::find(sections, kWord);
        if (kKnown == sections.end()) {
            sections.push_back(kWord);
            held_.settings.push_back({each});
        } else {
            held_.settings[static_cast<std::size_t>(kKnown - sections.begin())].push_back(each);
        }
    }
    for (std::size_t each = 0; each < sections.size() && entityRows_.size() < EditorsHeld::kMostRows; ++each) {
        const std::size_t kCount = held_.settings[each].size();
        const std::string kLabel = kCount > 1 ? sections[each] + " (" + std::to_string(kCount) + ")" : sections[each];
        // As the design's sections (D922): its plain name, its count at the
        // row's end; the row still named by its word, as before.
        auto added = box(entitiesColumn_,
                         ui::Layout{.height = ui::pixels(26),
                                    .alignItems = ui::Align::Center,
                                    .gap = 8,
                                    .shrink = 0,
                                    .padding = {10, 10, 0, 0}},
                         0);
        if (!added.has_value()) {
            return;
        }
        if (auto label = box(*added, ui::Layout{.grow = 1}, 0); label.has_value()) {
            static_cast<void>(words(*label, sectionName(sections[each]), look::kRowInk, look::kBodySize));
            static_cast<void>(tree_->setInteraction(*label, ui::Interaction{.hits = ui::Interaction::Hits::Nothing}));
        }
        if (kCount > 1) {
            if (auto count = box(*added, ui::Layout{.shrink = 0}, 0); count.has_value()) {
                static_cast<void>(monoWords(*count, std::to_string(kCount), look::kFaintInk, look::kSmallSize));
                static_cast<void>(
                    tree_->setInteraction(*count, ui::Interaction{.hits = ui::Interaction::Hits::Nothing}));
            }
        }
        static_cast<void>(tree_->setRole(*added, ui::Role::Button));
        static_cast<void>(tree_->setName(*added, kLabel));
        entityRows_.push_back(*added);
        entities_.push_back(sections[each]);
        names_.push_back(kLabel);
        brought_.push_back(false);
        removed_.push_back(false);
        places_.push_back(std::nullopt);
    }
    showSettingsPage(sections);
}

void ShellParticipant::showSettingsPage(const std::vector<std::string>& sections) {
    // The center: every section's lines as the game description has them,
    // under its plain name, each section pressed to edit in the Inspector
    // (D922).
    held_.settingsHeadings.clear();
    auto page = box(centerColumn_,
                    ui::Layout{.height = ui::pixels(0),
                               .minHeight = ui::pixels(0),
                               .direction = ui::Direction::Column,
                               .gap = 2,
                               .grow = 1,
                               .padding = {40, 40, 28, 28},
                               .scroll = ui::Scroll::Vertical},
                    0);
    if (!page.has_value()) {
        return;
    }
    viewRows_.push_back(*page);
    static_cast<void>(tree_->setRole(*page, ui::Role::ScrollView));
    static_cast<void>(tree_->setName(*page, "Settings"));
    if (auto title = box(*page, ui::Layout{.shrink = 0}, 0); title.has_value()) {
        static_cast<void>(strongWords(*title, description_.filename().string() + " \u00B7 settings", look::kInk, 18));
    }
    if (auto what = box(*page, ui::Layout{.shrink = 0, .margin = {0, 0, 4, 14}}, 0); what.has_value()) {
        static_cast<void>(words(*what,
                                "The game description's lines. A section opens in the Inspector, a field a line; "
                                "the same lines an agent writes.",
                                look::kLabelInk,
                                look::kLabelSize));
    }
    for (std::size_t each = 0; each < sections.size() && each < held_.settings.size(); ++each) {
        auto heading = box(*page,
                           ui::Layout{.alignItems = ui::Align::Center,
                                      .gap = 8,
                                      .shrink = 0,
                                      .padding = {0, 0, 14, 6},
                                      .border = {0, 0, 0, 1}},
                           0);
        if (!heading.has_value()) {
            return;
        }
        static_cast<void>(tree_->setLook(*heading, ui::Look{.borderColor = look::kLine}));
        if (auto title = box(*heading, ui::Layout{.shrink = 0}, 0); title.has_value()) {
            static_cast<void>(strongWords(*title, sectionName(sections[each]), look::kInk, 14));
            static_cast<void>(tree_->setInteraction(*title, ui::Interaction{.hits = ui::Interaction::Hits::Nothing}));
        }
        if (auto edit = box(*heading, ui::Layout{.shrink = 0}, 0); edit.has_value()) {
            static_cast<void>(words(*edit, "Edit", look::kAccent, look::kLabelSize));
            static_cast<void>(tree_->setInteraction(*edit, ui::Interaction{.hits = ui::Interaction::Hits::Nothing}));
        }
        static_cast<void>(tree_->setRole(*heading, ui::Role::Button));
        static_cast<void>(tree_->setName(*heading, sectionName(sections[each])));
        held_.settingsHeadings.emplace_back(*heading, each);
        for (const std::size_t kLine : held_.settings[each]) {
            if (auto text = box(*page, ui::Layout{.shrink = 0, .padding = {2, 2, 3, 2}}, 0); text.has_value()) {
                static_cast<void>(monoWords(*text, held_.settingsLines[kLine], look::kSoftInk, look::kLabelSize));
            }
        }
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
    if (at >= held_.settings.size()) {
        return;
    }
    auto heading = row(componentsColumn_, entity_ + " lines", kQuiet, kPanel);
    if (heading.has_value()) {
        componentRows_.push_back(*heading);
    }
    // Each line a field holding what follows its word, numbered where the
    // word begins several; an empty field takes the line away.
    const std::vector<std::size_t>& kHeld = held_.settings[at];
    for (std::size_t each = 0; each < kHeld.size(); ++each) {
        const std::string kLabel = kHeld.size() > 1 ? entity_ + " " + std::to_string(each + 1) : entity_;
        auto line = fieldRow(kLabel, restOf(held_.settingsLines[kHeld[each]]));
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
