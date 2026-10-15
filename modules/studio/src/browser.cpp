// Studio's asset browser (D799): the game's sources narrowed to a folder
// and a kind, each with its cook state, from `authoring.assets`.

#include "browser.h"

#include "rawframe/document/json.h"
#include "records.h"
#include "shell.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <set>
#include <span>

namespace rawframe::studio {

namespace {

using document::Value;

/// A setting's value as a field shows it: a text as it is, anything else
/// as JSON.
std::string shownText(const Value& value) {
    return value.text() != nullptr ? *value.text() : document::writeCompact(value);
}

/// What `cook` says shown beside a source.
std::string_view cookWords(std::string_view cook) {
    if (cook == "cooked") {
        return "cooked";
    }
    if (cook == "failed") {
        return "failed to cook";
    }
    if (cook == "cooking") {
        return "cooking";
    }
    return "not cooked";
}

} // namespace

std::vector<ListedSource> sourcesOf(std::string_view reply) {
    std::vector<ListedSource> made;
    const auto kParsed = document::parse(reply);
    const Value* kAnswer = kParsed.has_value() ? kParsed->find("answer") : nullptr;
    const Value* kSources = kAnswer != nullptr ? kAnswer->find("sources") : nullptr;
    if (kSources == nullptr || kSources->kind() != Value::Kind::Array) {
        return made;
    }
    const auto kTextOf = [](const Value& each, std::string_view name) -> const std::string* {
        const Value* kFound = each.find(name);
        return kFound != nullptr && kFound->kind() == Value::Kind::String ? kFound->text() : nullptr;
    };
    for (const Value& kEach : kSources->items()) {
        const std::string* kPath = kTextOf(kEach, "path");
        const std::string* kFolder = kTextOf(kEach, "folder");
        const std::string* kImporter = kTextOf(kEach, "importer");
        const std::string* kCook = kTextOf(kEach, "cook");
        if (kPath == nullptr || kFolder == nullptr || kImporter == nullptr || kCook == nullptr) {
            continue;
        }
        const std::string* kProblem = kTextOf(kEach, "problem");
        made.push_back(ListedSource{.path = *kPath,
                                    .folder = *kFolder,
                                    .importer = *kImporter,
                                    .cook = *kCook,
                                    .problem = kProblem != nullptr ? *kProblem : std::string{}});
    }
    return made;
}

std::string kindOf(std::string_view importer) {
    constexpr std::string_view kEngine = "rawframe.";
    return std::string{importer.starts_with(kEngine) ? importer.substr(kEngine.size()) : importer};
}

std::string kindLabel(std::string_view kind) {
    constexpr std::array<std::pair<std::string_view, std::string_view>, 13> kLabels = {{
        {"animation", "Animations"},
        {"audio", "Sounds"},
        {"canvasmaterial", "Canvas materials"},
        {"font", "Fonts"},
        {"game", "Games"},
        {"kest", "Scripts"},
        {"material", "Materials"},
        {"mesh", "Meshes"},
        {"mod", "Mods"},
        {"postprocess", "Post processes"},
        {"scene", "Scenes"},
        {"text", "Texts"},
        {"texture", "Textures"},
    }};
    const auto kFound = std::ranges::find(kLabels, kind, &std::pair<std::string_view, std::string_view>::first);
    return std::string{kFound != kLabels.end() ? kFound->second : kind};
}

const ListedSource* AssetBrowser::find(std::string_view path) const {
    const auto kFound = std::ranges::find(sources, path, &ListedSource::path);
    return kFound != sources.end() ? &*kFound : nullptr;
}

bool AssetBrowser::shows(std::string_view path) const {
    const ListedSource* kSource = find(path);
    if (kSource == nullptr) {
        // A document no sidecar names yet (a scene just made) shows only
        // while nothing narrows the list.
        return !folder.has_value() && !kind.has_value();
    }
    return (!folder.has_value() || kSource->folder == *folder) &&
           (!kind.has_value() || kindOf(kSource->importer) == *kind);
}

void ShellParticipant::listSources() {
    browser_.sources = sourcesOf(ask(assetsRecord(next())));
}

result::Status ShellParticipant::showBrowser() {
    for (const ui::Node kBox : browser_.boxes) {
        static_cast<void>(tree_->remove(kBox));
    }
    browser_.boxes.clear();
    browser_.kindRows.clear();
    browser_.folderRows.clear();
    // The kinds and folders the sources have, each a control that narrows
    // the list to it, the one chosen lit.
    std::set<std::string> kinds;
    std::set<std::string> folders;
    for (const ListedSource& kSource : browser_.sources) {
        kinds.insert(kindOf(kSource.importer));
        if (!kSource.folder.empty()) {
            folders.insert(kSource.folder);
        }
    }
    const auto kControls = [this](const std::set<std::string>& values,
                                  const std::optional<std::string>& chosen,
                                  std::string_view every,
                                  bool kinded,
                                  std::vector<std::pair<ui::Node, std::string>>& rows) -> result::Status {
        RAWFRAME_TRY_ASSIGN(const ui::Node kBox,
                            box(scenesColumn_, ui::Layout{.height = ui::pixels(28), .gap = 4, .shrink = 0}, 0));
        browser_.boxes.push_back(kBox);
        RAWFRAME_TRY_ASSIGN(const ui::Node kEvery, button(kBox, every));
        static_cast<void>(tree_->setLook(kEvery, ui::Look{.fill = chosen.has_value() ? kRow : kChosen, .radius = 4}));
        rows.emplace_back(kEvery, std::string{});
        for (const std::string& kValue : values) {
            RAWFRAME_TRY_ASSIGN(const ui::Node kControl, button(kBox, kinded ? kindLabel(kValue) : kValue + "/"));
            static_cast<void>(
                tree_->setLook(kControl, ui::Look{.fill = chosen == kValue ? kChosen : kRow, .radius = 4}));
            rows.emplace_back(kControl, kValue);
        }
        return {};
    };
    RAWFRAME_TRY(kControls(kinds, browser_.kind, "All kinds", true, browser_.kindRows));
    if (!folders.empty()) {
        RAWFRAME_TRY(kControls(folders, browser_.folder, "All folders", false, browser_.folderRows));
    }
    return {};
}

result::Status ShellParticipant::showSources() {
    browser_.sourceRows.clear();
    // The sources that are no document Studio opens, each with its kind and
    // how the cooks left it.
    for (const ListedSource& kSource : browser_.sources) {
        if (std::ranges::contains(scenes_, kSource.path) || !browser_.shows(kSource.path)) {
            continue;
        }
        RAWFRAME_TRY_ASSIGN(const ui::Node kRowNode,
                            box(scenesColumn_,
                                ui::Layout{.height = ui::pixels(28), .gap = 12, .shrink = 0, .padding = {8, 4, 8, 4}},
                                kSource.path == browser_.chosen ? kChosen : kRow));
        RAWFRAME_TRY(pressWords(kRowNode, kSource.path, kText, 14));
        RAWFRAME_TRY_ASSIGN(const ui::Node kState, box(kRowNode, ui::Layout{.shrink = 0}, 0));
        RAWFRAME_TRY(words(kState,
                           kindOf(kSource.importer) + ", " + std::string{cookWords(kSource.cook)},
                           kSource.cook == "failed" ? kWarning : kQuiet,
                           13));
        browser_.boxes.push_back(kRowNode);
        browser_.sourceRows.emplace_back(kRowNode, kSource.path);
    }
    return {};
}

bool ShellParticipant::pressBrowser(ui::Node node) {
    // The chosen source cooked again, its settings as the sidecar now has
    // them (D800): the game's cook, which reuses every other source.
    if (node == browser_.reimport && browser_.reimport != ui::Node{}) {
        if (!cooking_.has_value()) {
            cookOrStop();
        }
        return true;
    }
    const auto kNarrow = [&](std::vector<std::pair<ui::Node, std::string>>& rows, std::optional<std::string>& into) {
        const auto kFound = std::ranges::find(rows, node, &std::pair<ui::Node, std::string>::first);
        if (kFound == rows.end()) {
            return false;
        }
        into = kFound->second.empty() ? std::nullopt : std::optional<std::string>{kFound->second};
        ++browser_.narrowed;
        return true;
    };
    if (kNarrow(browser_.kindRows, browser_.kind) || kNarrow(browser_.folderRows, browser_.folder)) {
        static_cast<void>(showScenes());
        say(std::string{browser_.kind.has_value() ? kindLabel(*browser_.kind) : "All kinds"} + " in " +
            (browser_.folder.has_value() ? *browser_.folder + "/" : std::string{"all folders"}));
        return true;
    }
    const auto kSource = std::ranges::find(browser_.sourceRows, node, &std::pair<ui::Node, std::string>::first);
    if (kSource == browser_.sourceRows.end()) {
        return false;
    }
    browser_.chosen = kSource->second;
    ++browser_.chosenCount;
    showImportSettings();
    for (const auto& [kRowNode, kPath] : browser_.sourceRows) {
        static_cast<void>(
            tree_->setLook(kRowNode, ui::Look{.fill = kPath == browser_.chosen ? kChosen : kRow, .radius = 4}));
    }
    if (const ListedSource* kListed = browser_.find(browser_.chosen)) {
        say(kListed->path + ": " + kindOf(kListed->importer) + ", " + std::string{cookWords(kListed->cook)} +
            (kListed->problem.empty() ? "" : ": " + kListed->problem));
    }
    return true;
}

void ShellParticipant::showImportSettings() {
    endEdit();
    clear(componentRows_);
    actions_.clear();
    fields_.clear();
    browser_.reimport = ui::Node{};
    const auto kParsed = document::parse(ask(importSettingsRecord(next(), browser_.chosen)));
    const Value* kAnswer = kParsed.has_value() ? kParsed->find("answer") : nullptr;
    const Value* kImporter = kAnswer != nullptr ? kAnswer->find("importer") : nullptr;
    if (kImporter == nullptr || kImporter->text() == nullptr) {
        return;
    }
    const auto kHeader = row(componentsColumn_, browser_.chosen + "  " + *kImporter->text(), kText, kPanel);
    if (kHeader.has_value()) {
        componentRows_.push_back(*kHeader);
    }
    // Each setting its importer offers, as the sidecar has it or at its
    // fallback, chosen from its choices where it has them.
    const Value* kSettings = kAnswer->find("settings");
    const Value* kOffereds = kAnswer->find("fields");
    for (const Value& kOffered : kOffereds != nullptr ? kOffereds->items() : std::span<const Value>{}) {
        const Value* kName = kOffered.find("name");
        const Value* kType = kOffered.find("type");
        if (kName == nullptr || kName->text() == nullptr || kType == nullptr || kType->text() == nullptr) {
            continue;
        }
        const Value* kKept = kSettings != nullptr && kSettings->kind() == Value::Kind::Object
                                 ? kSettings->find(*kName->text())
                                 : nullptr;
        const Value* kValue = kKept != nullptr ? kKept : kOffered.find("fallback");
        Catalog::Field type{.name = *kName->text(), .kind = *kType->text()};
        if (const Value* kChoices = kOffered.find("choices"); kChoices != nullptr) {
            for (const Value& kChoice : kChoices->items()) {
                if (kChoice.text() != nullptr) {
                    type.cases.push_back(*kChoice.text());
                }
            }
        }
        auto line = fieldRow(
            *kName->text(), kValue != nullptr ? shownText(*kValue) : std::string{}, std::nullopt, kKept != nullptr);
        if (!line.has_value()) {
            return;
        }
        componentRows_.push_back(line->first);
        fields_.push_back(FieldRow{.value = line->second,
                                   .role = FieldRow::Role::ImportSetting,
                                   .field = *kName->text(),
                                   .kind = *kType->text(),
                                   .type = std::move(type)});
    }
    if (auto made = button(componentsColumn_, "Reimport"); made.has_value()) {
        browser_.reimport = *made;
        componentRows_.push_back(*made);
    }
}

void ShellParticipant::setImportSetting(const FieldRow& row, const std::string& text) {
    // The text as its setting's kind; the importer says whether it takes it.
    Value value = Value::string(text);
    if (row.kind == "truth" && (text == "true" || text == "false")) {
        value = Value::boolean(text == "true");
    } else if (row.kind == "integer") {
        std::int64_t number = 0;
        if (std::from_chars(text.data(), text.data() + text.size(), number).ec != std::errc{}) {
            ++browser_.settingsRefused;
            say(row.field + " is a whole number");
            return;
        }
        value = Value::integer(number);
    }
    Value settings = Value::object();
    settings.add(row.field, std::move(value));
    const std::string kReply = ask(setImportSettingsRecord(next(), browser_.chosen, std::move(settings)));
    const Answered kAnswered = answeredOf(kReply);
    if (!kAnswered.done) {
        ++browser_.settingsRefused;
        say(kAnswered.message);
        return;
    }
    ++browser_.settingsSet;
    say(browser_.chosen + ": " + row.field + " set");
    showImportSettings();
}

} // namespace rawframe::studio
