#include "rawframe/authoring_session/new_game.h"
#include "shell.h"

#include <algorithm>

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

result::Status ShellParticipant::showStart(ui::Node body) {
    templateRows_.clear();
    recentRows_.clear();
    newGameField_.reset();
    openGameField_.reset();
    RAWFRAME_TRY_ASSIGN(const ui::Node kStart,
                        box(body,
                            ui::Layout{.direction = ui::Direction::Row,
                                       .gap = 6,
                                       .grow = 1,
                                       .padding = {16, 16, 16, 16},
                                       .scroll = ui::Scroll::Vertical},
                            kBackground));
    RAWFRAME_TRY(tree_->setName(kStart, "Start"));
    // A new game from a template, the same as `rawframe-author new` makes
    // (D780): the templates, the chosen one marked, then its name.
    RAWFRAME_TRY_ASSIGN(const ui::Node kNew, column(kStart, "New game"));
    const std::vector<std::filesystem::path> kTemplates = templatesIn(templates_);
    templateAt_ = std::min(templateAt_, kTemplates.empty() ? 0 : kTemplates.size() - 1);
    for (std::size_t each = 0; each < kTemplates.size(); ++each) {
        RAWFRAME_TRY_ASSIGN(
            const ui::Node kLine,
            row(kNew, kTemplates[each].filename().string(), kText, each == templateAt_ ? kChosen : kRow, true));
        templateRows_.emplace_back(kLine, kTemplates[each]);
    }
    if (kTemplates.empty()) {
        static_cast<void>(row(kNew, "no templates in " + templates_.string(), kQuiet, kPanel));
    } else {
        auto line = fieldRow("new game", "", kNew);
        if (line.has_value()) {
            newGameField_ = FieldRow{.value = line->second, .role = FieldRow::Role::NewGame, .field = "new game"};
        }
        static_cast<void>(row(kNew, "in " + projects_.string(), kQuiet, kPanel));
    }
    // The games opened last, the latest first, those still there; then
    // any other by its description's path.
    RAWFRAME_TRY_ASSIGN(const ui::Node kOpen, column(kStart, "Open"));
    for (const std::string& kRecent : preferences_.recent) {
        std::error_code error;
        const std::filesystem::path kDescription{kRecent};
        if (!std::filesystem::is_regular_file(kDescription, error)) {
            continue;
        }
        RAWFRAME_TRY_ASSIGN(const ui::Node kLine,
                            row(kOpen,
                                kDescription.filename().string() + "  " + kDescription.parent_path().string(),
                                kText,
                                kRow,
                                true));
        recentRows_.emplace_back(kLine, kDescription);
    }
    auto line = fieldRow("open", "", kOpen);
    if (line.has_value()) {
        openGameField_ = FieldRow{.value = line->second, .role = FieldRow::Role::OpenGame, .field = "open"};
    }
    say(kTemplates.empty() && recentRows_.empty() ? "open a game by its description's path"
                                                  : "make a game from a template, or open one");
    return {};
}

void ShellParticipant::pressStart(ui::Node node) {
    if (const auto kTemplate =
            std::ranges::find(templateRows_, node, &std::pair<ui::Node, std::filesystem::path>::first);
        kTemplate != templateRows_.end()) {
        endEdit();
        templateAt_ = static_cast<std::size_t>(kTemplate - templateRows_.begin());
        for (std::size_t each = 0; each < templateRows_.size(); ++each) {
            static_cast<void>(tree_->setLook(templateRows_[each].first,
                                             ui::Look{.fill = each == templateAt_ ? kChosen : kRow, .radius = 3}));
        }
        say("new games from " + kTemplate->second.filename().string());
    } else if (const auto kRecent =
                   std::ranges::find(recentRows_, node, &std::pair<ui::Node, std::filesystem::path>::first);
               kRecent != recentRows_.end()) {
        endEdit();
        chooseGame(kRecent->second);
    } else if (newGameField_.has_value() && node == newGameField_->value) {
        beginEdit(*newGameField_);
    } else if (openGameField_.has_value() && node == openGameField_->value) {
        beginEdit(*openGameField_);
    } else {
        endEdit();
    }
}

void ShellParticipant::chooseGame(const std::filesystem::path& description) {
    std::error_code error;
    if (description.extension() != ".game" || !std::filesystem::is_regular_file(description, error)) {
        say(description.string() + " is not a game description");
        return;
    }
    settle(description, description.parent_path());
    rebuild();
    say(broken_.has_value() ? *broken_ : "opened " + description.filename().string());
}

void ShellParticipant::makeGame(const std::string& name) {
    if (templateAt_ >= templateRows_.size()) {
        return;
    }
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
                                 held_.changesColumn}) {
        if (tree_->contains(kPane)) {
            static_cast<void>(tree_->remove(kPane));
        }
    }
    static_cast<void>(tree_->remove(root_));
    static_cast<void>(build());
    if (seat_ != nullptr) {
        static_cast<void>(seat_->seat(*tree_, root_));
    }
}

} // namespace rawframe::studio
