#include "shell.h"

namespace rawframe::studio {

namespace {

/// The most lines the Console keeps, and the most it shows: the newest.
constexpr std::size_t kMostKept = 1000;
constexpr std::size_t kMostShown = 200;

constexpr std::uint32_t kWarningText = 0xE0B84CFFU;
constexpr std::uint32_t kErrorText = 0xE06C6CFFU;

constexpr std::string_view kSources[] = {"Studio", "Server", "Client"};

std::string_view sourceName(ConsoleLine::Source source) {
    return kSources[static_cast<std::size_t>(source)];
}

} // namespace

void ShellParticipant::addConsoleLine(ConsoleLine line) {
    ++consoleLines_;
    console_.push_back(std::move(line));
    if (console_.size() > kMostKept) {
        console_.pop_front();
    }
    if (dockShown_ == Dock::Console) {
        showConsole();
    }
}

void ShellParticipant::readLogs(double seconds) {
    if (tails_.empty() || seconds < nextLogRead_) {
        return;
    }
    nextLogRead_ = seconds + 0.5;
    std::size_t added = 0;
    for (LogTail& tail : tails_) {
        for (ConsoleLine& line : tail.read()) {
            ++consoleLines_;
            console_.push_back(std::move(line));
            ++added;
        }
    }
    while (console_.size() > kMostKept) {
        console_.pop_front();
    }
    // Read once more after the game ends, for what it wrote as it stopped.
    if (!playing_.has_value() && !stopping_.has_value()) {
        tails_.clear();
    }
    if (added > 0 && dockShown_ == Dock::Console) {
        showConsole();
    }
}

result::Status ShellParticipant::makeConsole() {
    RAWFRAME_TRY_ASSIGN(
        consoleColumn_,
        box(std::nullopt,
            ui::Layout{.minHeight = ui::pixels(0), .direction = ui::Direction::Column, .gap = 4, .grow = 1},
            kPanel));
    RAWFRAME_TRY(tree_->setRole(consoleColumn_, ui::Role::Group));
    RAWFRAME_TRY(tree_->setName(consoleColumn_, "Console"));
    // Its filters, each lit while it lets lines through: a source's lines
    // shown, and warnings and errors alone.
    RAWFRAME_TRY_ASSIGN(const ui::Node kBar,
                        box(consoleColumn_, ui::Layout{.height = ui::pixels(26), .gap = 4, .shrink = 0}, 0));
    consoleFilters_.clear();
    for (int each = 0; each < 4; ++each) {
        RAWFRAME_TRY_ASSIGN(const ui::Node kFilter,
                            box(kBar, ui::Layout{.shrink = 0, .padding = {10, 4, 10, 4}}, kRow));
        RAWFRAME_TRY(pressWords(kFilter, each < 3 ? kSources[each] : std::string_view{"Problems only"}, kText, 13));
        consoleFilters_.emplace_back(kFilter, each);
    }
    RAWFRAME_TRY_ASSIGN(consoleList_,
                        box(consoleColumn_,
                            ui::Layout{.minHeight = ui::pixels(0),
                                       .direction = ui::Direction::Column,
                                       .gap = 4,
                                       .grow = 1,
                                       .scroll = ui::Scroll::Vertical},
                            kPanel));
    RAWFRAME_TRY(tree_->setRole(consoleList_, ui::Role::ScrollView));
    RAWFRAME_TRY(tree_->setName(consoleList_, "Console lines"));
    consoleRows_.clear();
    return {};
}

void ShellParticipant::showConsole() {
    for (const auto& [kFilter, kEach] : consoleFilters_) {
        const bool kOn = kEach < 3 ? consoleSources_[static_cast<std::size_t>(kEach)] : consoleProblems_;
        static_cast<void>(tree_->setLook(kFilter, ui::Look{.fill = kOn ? kChosen : kRow, .radius = 4}));
    }
    for (const auto& [kRowNode, kAt] : consoleRows_) {
        static_cast<void>(tree_->remove(kRowNode));
    }
    consoleRows_.clear();
    std::vector<std::size_t> passing;
    for (std::size_t at = console_.size(); at > 0 && passing.size() < kMostShown; --at) {
        const ConsoleLine& kLine = console_[at - 1];
        if (consoleSources_[static_cast<std::size_t>(kLine.source)] &&
            (!consoleProblems_ || kLine.level != ConsoleLine::Level::Info)) {
            passing.push_back(at - 1);
        }
    }
    for (auto each = passing.rbegin(); each != passing.rend(); ++each) {
        const ConsoleLine& kLine = console_[*each];
        const std::uint32_t kColor = kLine.level == ConsoleLine::Level::Error     ? kErrorText
                                     : kLine.level == ConsoleLine::Level::Warning ? kWarningText
                                                                                  : kText;
        // A line naming a place in the game's own files is pressed to open
        // it; the rest are read.
        auto line = row(consoleList_,
                        std::string{sourceName(kLine.source)} + "  " + kLine.text,
                        kColor,
                        kLine.at.has_value() ? kRow : kPanel,
                        kLine.at.has_value());
        if (!line.has_value()) {
            return;
        }
        consoleRows_.emplace_back(*line, *each);
    }
    // The newest in sight.
    static_cast<void>(tree_->scrollTo(consoleList_, 0, 1.0e9F));
}

} // namespace rawframe::studio
