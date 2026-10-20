// Studio's asset browser (D799): the game's sources narrowed to a folder
// and a kind, each with its cook state, from `authoring.assets`.

#include "browser.h"

#include "hints.h"
#include "rawframe/authoring_session/game.h"
#include "rawframe/document/json.h"
#include "records.h"
#include "shell.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <filesystem>
#include <map>
#include <set>
#include <span>
#include <tuple>

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
                                    .problem = kProblem != nullptr ? *kProblem : std::string{},
                                    .id = kTextOf(kEach, "id") != nullptr ? *kTextOf(kEach, "id") : std::string{}});
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

std::uint64_t thumbnailKey(std::string_view path) noexcept {
    // FNV-1a over the path, its top byte Studio's thumbnails' own.
    std::uint64_t hash = 0xCBF29CE484222325ULL;
    for (const char kByte : path) {
        hash = (hash ^ static_cast<unsigned char>(kByte)) * 0x100000001B3ULL;
    }
    return (hash & 0x00FFFFFFFFFFFFFFULL) | 0x5400000000000000ULL;
}

bool named(std::string_view path, std::string_view search) {
    if (search.empty()) {
        return true;
    }
    const auto kLower = [](char each) {
        return each >= 'A' && each <= 'Z' ? static_cast<char>(each - 'A' + 'a') : each;
    };
    const std::string_view kName = path.substr(path.find_last_of('/') + 1);
    return std::ranges::search(kName, search, {}, kLower, kLower).begin() != kName.end();
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
        return !folder.has_value() && !kind.has_value() && named(path, search);
    }
    return (!folder.has_value() || kSource->folder == *folder) &&
           (!kind.has_value() || kindOf(kSource->importer) == *kind) && named(path, search);
}

void ShellParticipant::listSources() {
    browser_.sources = sourcesOf(ask(assetsRecord(next())));
}

result::Status ShellParticipant::makeAssets() {
    // As the design's Assets (D916): a heading with the folder shown, the
    // kinds, the search, the views, and Import; then the folders' column
    // beside the grid of cards.
    RAWFRAME_TRY_ASSIGN(scenesColumn_,
                        box(std::nullopt,
                            ui::Layout{.minHeight = ui::pixels(0), .direction = ui::Direction::Column, .grow = 1},
                            look::kPanelFill));
    RAWFRAME_TRY(tree_->setName(scenesColumn_, "Assets"));
    RAWFRAME_TRY_ASSIGN(const ui::Node kHeading,
                        box(scenesColumn_,
                            ui::Layout{.height = ui::pixels(42),
                                       .alignItems = ui::Align::Center,
                                       .gap = 6,
                                       .shrink = 0,
                                       .padding = {12, 12, 0, 0},
                                       .border = {0, 0, 0, 1}},
                            0));
    RAWFRAME_TRY(tree_->setLook(kHeading, ui::Look{.borderColor = look::kLine}));
    RAWFRAME_TRY_ASSIGN(browser_.crumbs,
                        box(kHeading, ui::Layout{.alignItems = ui::Align::Center, .gap = 6, .shrink = 0}, 0));
    RAWFRAME_TRY(box(kHeading, ui::Layout{.grow = 1}, 0));
    RAWFRAME_TRY_ASSIGN(const auto kSearch, searchWell(kHeading, "Search assets", "Search", "", 180, Icon::Search));
    browser_.searchValue = kSearch.second;
    RAWFRAME_TRY_ASSIGN(browser_.tools,
                        box(kHeading, ui::Layout{.alignItems = ui::Align::Center, .gap = 6, .shrink = 0}, 0));
    // The kinds on a row of their own under it, the whole width theirs,
    // scrolled sideways where they are still more (D925).
    RAWFRAME_TRY_ASSIGN(browser_.chips,
                        box(scenesColumn_,
                            ui::Layout{.height = ui::pixels(34),
                                       .alignItems = ui::Align::Center,
                                       .gap = 4,
                                       .shrink = 0,
                                       .padding = {12, 12, 0, 0},
                                       .border = {0, 0, 0, 1},
                                       .scroll = ui::Scroll::Horizontal},
                            0));
    RAWFRAME_TRY(tree_->setLook(browser_.chips, ui::Look{.borderColor = look::kLine, .clip = true}));
    RAWFRAME_TRY(tree_->setRole(browser_.chips, ui::Role::ScrollView));
    RAWFRAME_TRY(tree_->setName(browser_.chips, "Asset kinds"));
    RAWFRAME_TRY_ASSIGN(
        const ui::Node kBody,
        box(scenesColumn_,
            ui::Layout{.height = ui::pixels(0), .minHeight = ui::pixels(0), .direction = ui::Direction::Row, .grow = 1},
            0));
    RAWFRAME_TRY_ASSIGN(browser_.folders,
                        box(kBody,
                            ui::Layout{.width = ui::pixels(200),
                                       .minHeight = ui::pixels(0),
                                       .direction = ui::Direction::Column,
                                       .gap = 1,
                                       .shrink = 0,
                                       .padding = {6, 6, 8, 10},
                                       .border = {0, 1, 0, 0}},
                            0));
    RAWFRAME_TRY(tree_->setLook(browser_.folders, ui::Look{.borderColor = look::kLine}));
    RAWFRAME_TRY(tree_->setName(browser_.folders, "Folders"));
    RAWFRAME_TRY_ASSIGN(browser_.grid,
                        box(kBody,
                            ui::Layout{.width = ui::pixels(0),
                                       .minHeight = ui::pixels(0),
                                       .direction = ui::Direction::Row,
                                       .alignItems = ui::Align::Start,
                                       .gap = 12,
                                       .grow = 1,
                                       .padding = {16, 16, 14, 14},
                                       .scroll = ui::Scroll::Vertical,
                                       .wrap = true},
                            0));
    RAWFRAME_TRY(tree_->setRole(browser_.grid, ui::Role::ScrollView));
    RAWFRAME_TRY(tree_->setName(browser_.grid, "Asset grid"));
    return {};
}

result::Result<std::pair<ui::Node, ui::Node>> ShellParticipant::searchWell(
    ui::Node parent, std::string_view name, std::string_view hint, std::string_view value, float width, Icon which) {
    // The design's search (D916): a well with the magnifier, its words the
    // hint until something is typed.
    RAWFRAME_TRY_ASSIGN(const ui::Node kWell,
                        box(parent,
                            ui::Layout{.width = width > 0 ? ui::pixels(width) : ui::Dimension{},
                                       .height = ui::pixels(26),
                                       .alignItems = ui::Align::Center,
                                       .gap = 6,
                                       .grow = width > 0 ? 0.0F : 1.0F,
                                       .shrink = 0,
                                       .padding = {8, 4, 0, 0},
                                       .border = {1, 1, 1, 1}},
                            0));
    RAWFRAME_TRY(tree_->setLook(kWell, ui::Look{.fill = look::kWellFill, .borderColor = look::kEdge, .radius = 5}));
    RAWFRAME_TRY(icon(kWell, which, look::kFaintInk, 13));
    RAWFRAME_TRY_ASSIGN(const ui::Node kValue, tree_->addEditable(++keys_));
    RAWFRAME_TRY(tree_->setLayout(kValue, ui::Layout{.width = ui::pixels(0), .grow = 1, .padding = {2, 2, 4, 3}}));
    RAWFRAME_TRY(tree_->attach(kWell, kValue));
    RAWFRAME_TRY(words(kValue, value.empty() ? hint : value, value.empty() ? look::kFaintInk : kText, look::kBodySize));
    RAWFRAME_TRY(tree_->setRole(kValue, ui::Role::TextInput));
    RAWFRAME_TRY(tree_->setName(kValue, name));
    return std::pair{kWell, kValue};
}

namespace {

/// The icon a source of `kind` is drawn with, and its ink (D916).
std::pair<Icon, std::uint32_t> kindIcon(std::string_view kind) {
    constexpr std::array<std::tuple<std::string_view, Icon, std::uint32_t>, 13> kIcons = {{
        {"animation", Icon::Animation, 0xB48CFFFFU},
        {"audio", Icon::Sound, 0xF2A93BFFU},
        {"canvasmaterial", Icon::Material, 0xE0A458FFU},
        {"font", Icon::Font, 0xC3C5CCFFU},
        {"game", Icon::Gear, 0xA3A6AEFFU},
        {"kest", Icon::Script, 0x7FD18BFFU},
        {"material", Icon::Material, 0xE0A458FFU},
        {"mesh", Icon::Mesh, 0xC3C5CCFFU},
        {"mod", Icon::Puzzle, 0xB48CFFFFU},
        {"postprocess", Icon::Camera, 0x8FB8FFFFU},
        {"scene", Icon::Cube, 0x7AA9FFFFU},
        {"text", Icon::File, 0xA3A6AEFFU},
        {"texture", Icon::Texture, 0x8FB8FFFFU},
    }};
    for (const auto& [kKind, kIcon, kInk] : kIcons) {
        if (kKind == kind) {
            return {kIcon, kInk};
        }
    }
    return {Icon::Data, 0xA3A6AEFFU};
}

/// One source of `kind`, as its card says what it is.
std::string kindWord(std::string_view kind) {
    if (kind == "audio") {
        return "sound";
    }
    if (kind == "kest") {
        return "script";
    }
    if (kind == "canvasmaterial") {
        return "canvas material";
    }
    if (kind == "postprocess") {
        return "post process";
    }
    return std::string{kind};
}

/// What a document Studio opens is, as a card names it.
std::pair<std::string_view, std::string_view> documentKind(Listed listed) {
    switch (listed) {
    case Listed::Scene:
        return {"scene", "scene"};
    case Listed::Sheet:
        return {"text", "style sheet"};
    case Listed::Ui:
        return {"text", "ui document"};
    case Listed::Material:
        return {"material", "material"};
    case Listed::Graph:
        return {"material", "material"};
    case Listed::Data:
        return {"data", "data"};
    case Listed::Settings:
        return {"game", "settings"};
    case Listed::Capture:
        return {"texture", "capture"};
    case Listed::Blockout:
        return {"mesh", "blockout"};
    case Listed::Terrain:
        return {"mesh", "terrain"};
    case Listed::Tilemap:
        return {"texture", "tilemap"};
    case Listed::Scatter:
        return {"mesh", "scatter"};
    case Listed::Sequence:
        return {"animation", "sequence"};
    }
    return {"data", "document"};
}

} // namespace

result::Result<ui::Node> ShellParticipant::assetCard(const AssetCard& card) {
    const bool kTiles = browser_.thumbnail > 0;
    const auto [kIcon, kInk] = kindIcon(card.kind);
    if (!kTiles) {
        // The list: a row each, its icon, name, and what it is.
        RAWFRAME_TRY_ASSIGN(const ui::Node kRowNode,
                            box(browser_.grid,
                                ui::Layout{.width = ui::share(1),
                                           .height = ui::pixels(26),
                                           .alignItems = ui::Align::Center,
                                           .gap = 8,
                                           .shrink = 0,
                                           .padding = {8, 8, 0, 0}},
                                0));
        lightAsset(kRowNode, card.chosen);
        RAWFRAME_TRY(icon(kRowNode, kIcon, kInk, 14));
        RAWFRAME_TRY_ASSIGN(const ui::Node kName, box(kRowNode, ui::Layout{.width = ui::pixels(260), .shrink = 0}, 0));
        RAWFRAME_TRY(words(kName, card.name, look::kRowInk, look::kBodySize));
        RAWFRAME_TRY_ASSIGN(const ui::Node kWhat, box(kRowNode, ui::Layout{.shrink = 0}, 0));
        RAWFRAME_TRY(monoWords(kWhat, card.what, card.failed ? look::kError : look::kFaintInk, look::kSmallSize));
        for (const ui::Node kPart : {kName, kWhat}) {
            RAWFRAME_TRY(tree_->setInteraction(kPart, ui::Interaction{.hits = ui::Interaction::Hits::Nothing}));
        }
        RAWFRAME_TRY(tree_->setRole(kRowNode, ui::Role::Button));
        RAWFRAME_TRY(tree_->setName(kRowNode, card.name));
        return kRowNode;
    }
    // A card: the picture or the kind's icon on a tile, a dot where the
    // cook is behind, then the name and what it is.
    constexpr float kWide = 112;
    constexpr float kTall = 84;
    RAWFRAME_TRY_ASSIGN(const ui::Node kCard,
                        box(browser_.grid,
                            ui::Layout{.width = ui::pixels(kWide + 10),
                                       .direction = ui::Direction::Column,
                                       .gap = 6,
                                       .shrink = 0,
                                       .padding = {4, 4, 4, 6},
                                       .border = {1, 1, 1, 1}},
                            0));
    lightAsset(kCard, card.chosen);
    RAWFRAME_TRY_ASSIGN(const ui::Node kTile,
                        box(kCard,
                            ui::Layout{.width = ui::pixels(kWide),
                                       .height = ui::pixels(kTall),
                                       .justify = ui::Justify::Center,
                                       .alignItems = ui::Align::Center,
                                       .shrink = 0,
                                       .border = {1, 1, 1, 1}},
                            0));
    RAWFRAME_TRY(tree_->setLook(
        kTile, ui::Look{.fill = look::kRaisedFill, .borderColor = look::kEdge, .radius = 5, .clip = true}));
    RAWFRAME_TRY(tree_->setInteraction(kTile, ui::Interaction{.hits = ui::Interaction::Hits::Nothing}));
    if (card.picture != 0) {
        RAWFRAME_TRY_ASSIGN(
            const ui::Node kImage,
            box(kTile, ui::Layout{.width = ui::pixels(kWide - 2), .height = ui::pixels(kTall - 2), .shrink = 0}, 0));
        RAWFRAME_TRY(tree_->setLook(kImage, ui::Look{.image = card.picture}));
        RAWFRAME_TRY(tree_->setInteraction(kImage, ui::Interaction{.hits = ui::Interaction::Hits::Nothing}));
    } else {
        RAWFRAME_TRY(icon(kTile, kIcon, kInk, 34));
    }
    if (card.dot != 0) {
        RAWFRAME_TRY_ASSIGN(
            const ui::Node kDot,
            box(kTile,
                ui::Layout{.width = ui::pixels(7),
                           .height = ui::pixels(7),
                           .placement = {.absolute = true, .x = ui::pixels(kWide - 14), .y = ui::pixels(6)}},
                0));
        RAWFRAME_TRY(tree_->setLook(kDot, ui::Look{.fill = card.dot, .radius = 4}));
        RAWFRAME_TRY(tree_->setInteraction(kDot, ui::Interaction{.hits = ui::Interaction::Hits::Nothing}));
    }
    RAWFRAME_TRY_ASSIGN(const ui::Node kName,
                        box(kCard, ui::Layout{.width = ui::pixels(kWide), .shrink = 0, .padding = {1, 0, 0, 0}}, 0));
    RAWFRAME_TRY(words(kName, card.name, look::kRowInk, look::kLabelSize));
    RAWFRAME_TRY_ASSIGN(const ui::Node kWhat,
                        box(kCard, ui::Layout{.width = ui::pixels(kWide), .shrink = 0, .padding = {1, 0, 0, 0}}, 0));
    RAWFRAME_TRY(monoWords(kWhat, card.what, card.failed ? look::kError : look::kFaintInk, look::kSmallSize));
    for (const ui::Node kPart : {kName, kWhat}) {
        RAWFRAME_TRY(tree_->setInteraction(kPart, ui::Interaction{.hits = ui::Interaction::Hits::Nothing}));
    }
    RAWFRAME_TRY(tree_->setRole(kCard, ui::Role::Button));
    RAWFRAME_TRY(tree_->setName(kCard, card.name));
    return kCard;
}

void ShellParticipant::lightAsset(ui::Node node, bool chosen) {
    static_cast<void>(tree_->setLook(
        node, ui::Look{.fill = chosen ? 0x5B9BFF1FU : 0, .borderColor = chosen ? look::kAccent : 0, .radius = 6}));
}

result::Status ShellParticipant::showBrowser() {
    for (const ui::Node kBox : browser_.boxes) {
        static_cast<void>(tree_->remove(kBox));
    }
    browser_.boxes.clear();
    browser_.kindRows.clear();
    browser_.folderRows.clear();
    browser_.sizeRows.clear();
    browser_.import = ui::Node{};
    if (!tree_->contains(browser_.grid)) {
        return {};
    }
    // The kinds and folders the sources have, each how many it holds.
    std::set<std::string> kinds;
    std::map<std::string, std::size_t> folders;
    std::size_t every = 0;
    for (const ListedSource& kSource : browser_.sources) {
        kinds.insert(kindOf(kSource.importer));
        ++every;
        if (!kSource.folder.empty()) {
            ++folders[kSource.folder];
        }
    }
    for (const std::string& kScene : scenes_) {
        every += browser_.find(kScene) == nullptr ? 1 : 0;
    }
    every += authoring_session::filesBeside(description_, ".kest").size();
    const auto kHeld = [this](result::Result<ui::Node> made) -> result::Result<ui::Node> {
        if (made.has_value()) {
            browser_.boxes.push_back(*made);
        }
        return made;
    };
    // Where the grid is: the game, then the folder.
    RAWFRAME_TRY_ASSIGN(const ui::Node kGame, kHeld(box(browser_.crumbs, ui::Layout{.shrink = 0}, 0)));
    RAWFRAME_TRY(words(kGame, description_.stem().string(), look::kLabelInk, look::kBodySize));
    RAWFRAME_TRY(kHeld(icon(browser_.crumbs, Icon::ChevronRight, look::kOffInk, 12)));
    RAWFRAME_TRY_ASSIGN(const ui::Node kWhere, kHeld(box(browser_.crumbs, ui::Layout{.shrink = 0}, 0)));
    RAWFRAME_TRY(
        words(kWhere, browser_.folder.has_value() ? *browser_.folder : "Everything", look::kInk, look::kBodySize));
    // The kinds, All first, the one chosen lit.
    const auto kChip = [&](std::string_view text, bool on) -> result::Result<ui::Node> {
        RAWFRAME_TRY_ASSIGN(
            const ui::Node kMade,
            kHeld(
                box(browser_.chips,
                    ui::Layout{
                        .alignItems = ui::Align::Center, .shrink = 0, .padding = {8, 8, 3, 3}, .border = {1, 1, 1, 1}},
                    0)));
        RAWFRAME_TRY(tree_->setLook(kMade,
                                    ui::Look{.fill = on ? look::kSelected : look::kRaisedFill,
                                             .borderColor = on ? look::kSelectedEdge : look::kRaisedEdge,
                                             .radius = 5}));
        RAWFRAME_TRY(pressWords(kMade, text, on ? look::kInk : look::kSoftInk, look::kLabelSize));
        return kMade;
    };
    RAWFRAME_TRY_ASSIGN(const ui::Node kAll, kChip("All", !browser_.kind.has_value()));
    browser_.kindRows.emplace_back(kAll, std::string{});
    for (const std::string& kKind : kinds) {
        RAWFRAME_TRY_ASSIGN(const ui::Node kControl, kChip(kindLabel(kKind), browser_.kind == kKind));
        browser_.kindRows.emplace_back(kControl, kKind);
    }
    // The views, a list or cards (D801's sizes), then Import.
    for (const auto& [kWhich, kName, kSize] :
         {std::tuple<Icon, std::string_view, std::uint32_t>{Icon::List, "List", 0},
          std::tuple<Icon, std::string_view, std::uint32_t>{Icon::TilesView, "Thumbnails", 128}}) {
        RAWFRAME_TRY_ASSIGN(const ui::Node kControl,
                            kHeld(iconButton(browser_.tools, kWhich, kName, browser_.thumbnail == kSize)));
        browser_.sizeRows.emplace_back(kControl, kSize);
    }
    RAWFRAME_TRY_ASSIGN(browser_.import, kHeld(textButton(browser_.tools, Icon::Import, "Import")));
    // A new scene is the session's own verb, not an operation the catalog
    // lists, so it is always offered. A file's absolute path typed there
    // imports it instead (D505).
    RAWFRAME_TRY_ASSIGN(const auto kNew,
                        searchWell(browser_.tools, "new scene or import", "New scene", "", 150, Icon::Plus));
    browser_.boxes.push_back(kNew.first);
    newSceneRow_ = kNew.first;
    newScene_ =
        FieldRow{.value = kNew.second, .role = FieldRow::Role::NewScene, .field = "new scene", .hint = "New scene"};
    // The folders: the game's root, Everything, then each folder, each with
    // its count, the one shown lit; under them, where files are dropped.
    const auto kFolderRow = [&](std::string_view text,
                                Icon which,
                                std::optional<std::size_t> count,
                                float indent,
                                bool on) -> result::Result<ui::Node> {
        RAWFRAME_TRY_ASSIGN(const ui::Node kRowNode,
                            kHeld(box(browser_.folders,
                                      ui::Layout{.height = ui::pixels(24),
                                                 .alignItems = ui::Align::Center,
                                                 .gap = 6,
                                                 .shrink = 0,
                                                 .padding = {indent, 8, 0, 0}},
                                      0)));
        RAWFRAME_TRY(tree_->setLook(kRowNode, ui::Look{.fill = on ? look::kSelected : 0, .radius = 4}));
        RAWFRAME_TRY(icon(kRowNode, which, on ? look::kInk : look::kLabelInk, 13));
        RAWFRAME_TRY_ASSIGN(const ui::Node kLabel, box(kRowNode, ui::Layout{.grow = 1}, 0));
        RAWFRAME_TRY(words(kLabel, text, on ? look::kInk : look::kRowInk, look::kBodySize));
        RAWFRAME_TRY(tree_->setInteraction(kLabel, ui::Interaction{.hits = ui::Interaction::Hits::Nothing}));
        if (count.has_value()) {
            RAWFRAME_TRY_ASSIGN(const ui::Node kCount, box(kRowNode, ui::Layout{.shrink = 0}, 0));
            RAWFRAME_TRY(monoWords(kCount, std::to_string(*count), look::kFaintInk, look::kSmallSize));
            RAWFRAME_TRY(tree_->setInteraction(kCount, ui::Interaction{.hits = ui::Interaction::Hits::Nothing}));
        }
        RAWFRAME_TRY(tree_->setRole(kRowNode, ui::Role::Button));
        RAWFRAME_TRY(tree_->setName(kRowNode, text));
        return kRowNode;
    };
    RAWFRAME_TRY(kFolderRow(description_.stem().string(), Icon::ChevronDown, std::nullopt, 6, false));
    RAWFRAME_TRY_ASSIGN(const ui::Node kEvery,
                        kFolderRow("Everything", Icon::Layers, every, 20, !browser_.folder.has_value()));
    browser_.folderRows.emplace_back(kEvery, std::string{});
    for (const auto& [kFolder, kCount] : folders) {
        RAWFRAME_TRY_ASSIGN(const ui::Node kRowNode,
                            kFolderRow(kFolder,
                                       browser_.folder == kFolder ? Icon::FolderOpen : Icon::Folder,
                                       kCount,
                                       20,
                                       browser_.folder == kFolder));
        browser_.folderRows.emplace_back(kRowNode, kFolder);
    }
    RAWFRAME_TRY(kHeld(box(browser_.folders, ui::Layout{.grow = 1}, 0)));
    RAWFRAME_TRY_ASSIGN(
        const ui::Node kDrop,
        kHeld(box(browser_.folders,
                  ui::Layout{
                      .justify = ui::Justify::Center, .shrink = 0, .padding = {10, 10, 12, 12}, .border = {1, 1, 1, 1}},
                  0)));
    RAWFRAME_TRY(tree_->setLook(kDrop, ui::Look{.borderColor = look::kEdge, .radius = 6}));
    RAWFRAME_TRY_ASSIGN(const ui::Node kDropWords, box(kDrop, ui::Layout{.shrink = 0}, 0));
    RAWFRAME_TRY(words(kDropWords,
                       browser_.folder.has_value() ? "Drop files here to import into " + *browser_.folder
                                                   : std::string{"Drop files here to import them"},
                       look::kFaintInk,
                       look::kLabelSize));
    return {};
}

result::Status ShellParticipant::showDocuments() {
    if (!tree_->contains(browser_.grid)) {
        sceneRows_.assign(scenes_.size(), ui::Node{});
        return {};
    }
    // The documents Studio opens, each a card, the one open lit.
    for (std::size_t each = 0; each < scenes_.size(); ++each) {
        if (!browser_.shows(scenes_[each])) {
            sceneRows_.emplace_back();
            continue;
        }
        const Listed kListed = each < listed_.size() ? listed_[each] : Listed::Scene;
        const auto [kKind, kWhat] = documentKind(kListed);
        const ListedSource* kSource = browser_.find(scenes_[each]);
        const bool kFailed = kSource != nullptr && kSource->cook == "failed";
        const std::shared_ptr<const texture::Texture> kPicture =
            kSource != nullptr && (kListed == Listed::Material || kListed == Listed::Graph) ? thumbnailOf(*kSource)
                                                                                            : nullptr;
        RAWFRAME_TRY_ASSIGN(const ui::Node kCard,
                            assetCard(AssetCard{.name = documentLabel(scenes_[each]),
                                                .kind = std::string{kKind},
                                                .what = kFailed ? std::string{"cook failed"} : std::string{kWhat},
                                                .picture = kPicture != nullptr ? thumbnailKey(scenes_[each]) : 0,
                                                .dot = kFailed                                           ? look::kError
                                                       : kSource != nullptr && kSource->cook != "cooked" ? look::kWarn
                                                                                                         : 0,
                                                .failed = kFailed,
                                                .chosen = each == sceneAt_ && !scene_.empty()}));
        sceneRows_.push_back(kCard);
    }
    // The game's Kest scripts, each opened in the author's editor (D919).
    browser_.scriptRows.clear();
    if (browser_.folder.has_value() || (browser_.kind.has_value() && *browser_.kind != "kest")) {
        return {};
    }
    for (const std::filesystem::path& kFile : authoring_session::filesBeside(description_, ".kest")) {
        std::error_code error;
        const std::string kRelative =
            std::filesystem::relative(kFile, description_.parent_path(), error).generic_string();
        if (error || !named(kRelative, browser_.search)) {
            continue;
        }
        RAWFRAME_TRY_ASSIGN(const ui::Node kCard,
                            assetCard(AssetCard{.name = kFile.filename().string(), .kind = "kest", .what = "script"}));
        browser_.boxes.push_back(kCard);
        browser_.scriptRows.emplace_back(kCard, kRelative);
    }
    return {};
}

result::Status ShellParticipant::showSources() {
    browser_.sourceRows.clear();
    if (!tree_->contains(browser_.grid)) {
        return {};
    }
    // The sources that are no document Studio opens, each a card with its
    // kind and how the cooks left it.
    for (const ListedSource& kSource : browser_.sources) {
        if (std::ranges::contains(scenes_, kSource.path) || !browser_.shows(kSource.path)) {
            continue;
        }
        const std::string kKind = kindOf(kSource.importer);
        const std::shared_ptr<const texture::Texture> kPicture = thumbnailOf(kSource);
        const bool kFailed = kSource.cook == "failed";
        RAWFRAME_TRY_ASSIGN(const ui::Node kCard,
                            assetCard(AssetCard{.name = kSource.path.substr(kSource.path.find_last_of('/') + 1),
                                                .kind = kKind,
                                                .what = kFailed ? std::string{"import failed"} : kindWord(kKind),
                                                .picture = kPicture != nullptr ? thumbnailKey(kSource.path) : 0,
                                                .dot = kFailed                    ? look::kError
                                                       : kSource.cook != "cooked" ? look::kWarn
                                                                                  : 0,
                                                .failed = kFailed,
                                                .chosen = kSource.path == browser_.chosen}));
        browser_.boxes.push_back(kCard);
        browser_.sourceRows.emplace_back(kCard, kSource.path);
    }
    return {};
}

bool ShellParticipant::pressBrowser(ui::Node node) {
    // The chosen source cooked again, its settings as the sidecar now has
    // them (D800): the game's cook, which reuses every other source.
    if (node == browser_.references && browser_.references != ui::Node{}) {
        showReferences();
        return true;
    }
    // A source listed among the references chosen, as in the list (D803).
    if (const auto kListed = std::ranges::find(browser_.referenceRows, node, &std::pair<ui::Node, std::string>::first);
        kListed != browser_.referenceRows.end()) {
        browser_.chosen = kListed->second;
        ++browser_.chosenCount;
        static_cast<void>(showScenes());
        showImportSettings();
        showReferences();
        return true;
    }
    // A script opens in the author's editor (D919).
    if (const auto kScript = std::ranges::find(browser_.scriptRows, node, &std::pair<ui::Node, std::string>::first);
        kScript != browser_.scriptRows.end()) {
        openEditor(Diagnostic{.file = "game/" + kScript->second, .line = 1, .column = 1});
        return true;
    }
    // The search, typed into as the grid narrows (D916).
    if (node == browser_.searchValue && browser_.searchValue != ui::Node{}) {
        beginEdit(FieldRow{
            .value = browser_.searchValue, .role = FieldRow::Role::AssetSearch, .field = "search", .hint = "Search"});
        return true;
    }
    if (node == browser_.import && browser_.import != ui::Node{}) {
        importAsked();
        return true;
    }
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
    if (const auto kSize = std::ranges::find(browser_.sizeRows, node, &std::pair<ui::Node, std::uint32_t>::first);
        kSize != browser_.sizeRows.end()) {
        browser_.thumbnail = kSize->second;
        static_cast<void>(showScenes());
        say(browser_.thumbnail == 0 ? "assets listed" : "assets as thumbnails");
        return true;
    }
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
    // Lit as the list is made again.
    static_cast<void>(showScenes());
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
    browser_.referenceRows.clear();
    const auto kParsed = document::parse(ask(importSettingsRecord(next(), browser_.chosen)));
    const Value* kAnswer = kParsed.has_value() ? kParsed->find("answer") : nullptr;
    const Value* kImporter = kAnswer != nullptr ? kAnswer->find("importer") : nullptr;
    if (kImporter == nullptr || kImporter->text() == nullptr) {
        return;
    }
    const auto kKept = [this](result::Result<ui::Node> made) {
        if (made.has_value()) {
            componentRows_.push_back(*made);
        }
        return made;
    };
    // As the design's (D916): its picture or its kind's icon, its name and
    // kind, how the cooks left it, then its importer's settings, what uses
    // it, and Reimport.
    const ListedSource* kSource = browser_.find(browser_.chosen);
    const std::string kKind = kindOf(*kImporter->text());
    const std::shared_ptr<const texture::Texture> kPicture = kSource != nullptr ? thumbnailOf(*kSource) : nullptr;
    if (auto tile = kKept(box(componentsColumn_,
                              ui::Layout{.height = ui::pixels(180),
                                         .justify = ui::Justify::Center,
                                         .alignItems = ui::Align::Center,
                                         .shrink = 0,
                                         .margin = {6, 6, 4, 8},
                                         .border = {1, 1, 1, 1}},
                              0));
        tile.has_value()) {
        static_cast<void>(tree_->setLook(
            *tile, ui::Look{.fill = look::kRaisedFill, .borderColor = look::kEdge, .radius = 6, .clip = true}));
        if (kPicture != nullptr) {
            if (auto image =
                    box(*tile, ui::Layout{.width = ui::pixels(176), .height = ui::pixels(176), .shrink = 0}, 0);
                image.has_value()) {
                static_cast<void>(tree_->setLook(*image, ui::Look{.image = thumbnailKey(browser_.chosen)}));
            }
        } else {
            const auto [kIcon, kInk] = kindIcon(kKind);
            static_cast<void>(icon(*tile, kIcon, kInk, 48));
        }
    }
    if (auto head =
            kKept(box(componentsColumn_,
                      ui::Layout{.alignItems = ui::Align::Center, .gap = 8, .shrink = 0, .padding = {8, 8, 2, 6}},
                      0));
        head.has_value()) {
        if (auto name = box(*head, ui::Layout{.shrink = 1}, 0); name.has_value()) {
            static_cast<void>(
                strongWords(*name, browser_.chosen.substr(browser_.chosen.find_last_of('/') + 1), look::kInk, 14));
        }
        if (auto kind = box(*head, ui::Layout{.shrink = 0}, 0); kind.has_value()) {
            static_cast<void>(words(*kind, kindWord(kKind), look::kFaintInk, look::kLabelSize));
        }
    }
    // A cook behind, or one that failed and why.
    if (kSource != nullptr && kSource->cook != "cooked") {
        const bool kFailed = kSource->cook == "failed";
        if (auto banner = kKept(
                box(componentsColumn_,
                    ui::Layout{.shrink = 0, .padding = {10, 10, 7, 7}, .margin = {6, 6, 4, 4}, .border = {1, 1, 1, 1}},
                    0));
            banner.has_value()) {
            static_cast<void>(tree_->setLook(*banner,
                                             ui::Look{.fill = kFailed ? 0x3A1E22FFU : 0x33291AFFU,
                                                      .borderColor = kFailed ? 0x6B2F35FFU : 0x5C4520FFU,
                                                      .radius = 5}));
            static_cast<void>(words(*banner,
                                    kFailed
                                        ? "Import failed" + (kSource->problem.empty() ? "" : ": " + kSource->problem)
                                        : std::string{"Not cooked yet: cook to use it"},
                                    kFailed ? look::kError : look::kWarn,
                                    look::kLabelSize));
        }
    }
    const auto kSection = [&](std::string_view title, std::string_view quiet) {
        auto heading = kKept(box(componentsColumn_,
                                 ui::Layout{.alignItems = ui::Align::Center,
                                            .gap = 8,
                                            .shrink = 0,
                                            .padding = {8, 8, 8, 6},
                                            .margin = {0, 0, 6, 0},
                                            .border = {0, 0, 1, 0}},
                                 0));
        if (!heading.has_value()) {
            return;
        }
        static_cast<void>(tree_->setLook(*heading, ui::Look{.borderColor = 0x24262CFFU}));
        if (auto titled = box(*heading, ui::Layout{.shrink = 0}, 0); titled.has_value()) {
            static_cast<void>(strongWords(*titled, title, look::kInk));
        }
        if (auto quieted = box(*heading, ui::Layout{.shrink = 1}, 0); quieted.has_value()) {
            static_cast<void>(monoWords(*quieted, quiet, look::kFaintInk, look::kSmallSize));
        }
    };
    kSection("Import", *kImporter->text());
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
        const Value* kSetting = kSettings != nullptr && kSettings->kind() == Value::Kind::Object
                                    ? kSettings->find(*kName->text())
                                    : nullptr;
        const Value* kValue = kSetting != nullptr ? kSetting : kOffered.find("fallback");
        Catalog::Field type{.name = *kName->text(), .kind = *kType->text()};
        if (const Value* kChoices = kOffered.find("choices"); kChoices != nullptr) {
            for (const Value& kChoice : kChoices->items()) {
                if (kChoice.text() != nullptr) {
                    type.cases.push_back(*kChoice.text());
                }
            }
        }
        const std::string kValueText = kValue != nullptr ? shownText(*kValue) : std::string{};
        auto line =
            fieldRow(*kName->text(), kValueText, std::nullopt, kSetting != nullptr, prettyField(*kName->text()));
        if (!line.has_value()) {
            return;
        }
        componentRows_.push_back(line->first);
        if (*kType->text() == "truth") {
            asCheckbox(line->second, kValueText == "true");
        } else if (!type.cases.empty()) {
            asChooser(line->first);
        }
        fields_.push_back(FieldRow{.value = line->second,
                                   .role = FieldRow::Role::ImportSetting,
                                   .field = *kName->text(),
                                   .kind = *kType->text(),
                                   .type = std::move(type)});
    }
    // What uses it, each pressed to choose it (D803).
    const auto kReferences = document::parse(ask(assetReferencesRecord(next(), browser_.chosen)));
    const Value* kFound = kReferences.has_value() ? kReferences->find("answer") : nullptr;
    const Value* kUsedBy = kFound != nullptr ? kFound->find("usedBy") : nullptr;
    const std::size_t kUsers = kUsedBy != nullptr ? kUsedBy->items().size() : 0;
    kSection("Used by", std::to_string(kUsers));
    for (const Value& kPath : kUsedBy != nullptr ? kUsedBy->items() : std::span<const Value>{}) {
        if (kPath.text() == nullptr) {
            continue;
        }
        if (auto made = kKept(row(componentsColumn_, *kPath.text(), kText, kRow, true)); made.has_value()) {
            browser_.referenceRows.emplace_back(*made, *kPath.text());
        }
    }
    if (kUsers == 0) {
        static_cast<void>(kKept(row(componentsColumn_, "Nothing uses it yet", look::kFaintInk, kRow, false)));
    }
    if (auto buttons =
            kKept(box(componentsColumn_,
                      ui::Layout{.alignItems = ui::Align::Center, .gap = 8, .shrink = 0, .padding = {8, 8, 12, 8}},
                      0));
        buttons.has_value()) {
        if (auto made = textButton(*buttons, std::nullopt, "Reimport", ButtonKind::Primary); made.has_value()) {
            browser_.reimport = *made;
        }
        // Everything that uses it, in the center (D803), beside Reimport
        // (D925).
        if (auto made = textButton(*buttons, std::nullopt, "References", ButtonKind::Normal); made.has_value()) {
            browser_.references = *made;
        }
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

std::shared_ptr<const texture::Texture> ShellParticipant::thumbnailOf(const ListedSource& source) {
    if (browser_.thumbnail == 0 || (source.importer != "rawframe.texture" && source.importer != "rawframe.mesh" &&
                                    source.importer != "rawframe.material")) {
        return nullptr;
    }
    const std::uint64_t kKey = thumbnailKey(source.path);
    if (const auto kHeld = browser_.pictures.find(kKey); kHeld != browser_.pictures.end()) {
        return kHeld->second;
    }
    // Where the session's last cook holds it, and which level fits; none
    // before a cook, which is said as the row's state.
    const auto kParsed = document::parse(ask(thumbnailRecord(next(), source.path, browser_.thumbnail)));
    const Value* kAnswer = kParsed.has_value() ? kParsed->find("answer") : nullptr;
    // A mesh's is being drawn beside the session: asked again shortly.
    const Value* kError = kParsed.has_value() ? kParsed->find("error") : nullptr;
    const Value* kClass = kError != nullptr ? kError->find("class") : nullptr;
    if (kClass != nullptr && kClass->text() != nullptr && *kClass->text() == "unavailable") {
        browser_.drawing = true;
    }
    const Value* kObject = kAnswer != nullptr ? kAnswer->find("object") : nullptr;
    const Value* kLevel = kAnswer != nullptr ? kAnswer->find("level") : nullptr;
    if (kObject == nullptr || kObject->text() == nullptr || kLevel == nullptr || !kLevel->integer().has_value()) {
        return nullptr;
    }
    const std::optional<std::string> kBytes = authoring_session::readFile(*kObject->text());
    if (!kBytes.has_value()) {
        return nullptr;
    }
    auto decoded = texture::decode(std::as_bytes(std::span{kBytes->data(), kBytes->size()}));
    const auto kAt = static_cast<std::size_t>(*kLevel->integer());
    if (!decoded.has_value() || kAt >= decoded->levels.size()) {
        return nullptr;
    }
    // From the level that fits down, as the UI draws a texture's first.
    decoded->levels.erase(decoded->levels.begin(), decoded->levels.begin() + static_cast<std::ptrdiff_t>(kAt));
    auto made = std::make_shared<const texture::Texture>(std::move(*decoded));
    browser_.pictures.emplace(kKey, made);
    ++browser_.thumbnailsMade;
    return made;
}

bool ShellParticipant::importFile(const std::string& path, const std::string& as) {
    const std::string kReply = ask(importRecord(next(), path, as));
    const std::string kImported = importedIn(kReply);
    if (kImported.empty()) {
        ++refused_;
        say(answeredOf(kReply).message);
        return false;
    }
    ++applied_;
    const std::string kAssets = ask(assetsRecord(next()));
    assets_ = assetsOf(kAssets);
    browser_.sources = sourcesOf(kAssets);
    say("imported " + kImported);
    return true;
}

void ShellParticipant::importAsked() {
    // The files to bring in, from the platform's dialog (D918); where it
    // has none, typed as a path into the new scene's field or dropped on
    // the window (D802, D505).
    if (dialogs_ != nullptr) {
        askDialog(view::FileDialogs::Kind::OpenMany, "Import into the game", "import");
        return;
    }
    if (newScene_.has_value()) {
        beginEdit(*newScene_);
    }
    say("type a file's absolute path and press Enter, or drop files on the window");
}

void ShellParticipant::takeDrops() {
    if (dropped_ == nullptr) {
        return;
    }
    bool any = false;
    while (std::optional<view::FilesDropped::Drop> drop = dropped_->take()) {
        // Into the folder the browser shows, under each file's own name; the
        // session refuses what is no asset it imports, which is said.
        for (const std::string& kPath : drop->paths) {
            ++browser_.dropped;
            const std::string kName = std::filesystem::path{kPath}.filename().generic_string();
            const std::string kAs = browser_.folder.has_value() ? *browser_.folder + "/" + kName : kName;
            browser_.droppedImported += importFile(kPath, kAs) ? 1 : 0;
            any = true;
        }
        if (drop->truncated) {
            say("some of the files dropped were left out");
        }
    }
    if (any) {
        static_cast<void>(showScenes());
    }
}

void ShellParticipant::showReferences() {
    for (const ui::Node kBox : browser_.referenceBoxes) {
        static_cast<void>(tree_->remove(kBox));
    }
    browser_.referenceBoxes.clear();
    browser_.referenceRows.clear();
    const std::string kReply = ask(assetReferencesRecord(next(), browser_.chosen));
    const auto kParsed = document::parse(kReply);
    const Value* kAnswer = kParsed.has_value() ? kParsed->find("answer") : nullptr;
    if (kAnswer == nullptr) {
        say(answeredOf(kReply).message);
        return;
    }
    // Under the document's tabs, in place of a scene's view: what it uses,
    // then what uses it, each source pressed to choose it.
    clear(viewRows_);
    viewFields_.clear();
    clear(held_.cameraRows);
    // As the design's lists: a heading with its count, then each source
    // with its kind's mark, pressed to choose it and see its own (D925).
    const auto kSection = [&](std::string_view title, const Value* listed) {
        const std::size_t kCount = listed != nullptr ? listed->items().size() : 0;
        if (auto header =
                box(centerColumn_,
                    ui::Layout{.alignItems = ui::Align::Center, .gap = 8, .shrink = 0, .padding = {16, 16, 14, 6}},
                    0);
            header.has_value()) {
            browser_.referenceBoxes.push_back(*header);
            if (auto named = box(*header, ui::Layout{.shrink = 0}, 0); named.has_value()) {
                static_cast<void>(strongWords(*named, std::string{title}, look::kInk));
            }
            if (auto counted = box(*header, ui::Layout{.shrink = 0}, 0); counted.has_value()) {
                static_cast<void>(monoWords(*counted, std::to_string(kCount), look::kFaintInk, look::kSmallSize));
            }
        }
        if (kCount == 0) {
            if (auto none = box(centerColumn_, ui::Layout{.shrink = 0, .padding = {16, 16, 2, 6}}, 0);
                none.has_value()) {
                browser_.referenceBoxes.push_back(*none);
                static_cast<void>(words(*none, "Nothing", look::kFaintInk, look::kBodySize));
            }
            return;
        }
        for (const Value& kPath : listed->items()) {
            if (kPath.text() == nullptr) {
                continue;
            }
            const auto kSource = std::ranges::find(browser_.sources, *kPath.text(), &ListedSource::path);
            const auto [kIcon, kInk] = kindIcon(kSource != browser_.sources.end() ? kindOf(kSource->importer) : "");
            if (auto made = entityRow(centerColumn_, *kPath.text(), 0, false, kIcon, kInk, look::kRowInk);
                made.has_value()) {
                static_cast<void>(tree_->setLayout(*made,
                                                   ui::Layout{.height = ui::pixels(28),
                                                              .alignItems = ui::Align::Center,
                                                              .gap = 8,
                                                              .shrink = 0,
                                                              .padding = {16, 16, 0, 0},
                                                              .margin = {8, 8, 0, 0}}));
                browser_.referenceBoxes.push_back(*made);
                browser_.referenceRows.emplace_back(*made, *kPath.text());
            }
        }
    };
    if (tree_->contains(held_.modePalette)) {
        static_cast<void>(tree_->detach(held_.modePalette));
    }
    kSection(browser_.chosen + " uses", kAnswer->find("uses"));
    kSection("Used by", kAnswer->find("usedBy"));
    ++browser_.referencesShown;
    say(browser_.chosen + ": " + std::to_string(browser_.referenceRows.size()) + " references");
}

void ShellParticipant::pollThumbnails(double seconds) {
    if (!browser_.drawing || seconds - browser_.drawingAsked < 0.5) {
        return;
    }
    browser_.drawing = false;
    browser_.drawingAsked = seconds;
    static_cast<void>(showScenes());
}

} // namespace rawframe::studio
