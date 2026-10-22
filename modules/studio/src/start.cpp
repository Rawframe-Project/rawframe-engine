#include "rawframe/authoring_session/new_game.h"
#include "rawframe/process/self.h"
#include "rawframe/render/capture.h"
#include "shell.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <fstream>

namespace rawframe::studio {

namespace {

/// The templates in `directory`: each directory holding a game description,
/// by name.
std::vector<std::filesystem::path> templatesIn(const std::filesystem::path& directory) {
    std::vector<std::filesystem::path> found;
    std::error_code error;
    for (auto entry = std::filesystem::directory_iterator(directory, error);
         !error && entry != std::filesystem::directory_iterator();
         entry.increment(error)) {
        if (!entry->is_directory() || entry->is_symlink()) {
            continue;
        }
        std::error_code inner;
        for (const auto& file : std::filesystem::directory_iterator(entry->path(), inner)) {
            if (file.path().extension() == ".game") {
                found.push_back(entry->path());
                break;
            }
        }
    }
    std::ranges::sort(found);
    return found;
}

} // namespace

namespace {

/// What a template makes, as its card says: its description's first
/// comment, to its first full stop past the name in brackets.
std::string templateWords(const std::filesystem::path& folder) {
    if (folder.filename() == "starter-3d") {
        return "A lit scene of parts, a player, a camera, a server.";
    }
    return "A game to start from.";
}

/// How long ago `file` changed, as a list says it.
std::string ageOf(const std::filesystem::path& file) {
    std::error_code error;
    const auto kWritten = std::filesystem::last_write_time(file, error);
    if (error) {
        return {};
    }
    const auto kHours =
        std::chrono::duration_cast<std::chrono::hours>(std::filesystem::file_time_type::clock::now() - kWritten)
            .count();
    return kHours < 24 ? "today" : kHours < 48 ? "yesterday" : std::to_string(kHours / 24) + " days ago";
}

/// The game description a folder holds, if it holds one.
std::optional<std::filesystem::path> descriptionIn(const std::filesystem::path& folder) {
    std::error_code error;
    if (folder.extension() == ".game" && std::filesystem::is_regular_file(folder, error)) {
        return folder;
    }
    for (auto entry = std::filesystem::directory_iterator(folder, error);
         !error && entry != std::filesystem::directory_iterator();
         entry.increment(error)) {
        if (entry->path().extension() == ".game") {
            return entry->path();
        }
    }
    return std::nullopt;
}

} // namespace

result::Status ShellParticipant::showStart(ui::Node body) {
    templateRows_.clear();
    recentRows_.clear();
    newGameField_.reset();
    openGameField_.reset();
    gameFolderField_.reset();
    // As the design's start (D918): the games opened last on the left,
    // a new game from a template on the right.
    RAWFRAME_TRY_ASSIGN(const ui::Node kStart,
                        box(body, ui::Layout{.direction = ui::Direction::Row, .grow = 1}, look::kWindowFill));
    RAWFRAME_TRY(tree_->setName(kStart, "Start"));
    RAWFRAME_TRY_ASSIGN(const ui::Node kOpen,
                        box(kStart,
                            ui::Layout{.width = ui::pixels(0),
                                       .direction = ui::Direction::Column,
                                       .gap = 6,
                                       .grow = 1,
                                       .padding = {56, 56, 48, 40}},
                            0));
    RAWFRAME_TRY_ASSIGN(const ui::Node kTitle, box(kOpen, ui::Layout{.shrink = 0}, 0));
    RAWFRAME_TRY(strongWords(kTitle, "Open a game", look::kInk, 26));
    RAWFRAME_TRY_ASSIGN(const ui::Node kWhat, box(kOpen, ui::Layout{.shrink = 0, .margin = {0, 0, 4, 18}}, 0));
    RAWFRAME_TRY(words(kWhat,
                       "The games you opened last, newest first. A game is a folder with its description; Studio "
                       "opens it as it is on disk.",
                       look::kLabelInk,
                       look::kBodySize));
    RAWFRAME_TRY_ASSIGN(const ui::Node kFind,
                        box(kOpen, ui::Layout{.alignItems = ui::Align::Center, .gap = 10, .shrink = 0}, 0));
    RAWFRAME_TRY_ASSIGN(const auto kSearch,
                        searchWell(kFind, "open", "Find a game, or a game's path", gameSearch_, 0, Icon::Search));
    RAWFRAME_TRY(tree_->setLayout(kSearch.first,
                                  ui::Layout{.height = ui::pixels(30),
                                             .alignItems = ui::Align::Center,
                                             .gap = 6,
                                             .grow = 1,
                                             .padding = {10, 6, 0, 0},
                                             .border = {1, 1, 1, 1}}));
    openGameField_ = FieldRow{.value = kSearch.second,
                              .role = FieldRow::Role::GameSearch,
                              .field = "open",
                              .hint = "Find a game, or a game's path"};
    RAWFRAME_TRY_ASSIGN(openFolderNode_, textButton(kFind, Icon::Folder, "Open a folder"));
    RAWFRAME_TRY_ASSIGN(recentList_,
                        box(kOpen,
                            ui::Layout{.height = ui::pixels(0),
                                       .minHeight = ui::pixels(0),
                                       .direction = ui::Direction::Column,
                                       .gap = 6,
                                       .grow = 1,
                                       .margin = {0, 0, 10, 0},
                                       .scroll = ui::Scroll::Vertical},
                            0));
    RAWFRAME_TRY(tree_->setRole(recentList_, ui::Role::ScrollView));
    RAWFRAME_TRY(tree_->setName(recentList_, "Games"));
    showRecent();
    // What Studio starts a game with, beside it.
    RAWFRAME_TRY_ASSIGN(const ui::Node kBeside,
                        box(kOpen,
                            ui::Layout{.alignItems = ui::Align::Center,
                                       .gap = 18,
                                       .shrink = 0,
                                       .padding = {14, 14, 10, 10},
                                       .border = {1, 1, 1, 1}},
                            0));
    RAWFRAME_TRY(tree_->setLook(kBeside, ui::Look{.fill = look::kPanelFill, .borderColor = look::kLine, .radius = 6}));
    RAWFRAME_TRY_ASSIGN(const ui::Node kBesideTitle, box(kBeside, ui::Layout{.shrink = 0}, 0));
    RAWFRAME_TRY(strongWords(kBesideTitle, "Beside Studio", look::kInk, look::kLabelSize));
    for (const std::string_view kTool : {"rawframe-cook", "rawframe-server", "rawframe-client", "rawframe-author"}) {
        std::error_code error;
        std::filesystem::path program = process::besideSelf(std::string{kTool});
        const bool kThere =
            std::filesystem::exists(program, error) || std::filesystem::exists(program.string() + ".exe", error);
        RAWFRAME_TRY_ASSIGN(const ui::Node kItem,
                            box(kBeside, ui::Layout{.alignItems = ui::Align::Center, .gap = 6, .shrink = 0}, 0));
        RAWFRAME_TRY(box(kItem,
                         ui::Layout{.width = ui::pixels(7), .height = ui::pixels(7), .shrink = 0},
                         kThere ? look::kReady : look::kError));
        RAWFRAME_TRY_ASSIGN(const ui::Node kName, box(kItem, ui::Layout{.shrink = 0}, 0));
        RAWFRAME_TRY(words(kName, kTool, look::kSoftInk, look::kLabelSize));
    }
    // The new game: its name, its folder, its template, then Create.
    RAWFRAME_TRY_ASSIGN(const ui::Node kNew,
                        box(kStart,
                            ui::Layout{.width = ui::pixels(520),
                                       .direction = ui::Direction::Column,
                                       .gap = 6,
                                       .shrink = 0,
                                       .padding = {44, 44, 48, 40},
                                       .border = {1, 0, 0, 0}},
                            0));
    RAWFRAME_TRY(tree_->setLook(kNew, ui::Look{.fill = look::kBarFill, .borderColor = look::kLine}));
    RAWFRAME_TRY_ASSIGN(const ui::Node kNewTitle, box(kNew, ui::Layout{.shrink = 0}, 0));
    RAWFRAME_TRY(strongWords(kNewTitle, "New game", look::kInk, 22));
    RAWFRAME_TRY_ASSIGN(
        const ui::Node kNewWhat,
        box(kNew, ui::Layout{.alignItems = ui::Align::Center, .gap = 4, .shrink = 0, .margin = {0, 0, 2, 16}}, 0));
    RAWFRAME_TRY_ASSIGN(const ui::Node kNewWords, box(kNewWhat, ui::Layout{.shrink = 0}, 0));
    RAWFRAME_TRY(words(kNewWords, "Made from a template as", look::kLabelInk, look::kBodySize));
    RAWFRAME_TRY_ASSIGN(const ui::Node kNewTool, box(kNewWhat, ui::Layout{.shrink = 0}, 0));
    RAWFRAME_TRY(monoWords(kNewTool, "rawframe-author new", look::kSoftInk, look::kLabelSize));
    RAWFRAME_TRY_ASSIGN(const ui::Node kNewThen, box(kNewWhat, ui::Layout{.shrink = 0}, 0));
    RAWFRAME_TRY(words(kNewThen, "makes it, then opened.", look::kLabelInk, look::kBodySize));
    const auto kLabel = [&](std::string_view text) -> result::Status {
        RAWFRAME_TRY_ASSIGN(const ui::Node kMade, box(kNew, ui::Layout{.shrink = 0, .margin = {0, 0, 10, 2}}, 0));
        return words(kMade, text, look::kLabelInk, look::kLabelSize);
    };
    const auto kWell = [&](ui::Node parent, std::string_view name, std::string_view value) -> result::Result<ui::Node> {
        RAWFRAME_TRY_ASSIGN(const ui::Node kValue, tree_->addEditable(++keys_));
        RAWFRAME_TRY(tree_->setLayout(kValue,
                                      ui::Layout{.width = ui::pixels(0),
                                                 .minHeight = ui::pixels(30),
                                                 .grow = 1,
                                                 .padding = {10, 10, 6, 5},
                                                 .border = {1, 1, 1, 1}}));
        RAWFRAME_TRY(tree_->setLook(
            kValue, ui::Look{.fill = look::kWellFill, .borderColor = look::kEdge, .radius = 4, .clip = true}));
        RAWFRAME_TRY(tree_->attach(parent, kValue));
        RAWFRAME_TRY(words(kValue, value, kText, look::kBodySize));
        RAWFRAME_TRY(tree_->setRole(kValue, ui::Role::TextInput));
        RAWFRAME_TRY(tree_->setName(kValue, name));
        return kValue;
    };
    // A name no game in the folder has yet.
    // As a Kest project is named: lowercase letters, digits, and _.
    std::string name = "my_game";
    for (int each = 2; std::filesystem::exists(projects_ / name); ++each) {
        name = "my_game_" + std::to_string(each);
    }
    RAWFRAME_TRY(kLabel("Name"));
    RAWFRAME_TRY_ASSIGN(const ui::Node kNameLine, box(kNew, ui::Layout{.shrink = 0}, 0));
    RAWFRAME_TRY_ASSIGN(const ui::Node kName, kWell(kNameLine, "new game", name));
    newGameField_ = FieldRow{.value = kName, .role = FieldRow::Role::NewGame, .field = "new game"};
    RAWFRAME_TRY(kLabel("Folder"));
    RAWFRAME_TRY_ASSIGN(const ui::Node kFolderLine,
                        box(kNew, ui::Layout{.alignItems = ui::Align::Center, .gap = 8, .shrink = 0}, 0));
    RAWFRAME_TRY_ASSIGN(const ui::Node kFolder, kWell(kFolderLine, "folder", projects_.string()));
    gameFolderField_ = FieldRow{.value = kFolder, .role = FieldRow::Role::GameFolder, .field = "folder"};
    RAWFRAME_TRY_ASSIGN(chooseFolderNode_, textButton(kFolderLine, std::nullopt, "..."));
    RAWFRAME_TRY(kLabel("Template"));
    RAWFRAME_TRY_ASSIGN(const ui::Node kCards, box(kNew, ui::Layout{.gap = 10, .shrink = 0, .wrap = true}, 0));
    const std::vector<std::filesystem::path> kTemplates = templatesIn(templates_);
    templateAt_ = std::min(templateAt_, kTemplates.empty() ? 0 : kTemplates.size() - 1);
    for (std::size_t each = 0; each < kTemplates.size(); ++each) {
        const bool kOn = each == templateAt_;
        RAWFRAME_TRY_ASSIGN(const ui::Node kCard,
                            box(kCards,
                                ui::Layout{.width = ui::pixels(206),
                                           .direction = ui::Direction::Column,
                                           .gap = 6,
                                           .shrink = 0,
                                           .padding = {10, 10, 10, 12},
                                           .border = {1, 1, 1, 1}},
                                0));
        RAWFRAME_TRY(tree_->setLook(kCard,
                                    ui::Look{.fill = kOn ? 0x1B2440FFU : look::kPanelFill,
                                             .borderColor = kOn ? look::kAccent : look::kEdge,
                                             .radius = 6}));
        RAWFRAME_TRY_ASSIGN(const ui::Node kTile,
                            box(kCard,
                                ui::Layout{.height = ui::pixels(92),
                                           .justify = ui::Justify::Center,
                                           .alignItems = ui::Align::Center,
                                           .shrink = 0},
                                0));
        RAWFRAME_TRY(tree_->setLook(kTile, ui::Look{.fill = 0x1E2233FFU, .radius = 5}));
        RAWFRAME_TRY(tree_->setInteraction(kTile, ui::Interaction{.hits = ui::Interaction::Hits::Nothing}));
        RAWFRAME_TRY(icon(
            kTile, kTemplates[each].filename().string().ends_with("2d") ? Icon::Tiles : Icon::Cube, look::kAccent, 34));
        std::string shown = kTemplates[each].filename().string();
        if (shown == "starter-3d") {
            shown = "Starter 3D";
        }
        RAWFRAME_TRY_ASSIGN(const ui::Node kCardName, box(kCard, ui::Layout{.shrink = 0, .margin = {0, 0, 4, 0}}, 0));
        RAWFRAME_TRY(strongWords(kCardName, shown, look::kInk, look::kBodySize));
        RAWFRAME_TRY_ASSIGN(const ui::Node kCardWhat, box(kCard, ui::Layout{.shrink = 0}, 0));
        RAWFRAME_TRY(tree_->setText(kCardWhat,
                                    templateWords(kTemplates[each]),
                                    ui::TextLook{.font = font_, .size = look::kLabelSize, .color = look::kLabelInk}));
        for (const ui::Node kPart : {kCardName, kCardWhat}) {
            RAWFRAME_TRY(tree_->setInteraction(kPart, ui::Interaction{.hits = ui::Interaction::Hits::Nothing}));
        }
        RAWFRAME_TRY(tree_->setRole(kCard, ui::Role::Button));
        RAWFRAME_TRY(tree_->setName(kCard, kTemplates[each].filename().string()));
        templateRows_.emplace_back(kCard, kTemplates[each]);
    }
    if (kTemplates.empty()) {
        RAWFRAME_TRY_ASSIGN(const ui::Node kNone, box(kNew, ui::Layout{.shrink = 0}, 0));
        RAWFRAME_TRY(
            words(kNone, "No templates beside Studio, in " + templates_.string(), look::kWarn, look::kLabelSize));
    }
    RAWFRAME_TRY(box(kNew, ui::Layout{.grow = 1}, 0));
    RAWFRAME_TRY_ASSIGN(const ui::Node kCreateLine,
                        box(kNew, ui::Layout{.alignItems = ui::Align::Center, .gap = 12, .shrink = 0}, 0));
    // What Studio says here, with no status bar on the start screen.
    RAWFRAME_TRY_ASSIGN(statusNode_, box(kCreateLine, ui::Layout{.width = ui::pixels(0), .grow = 1}, 0));
    RAWFRAME_TRY(tree_->setLook(statusNode_, ui::Look{.clip = true}));
    createNode_ = ui::Node{};
    if (!kTemplates.empty()) {
        RAWFRAME_TRY_ASSIGN(createNode_, textButton(kCreateLine, std::nullopt, "Create and open", ButtonKind::Primary));
    }
    say(kTemplates.empty() && recentRows_.empty() ? "open a game by its folder"
                                                  : "make a game from a template, or open one");
    return {};
}

void ShellParticipant::showRecent() {
    for (const auto& [kLine, kGame] : recentRows_) {
        static_cast<void>(tree_->remove(kLine));
    }
    recentRows_.clear();
    if (!tree_->contains(recentList_)) {
        return;
    }
    // The games opened last, the latest first, those still there and
    // named as the search has it.
    for (const std::string& kRecent : preferences_.recent) {
        std::error_code error;
        const std::filesystem::path kDescription{kRecent};
        if (!std::filesystem::is_regular_file(kDescription, error) ||
            !named(kDescription.parent_path().string() + "/" + kDescription.stem().string(), gameSearch_)) {
            continue;
        }
        const bool kFirst = recentRows_.empty();
        auto line = box(recentList_,
                        ui::Layout{.alignItems = ui::Align::Center,
                                   .gap = 14,
                                   .shrink = 0,
                                   .padding = {12, 14, 10, 10},
                                   .border = {1, 1, 1, 1}},
                        0);
        if (!line.has_value()) {
            return;
        }
        static_cast<void>(tree_->setLook(
            *line,
            ui::Look{.fill = kFirst ? 0x1B2440FFU : 0, .borderColor = kFirst ? look::kSelectedEdge : 0, .radius = 6}));
        static_cast<void>(tree_->setRole(*line, ui::Role::Button));
        static_cast<void>(tree_->setName(*line, kDescription.stem().string()));
        if (auto tile = box(*line,
                            ui::Layout{.width = ui::pixels(72),
                                       .height = ui::pixels(44),
                                       .justify = ui::Justify::Center,
                                       .alignItems = ui::Align::Center,
                                       .shrink = 0},
                            0);
            tile.has_value()) {
            static_cast<void>(tree_->setInteraction(*tile, ui::Interaction{.hits = ui::Interaction::Hits::Nothing}));
            // Its picture where Studio kept one (D927), else its mark.
            const std::uint64_t kKey = thumbnailKey("game:" + kDescription.lexically_normal().generic_string());
            if (!browser_.pictures.contains(kKey)) {
                if (const auto kPath = gamePicturePath(kDescription); kPath.has_value()) {
                    const std::optional<std::string> kBytes = authoring_session::readFile(*kPath);
                    std::optional<render::StreamedPicture> read =
                        kBytes.has_value()
                            ? render::streamedFrom(std::as_bytes(std::span{kBytes->data(), kBytes->size()}))
                            : std::nullopt;
                    if (read.has_value()) {
                        texture::Texture made{.format = texture::Format::Rgba8Srgb};
                        made.levels.push_back(texture::Level{
                            .width = read->width, .height = read->height, .bytes = std::move(read->pixels)});
                        browser_.pictures[kKey] = std::make_shared<const texture::Texture>(std::move(made));
                    }
                }
            }
            if (browser_.pictures.contains(kKey)) {
                static_cast<void>(tree_->setLook(*tile, ui::Look{.radius = 5, .image = kKey}));
            } else {
                static_cast<void>(tree_->setLook(*tile, ui::Look{.fill = look::kRaisedFill, .radius = 5}));
                static_cast<void>(icon(*tile, Icon::Cube, look::kEntityInk, 18));
            }
        }
        if (auto texts = box(
                *line, ui::Layout{.width = ui::pixels(0), .direction = ui::Direction::Column, .gap = 4, .grow = 1}, 0);
            texts.has_value()) {
            static_cast<void>(tree_->setInteraction(*texts, ui::Interaction{.hits = ui::Interaction::Hits::Nothing}));
            if (auto title = box(*texts, ui::Layout{.shrink = 0}, 0); title.has_value()) {
                std::string shown = kDescription.stem().string();
                if (!shown.empty()) {
                    shown[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(shown[0])));
                }
                static_cast<void>(strongWords(*title, shown, look::kInk, 14));
            }
            if (auto path = box(*texts, ui::Layout{.shrink = 1}, 0); path.has_value()) {
                static_cast<void>(monoWords(*path, kDescription.string(), look::kFaintInk, look::kSmallSize));
            }
        }
        if (auto age = box(*line, ui::Layout{.shrink = 0}, 0); age.has_value()) {
            static_cast<void>(tree_->setInteraction(*age, ui::Interaction{.hits = ui::Interaction::Hits::Nothing}));
            static_cast<void>(words(*age, ageOf(kDescription), look::kLabelInk, look::kLabelSize));
        }
        recentRows_.emplace_back(*line, kDescription);
    }
    // The sample games beside Studio, which its package brings (D920),
    // those not among the games opened.
    std::error_code beside;
    const std::filesystem::path kSamples =
        std::filesystem::canonical(process::ownExecutable(), beside).parent_path() / "games";
    std::vector<std::filesystem::path> samples;
    for (auto entry = std::filesystem::directory_iterator(kSamples, beside);
         !beside && entry != std::filesystem::directory_iterator();
         entry.increment(beside)) {
        const std::optional<std::filesystem::path> kDescription = descriptionIn(entry->path());
        if (kDescription.has_value() &&
            std::ranges::none_of(recentRows_,
                                 [&](const auto& row) {
                                     return row.second == *kDescription;
                                 }) &&
            named(kDescription->stem().string(), gameSearch_)) {
            samples.push_back(*kDescription);
        }
    }
    std::ranges::sort(samples);
    if (!samples.empty()) {
        if (auto heading = box(recentList_, ui::Layout{.shrink = 0, .padding = {2, 2, 14, 2}}, 0);
            heading.has_value()) {
            static_cast<void>(strongWords(*heading, "Samples beside Studio", look::kLabelInk, look::kLabelSize));
            recentRows_.emplace_back(*heading, std::filesystem::path{});
        }
    }
    for (const std::filesystem::path& kSample : samples) {
        auto line = box(recentList_,
                        ui::Layout{.alignItems = ui::Align::Center, .gap = 14, .shrink = 0, .padding = {12, 14, 8, 8}},
                        0);
        if (!line.has_value()) {
            return;
        }
        static_cast<void>(tree_->setLook(*line, ui::Look{.radius = 6}));
        static_cast<void>(tree_->setRole(*line, ui::Role::Button));
        static_cast<void>(tree_->setName(*line, kSample.stem().string()));
        if (auto tile = box(*line,
                            ui::Layout{.width = ui::pixels(72),
                                       .height = ui::pixels(36),
                                       .justify = ui::Justify::Center,
                                       .alignItems = ui::Align::Center,
                                       .shrink = 0},
                            0);
            tile.has_value()) {
            static_cast<void>(tree_->setLook(*tile, ui::Look{.fill = look::kRaisedFill, .radius = 5}));
            static_cast<void>(tree_->setInteraction(*tile, ui::Interaction{.hits = ui::Interaction::Hits::Nothing}));
            static_cast<void>(icon(*tile, Icon::Folder, look::kLabelInk, 16));
        }
        if (auto title = box(*line, ui::Layout{.shrink = 0}, 0); title.has_value()) {
            static_cast<void>(tree_->setInteraction(*title, ui::Interaction{.hits = ui::Interaction::Hits::Nothing}));
            std::string shown = kSample.stem().string();
            shown[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(shown[0])));
            static_cast<void>(strongWords(*title, shown, look::kSoftInk, look::kBodySize));
        }
        recentRows_.emplace_back(*line, kSample);
    }
    if (recentRows_.empty()) {
        if (auto none = box(recentList_, ui::Layout{.shrink = 0, .padding = {4, 4, 10, 0}}, 0); none.has_value()) {
            static_cast<void>(words(*none,
                                    gameSearch_.empty()
                                        ? "No games opened yet. Make one on the right, or open a folder."
                                        : "No game opened before is named so; Enter opens a path.",
                                    look::kLabelInk,
                                    look::kBodySize));
            recentRows_.emplace_back(*none, std::filesystem::path{});
        }
    }
}

void ShellParticipant::pressStart(ui::Node node) {
    if (const auto kTemplate =
            std::ranges::find(templateRows_, node, &std::pair<ui::Node, std::filesystem::path>::first);
        kTemplate != templateRows_.end()) {
        endEdit();
        templateAt_ = static_cast<std::size_t>(kTemplate - templateRows_.begin());
        for (std::size_t each = 0; each < templateRows_.size(); ++each) {
            const bool kOn = each == templateAt_;
            static_cast<void>(tree_->setLook(templateRows_[each].first,
                                             ui::Look{.fill = kOn ? 0x1B2440FFU : look::kPanelFill,
                                                      .borderColor = kOn ? look::kAccent : look::kEdge,
                                                      .radius = 6}));
        }
        say("new games from " + kTemplate->second.filename().string());
    } else if (const auto kRecent =
                   std::ranges::find(recentRows_, node, &std::pair<ui::Node, std::filesystem::path>::first);
               kRecent != recentRows_.end() && !kRecent->second.empty()) {
        endEdit();
        chooseGame(kRecent->second);
    } else if (newGameField_.has_value() && node == newGameField_->value) {
        beginEdit(*newGameField_);
    } else if (gameFolderField_.has_value() && node == gameFolderField_->value) {
        beginEdit(*gameFolderField_);
    } else if (openGameField_.has_value() && node == openGameField_->value) {
        beginEdit(*openGameField_);
    } else if (node == openFolderNode_ && openFolderNode_ != ui::Node{}) {
        endEdit();
        askDialog(view::FileDialogs::Kind::Folder, "Open a game's folder", "open");
    } else if (node == chooseFolderNode_ && chooseFolderNode_ != ui::Node{}) {
        endEdit();
        askDialog(view::FileDialogs::Kind::Folder, "Where new games go", "folder");
    } else if (node == createNode_ && createNode_ != ui::Node{} && newGameField_.has_value()) {
        const std::string kName{tree_->textOf(newGameField_->value)};
        endEdit();
        makeGame(kName);
    } else {
        endEdit();
    }
}

void ShellParticipant::askDialog(view::FileDialogs::Kind kind, std::string title, std::string forWhat) {
    if (dialogs_ == nullptr) {
        say("this window shows no file dialogs; type the path instead");
        return;
    }
    const std::uint64_t kTicket = ++dialogTickets_;
    dialogsAsked_.emplace_back(kTicket, std::move(forWhat));
    std::string folder = gameOpen() ? description_.parent_path().string() : projects_.string();
    std::error_code error;
    if (!std::filesystem::is_directory(folder, error)) {
        folder.clear();
    }
    dialogs_->ask(
        view::FileDialogs::Asked{.ticket = kTicket, .kind = kind, .title = std::move(title), .folder = folder});
}

void ShellParticipant::takeDialogs() {
    // A paste's text, typed into the field it was asked for while that
    // field still holds the keyboard, as typing is (D999).
    while (std::optional<view::Clipboard::Read> read =
               held_.clipboard != nullptr ? held_.clipboard->take() : std::nullopt) {
        if (read->ticket != held_.pasteTicket || read->unsupported || edit_ == nullptr ||
            edit_->node() != held_.pasteInto) {
            continue;
        }
        std::erase(read->text, '\r');
        take(view::Typing{.kind = view::Typing::Kind::Text, .text = std::move(read->text)});
        held_.pasteTicket = 0;
    }
    if (dialogs_ == nullptr) {
        return;
    }
    while (std::optional<view::FileDialogs::Answer> answer = dialogs_->take()) {
        const auto kAsked =
            std::ranges::find(dialogsAsked_, answer->ticket, &std::pair<std::uint64_t, std::string>::first);
        if (kAsked == dialogsAsked_.end()) {
            continue;
        }
        const std::string kFor = kAsked->second;
        dialogsAsked_.erase(kAsked);
        if (answer->unsupported) {
            say("this platform shows no file dialogs; type the path instead");
            continue;
        }
        if (answer->paths.empty()) {
            continue;
        }
        if (kFor == "open") {
            openFolder(answer->paths.front());
        } else if (kFor == "folder" && gameFolderField_.has_value()) {
            projects_ = answer->paths.front();
            static_cast<void>(words(gameFolderField_->value, projects_.string(), kText, look::kBodySize));
        } else if (kFor == "import") {
            for (const std::string& kPath : answer->paths) {
                const std::string kName = std::filesystem::path{kPath}.filename().generic_string();
                static_cast<void>(
                    importFile(kPath, browser_.folder.has_value() ? *browser_.folder + "/" + kName : kName));
            }
            static_cast<void>(showScenes());
        }
    }
}

void ShellParticipant::openFolder(const std::filesystem::path& folder) {
    const std::optional<std::filesystem::path> kDescription = descriptionIn(folder);
    if (!kDescription.has_value()) {
        say(folder.string() + " holds no game description (.game)");
        return;
    }
    chooseGame(*kDescription);
}

void ShellParticipant::closeGame() {
    // Back to the start screen (D923): the game played stopped, its
    // session let go, nothing of it left chosen.
    if (playing_.has_value()) {
        stopPlaying();
    }
    previewing_ = false;
    preview_.reset();
    cooking_.reset();
    live_.on = false;
    tails_.clear();
    session_.reset();
    broken_.reset();
    diagnostic_.reset();
    scene_.clear();
    scenes_.clear();
    listed_.clear();
    openDocuments_.clear();
    entity_.clear();
    selected_.clear();
    browser_.chosen.clear();
    description_.clear();
    editOpened_ = false;
    editCooked_ = false;
    rebuild();
    say("closed the game");
}

void ShellParticipant::chooseGame(const std::filesystem::path& description) {
    std::error_code error;
    if (description.extension() != ".game" || !std::filesystem::is_regular_file(description, error)) {
        say(description.string() + " is not a game description");
        return;
    }
    // Another game open is closed first, its game stopped.
    if (gameOpen() && held_.joined == nullptr) {
        closeGame();
    }
    // Each game opens on its first scene and cooks once (D912, D918), and
    // keeps a picture of itself once (D927).
    held_.gamePictured = false;
    editOpened_ = false;
    editCooked_ = false;
    settle(description, description.parent_path());
    rebuild();
    say(broken_.has_value() ? *broken_ : "opened " + description.filename().string());
}

void ShellParticipant::makeGame(const std::string& name) {
    if (templateAt_ >= templateRows_.size()) {
        return;
    }
    // The folder as its field has it now.
    if (gameFolderField_.has_value() && tree_->contains(gameFolderField_->value)) {
        projects_ = std::string{tree_->textOf(gameFolderField_->value)};
    }
    std::error_code error;
    std::filesystem::create_directories(projects_, error);
    const auto kMade = authoring_session::newGame(templateRows_[templateAt_].second, projects_ / name);
    if (!kMade.has_value()) {
        ++refused_;
        say(std::string{kMade.error().description()});
        return;
    }
    ++made_;
    chooseGame(kMade->description);
}

void ShellParticipant::rebuild() {
    endEdit();
    // The panes shown apart go back with the tree they were in (D988).
    closeAsides();
    // The dialogs went with the tree they were in.
    dialog_.reset();
    held_.exportDialog.reset();
    // The window's access goes with the tree it read (D575).
    if (seat_ != nullptr) {
        seat_->leave();
    }
    // The dock's panes not shown are not under the root.
    for (const ui::Node kPane : {scenesColumn_,
                                 historyColumn_,
                                 consoleColumn_,
                                 networkColumn_,
                                 held_.environmentColumn,
                                 held_.profilerColumn,
                                 held_.frameColumn,
                                 held_.changesColumn,
                                 held_.mixerColumn,
                                 held_.curvesColumn,
                                 held_.panelsColumn,
                                 componentsColumn_}) {
        if (tree_->contains(kPane)) {
            static_cast<void>(tree_->remove(kPane));
        }
    }
    static_cast<void>(tree_->remove(root_));
    if (tree_->contains(held_.shelf)) {
        static_cast<void>(tree_->remove(held_.shelf));
    }
    static_cast<void>(build());
    reopenAsides();
    if (seat_ != nullptr) {
        static_cast<void>(seat_->seat(*tree_, root_));
    }
}

std::optional<std::filesystem::path> ShellParticipant::gamePicturePath(const std::filesystem::path& description) const {
    if (preferencesFile_.empty()) {
        return std::nullopt;
    }
    std::array<char, 24> name{};
    std::snprintf(name.data(),
                  name.size(),
                  "%016llx",
                  static_cast<unsigned long long>(thumbnailKey(description.lexically_normal().generic_string())));
    return preferencesFile_.parent_path() / "pictures" / (std::string{name.data()} + ".picture");
}

void ShellParticipant::keepGamePicture(const texture::Texture& picture) {
    const auto kPath = gamePicturePath(description_);
    if (!kPath.has_value() || picture.levels.empty()) {
        return;
    }
    // A tile's 144 by 88, from the picture's middle at its aspect, each
    // pixel the mean of those it covers.
    const texture::Level& kFrom = picture.levels[0];
    constexpr std::uint32_t kWide = 144;
    constexpr std::uint32_t kTall = 88;
    if (kFrom.width < kWide || kFrom.height < kTall ||
        kFrom.bytes.size() != std::size_t{kFrom.width} * kFrom.height * 4) {
        return;
    }
    const double kScale = std::min(static_cast<double>(kFrom.width) / kWide, static_cast<double>(kFrom.height) / kTall);
    const double kLeft = (kFrom.width - kWide * kScale) / 2;
    const double kTop = (kFrom.height - kTall * kScale) / 2;
    render::StreamedPicture made{.sequence = 1, .width = kWide, .height = kTall, .pixels = {}};
    made.pixels.resize(std::size_t{kWide} * kTall * 4);
    for (std::uint32_t y = 0; y < kTall; ++y) {
        for (std::uint32_t x = 0; x < kWide; ++x) {
            const auto kX0 = static_cast<std::uint32_t>(kLeft + x * kScale);
            const auto kY0 = static_cast<std::uint32_t>(kTop + y * kScale);
            const auto kX1 =
                std::max(kX0 + 1, std::min(kFrom.width, static_cast<std::uint32_t>(kLeft + (x + 1) * kScale)));
            const auto kY1 =
                std::max(kY0 + 1, std::min(kFrom.height, static_cast<std::uint32_t>(kTop + (y + 1) * kScale)));
            std::array<std::uint32_t, 4> sum{};
            std::uint32_t count = 0;
            for (std::uint32_t sy = kY0; sy < kY1; ++sy) {
                for (std::uint32_t sx = kX0; sx < kX1; ++sx) {
                    const std::size_t kAt = (std::size_t{sy} * kFrom.width + sx) * 4;
                    for (std::size_t channel = 0; channel < 4; ++channel) {
                        sum[channel] += std::to_integer<std::uint32_t>(kFrom.bytes[kAt + channel]);
                    }
                    ++count;
                }
            }
            for (std::size_t channel = 0; channel < 4; ++channel) {
                made.pixels[(std::size_t{y} * kWide + x) * 4 + channel] = static_cast<std::byte>(sum[channel] / count);
            }
        }
    }
    std::error_code error;
    std::filesystem::create_directories(kPath->parent_path(), error);
    const std::vector<std::byte> kBytes = render::streamedOf(made);
    std::ofstream file{*kPath, std::ios::binary | std::ios::trunc};
    file.write(reinterpret_cast<const char*>(kBytes.data()), static_cast<std::streamsize>(kBytes.size()));
    if (!file) {
        return;
    }
    // Shown afresh when the start screen is next.
    browser_.pictures.erase(thumbnailKey("game:" + description_.lexically_normal().generic_string()));
}

} // namespace rawframe::studio
