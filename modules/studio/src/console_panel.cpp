#include "shell.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdio>

namespace rawframe::studio {

namespace {

/// The most lines the Console keeps, and the most it shows: the newest.
constexpr std::size_t kMostKept = 1000;
constexpr std::size_t kMostShown = 200;

constexpr std::string_view kSources[] = {"Studio", "Server", "Client"};

std::string_view sourceName(ConsoleLine::Source source) {
    return kSources[static_cast<std::size_t>(source)];
}

} // namespace

void ShellParticipant::addConsoleLine(ConsoleLine line) {
    ++consoleLines_;
    if (line.wall == 0) {
        line.wall =
            std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::system_clock::now().time_since_epoch())
                .count();
    }
    if (consoleEpoch_ == 0) {
        consoleEpoch_ = line.wall;
    }
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
            if (consoleEpoch_ == 0) {
                consoleEpoch_ = line.wall;
            }
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
        box(std::nullopt, ui::Layout{.minHeight = ui::pixels(0), .direction = ui::Direction::Column, .grow = 1}, 0));
    RAWFRAME_TRY(tree_->setRole(consoleColumn_, ui::Role::Group));
    RAWFRAME_TRY(tree_->setName(consoleColumn_, "Console"));
    // As the design's Console (D923): the levels and the sources as chips,
    // each lit while it lets lines through, a filter, and Clear.
    RAWFRAME_TRY_ASSIGN(const ui::Node kBar,
                        box(consoleColumn_,
                            ui::Layout{.alignItems = ui::Align::Center,
                                       .gap = 8,
                                       .shrink = 0,
                                       .padding = {10, 10, 7, 7},
                                       .border = {0, 0, 0, 1}},
                            0));
    RAWFRAME_TRY(tree_->setLook(kBar, ui::Look{.borderColor = look::kLine}));
    RAWFRAME_TRY_ASSIGN(consoleChips_,
                        box(kBar, ui::Layout{.alignItems = ui::Align::Center, .gap = 6, .shrink = 0}, 0));
    RAWFRAME_TRY_ASSIGN(const auto kSearch, searchWell(kBar, "Filter the Console", "Filter", "", 170, Icon::Search));
    consoleSearchValue_ = kSearch.second;
    RAWFRAME_TRY_ASSIGN(consoleClear_, textButton(kBar, std::nullopt, "Clear", ButtonKind::Quiet));
    RAWFRAME_TRY_ASSIGN(consoleList_,
                        box(consoleColumn_,
                            ui::Layout{.minHeight = ui::pixels(0),
                                       .direction = ui::Direction::Column,
                                       .grow = 1,
                                       .scroll = ui::Scroll::Vertical},
                            0));
    RAWFRAME_TRY(tree_->setRole(consoleList_, ui::Role::ScrollView));
    RAWFRAME_TRY(tree_->setName(consoleList_, "Console lines"));
    consoleRows_.clear();
    return {};
}

namespace {

/// When a line was said, from the Console's first: minutes, seconds, and
/// hundredths, hours before them past the first hour.
std::string sinceFirst(std::int64_t wall, std::int64_t epoch) {
    if (wall == 0 || epoch == 0 || wall < epoch) {
        return "--:--.--";
    }
    const std::int64_t kHundredths = (wall - epoch) / 10'000'000;
    const std::int64_t kSeconds = kHundredths / 100;
    std::array<char, 24> text{};
    if (kSeconds >= 3600) {
        std::snprintf(text.data(),
                      text.size(),
                      "%lld:%02lld:%02lld",
                      static_cast<long long>(kSeconds / 3600),
                      static_cast<long long>(kSeconds / 60 % 60),
                      static_cast<long long>(kSeconds % 60));
    } else {
        std::snprintf(text.data(),
                      text.size(),
                      "%02lld:%02lld.%02lld",
                      static_cast<long long>(kSeconds / 60),
                      static_cast<long long>(kSeconds % 60),
                      static_cast<long long>(kHundredths % 100));
    }
    return text.data();
}

/// Whether `text` holds `typed`, letters of either case alike.
bool holds(std::string_view text, std::string_view typed) {
    const auto kLower = [](char each) {
        return static_cast<char>(std::tolower(static_cast<unsigned char>(each)));
    };
    return !std::ranges::search(text, typed, {}, kLower, kLower).empty();
}

} // namespace

void ShellParticipant::showConsole() {
    // The chips again, each with how many lines it stands for.
    for (const auto& [kFilter, kEach] : consoleFilters_) {
        static_cast<void>(tree_->remove(kFilter));
    }
    consoleFilters_.clear();
    std::array<std::size_t, 3> levels{};
    std::array<std::size_t, 3> sources{};
    for (const ConsoleLine& kLine : console_) {
        ++levels[static_cast<std::size_t>(kLine.level)];
        ++sources[static_cast<std::size_t>(kLine.source)];
    }
    const auto kChip = [this](std::string text, bool on, int each) {
        auto made = box(
            consoleChips_,
            ui::Layout{.alignItems = ui::Align::Center, .shrink = 0, .padding = {9, 9, 4, 4}, .border = {1, 1, 1, 1}},
            0);
        if (!made.has_value()) {
            return;
        }
        static_cast<void>(tree_->setLook(*made,
                                         ui::Look{.fill = on ? look::kSelected : look::kRaisedFill,
                                                  .borderColor = on ? look::kSelectedEdge : look::kRaisedEdge,
                                                  .radius = 5}));
        static_cast<void>(pressWords(*made, text, on ? look::kInk : look::kFaintInk, look::kLabelSize));
        consoleFilters_.emplace_back(*made, each);
    };
    constexpr std::array<std::string_view, 3> kLevelNames = {"Info", "Warnings", "Errors"};
    for (const int kLevel : {2, 1, 0}) {
        const auto kAt = static_cast<std::size_t>(kLevel);
        kChip(std::string{kLevelNames[kAt]} + " " + std::to_string(levels[kAt]), consoleLevels_[kAt], 3 + kLevel);
    }
    for (const int kSource : {0, 1, 2}) {
        const auto kAt = static_cast<std::size_t>(kSource);
        kChip(sources[kAt] > 0 ? std::string{kSources[kAt]} + " " + std::to_string(sources[kAt])
                               : std::string{kSources[kAt]},
              consoleSources_[kAt],
              kSource);
    }
    for (const auto& [kRowNode, kAt] : consoleRows_) {
        static_cast<void>(tree_->remove(kRowNode));
    }
    consoleRows_.clear();
    std::vector<std::size_t> passing;
    for (std::size_t at = console_.size(); at > 0 && passing.size() < kMostShown; --at) {
        const ConsoleLine& kLine = console_[at - 1];
        if (consoleSources_[static_cast<std::size_t>(kLine.source)] &&
            consoleLevels_[static_cast<std::size_t>(kLine.level)] &&
            (consoleSearch_.empty() || holds(kLine.text, consoleSearch_) || holds(kLine.domain, consoleSearch_))) {
            passing.push_back(at - 1);
        }
    }
    if (passing.empty()) {
        auto none = box(consoleList_, ui::Layout{.shrink = 0, .padding = {14, 14, 12, 12}}, 0);
        if (none.has_value()) {
            static_cast<void>(words(*none,
                                    console_.empty() ? "Nothing said yet. Lines from Studio, and from the server and "
                                                       "the client while the game plays, show here."
                                                     : "No line passes the filters.",
                                    look::kFaintInk,
                                    look::kBodySize));
            consoleRows_.emplace_back(*none, console_.size());
        }
        return;
    }
    for (auto each = passing.rbegin(); each != passing.rend(); ++each) {
        const ConsoleLine& kLine = console_[*each];
        const bool kFailed = kLine.level == ConsoleLine::Level::Error;
        const bool kWarned = kLine.level == ConsoleLine::Level::Warning;
        // A row as the design's: the level's mark, when, who, what, and
        // where; an error's row tinted, a line naming a place in the
        // game's own files pressed to open it there.
        auto line = box(consoleList_,
                        ui::Layout{.alignItems = ui::Align::Center,
                                   .gap = 12,
                                   .shrink = 0,
                                   .padding = {10, 10, 5, 5},
                                   .border = {0, 0, 0, 1}},
                        0);
        if (!line.has_value()) {
            return;
        }
        static_cast<void>(
            tree_->setLook(*line, ui::Look{.fill = kFailed ? 0x3A1D2266U : 0U, .borderColor = look::kLine}));
        if (kFailed || kWarned) {
            static_cast<void>(
                icon(*line, kFailed ? Icon::Error : Icon::Warning, kFailed ? look::kError : look::kWarn, 13));
        } else {
            auto dot =
                box(*line,
                    ui::Layout{.width = ui::pixels(13), .height = ui::pixels(13), .shrink = 0, .padding = {4, 4, 4, 4}},
                    0);
            if (dot.has_value()) {
                auto inner = box(*dot, ui::Layout{.width = ui::pixels(5), .height = ui::pixels(5), .shrink = 0}, 0);
                if (inner.has_value()) {
                    static_cast<void>(tree_->setLook(*inner, ui::Look{.fill = look::kOffInk, .radius = 3}));
                }
            }
        }
        const auto kCell = [&](float width, std::string_view text, std::uint32_t ink, bool mono) {
            auto cell = box(*line,
                            ui::Layout{.width = width > 0 ? ui::pixels(width) : ui::pixels(0),
                                       .grow = width > 0 ? 0.0F : 1.0F,
                                       .shrink = 0},
                            0);
            if (cell.has_value()) {
                static_cast<void>(mono ? monoWords(*cell, text, ink, look::kSmallSize)
                                       : words(*cell, text, ink, look::kBodySize));
                static_cast<void>(
                    tree_->setInteraction(*cell, ui::Interaction{.hits = ui::Interaction::Hits::Nothing}));
            }
        };
        kCell(68, sinceFirst(kLine.wall, consoleEpoch_), look::kFaintInk, true);
        kCell(52, sourceName(kLine.source), look::kFaintInk, true);
        kCell(0, kLine.text, kFailed ? look::kInk : kWarned ? look::kSoftInk : look::kRowInk, false);
        const std::string kWhere = kLine.at.has_value() ? std::filesystem::path{kLine.at->file}.filename().string() +
                                                              ":" + std::to_string(kLine.at->line)
                                                        : kLine.domain;
        if (!kWhere.empty()) {
            auto where = box(*line, ui::Layout{.shrink = 0}, 0);
            if (where.has_value()) {
                static_cast<void>(
                    monoWords(*where, kWhere, kLine.at.has_value() ? look::kAccent : 0x5B9BFFAAU, look::kSmallSize));
                static_cast<void>(
                    tree_->setInteraction(*where, ui::Interaction{.hits = ui::Interaction::Hits::Nothing}));
            }
        }
        if (kLine.at.has_value()) {
            static_cast<void>(tree_->setRole(*line, ui::Role::Button));
            static_cast<void>(tree_->setName(*line, kLine.text));
        }
        consoleRows_.emplace_back(*line, *each);
    }
    // The newest in sight.
    static_cast<void>(tree_->scrollTo(consoleList_, 0, 1.0e9F));
}

} // namespace rawframe::studio
