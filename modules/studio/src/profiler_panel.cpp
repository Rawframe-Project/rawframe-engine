#include "shell.h"

#include <algorithm>
#include <cstdio>

namespace rawframe::studio {

namespace {

constexpr float kBarWidth = 4;
constexpr float kStripTall = 64;
constexpr std::size_t kSystemsShown = 12;
constexpr std::uint32_t kBarFill = 0x7FA7D9FFU;
constexpr std::uint32_t kOverFill = 0xE06C6CFFU;

std::string milliseconds(std::int64_t microseconds) {
    char made[32];
    std::snprintf(made, sizeof made, "%.2f ms", static_cast<double>(microseconds) / 1000.0);
    return made;
}

} // namespace

result::Status ShellParticipant::makeProfiler() {
    RAWFRAME_TRY_ASSIGN(held_.profilerColumn, pane("Profiler"));
    showProfiler({});
    return {};
}

void ShellParticipant::readProfiler(double seconds) {
    if (!playing_.has_value() || scene_.empty() || dockShown_ != Dock::Profiler || seconds < held_.profilerNextRead) {
        return;
    }
    held_.profilerNextRead = seconds + 1;
    const auto kParsed = document::parse(ask(profileRecord(next(), scene_)));
    const Value* kAnswer = kParsed.has_value() ? kParsed->find("answer") : nullptr;
    showProfiler(kAnswer != nullptr ? std::optional<Value>{*kAnswer} : std::nullopt);
}

void ShellParticipant::showProfiler(const std::optional<Value>& profile) {
    for (const ui::Node kLeft : held_.profilerRows) {
        static_cast<void>(tree_->remove(kLeft));
    }
    held_.profilerRows.clear();
    const auto kLine = [this](const std::string& words, std::uint32_t color) {
        if (auto made = row(held_.profilerColumn, words, color, kPanel); made.has_value()) {
            held_.profilerRows.push_back(*made);
        }
    };
    const Value* kTicks = profile.has_value() ? profile->find("ticksUs") : nullptr;
    const Value* kSystems = profile.has_value() ? profile->find("systems") : nullptr;
    if (kTicks == nullptr || kTicks->kind() != Value::Kind::Array || kTicks->items().empty()) {
        kLine(playing_.has_value() ? "reading the game's server" : "play the game to see what its ticks take", kQuiet);
        return;
    }
    // The ticks' spread, then each as a bar against the tick's own time,
    // red past it: a tick longer than its share of a second is late.
    std::vector<std::int64_t> ticks;
    for (const Value& kTick : kTicks->items()) {
        ticks.push_back(kTick.integer().value_or(0));
    }
    std::vector<std::int64_t> sorted = ticks;
    std::ranges::sort(sorted);
    const Value* kShare = profile->find("tickUs");
    const std::int64_t kBudget = kShare != nullptr && kShare->integer().value_or(0) > 0 ? *kShare->integer() : 16'667;
    const std::int64_t kMost = sorted.back();
    kLine("server ticks: half within " + milliseconds(sorted[sorted.size() / 2]) + ", longest " + milliseconds(kMost) +
              ", over the last " + std::to_string(ticks.size()) + " (each has " + milliseconds(kBudget) + ")",
          kText);
    auto strip = box(held_.profilerColumn,
                     ui::Layout{.width = ui::pixels(kBarWidth * static_cast<float>(ticks.size())),
                                .height = ui::pixels(kStripTall),
                                .alignItems = ui::Align::End,
                                .shrink = 0},
                     kBackground);
    if (strip.has_value()) {
        held_.profilerRows.push_back(*strip);
        const double kScale = static_cast<double>(std::max(kMost, kBudget));
        for (const std::int64_t kTick : ticks) {
            const float kTall = std::max(1.0F, static_cast<float>(static_cast<double>(kTick) / kScale) * kStripTall);
            static_cast<void>(box(*strip,
                                  ui::Layout{.width = ui::pixels(kBarWidth - 1),
                                             .height = ui::pixels(kTall),
                                             .shrink = 0,
                                             .margin = {0, 1, 0, 0}},
                                  kTick > kBudget ? kOverFill : kBarFill));
        }
    }
    // The systems that took longest over those ticks, each its mean and
    // its longest.
    std::vector<std::pair<std::int64_t, std::string>> systems;
    std::vector<std::int64_t> mosts;
    for (const Value& kSystem :
         kSystems != nullptr && kSystems->kind() == Value::Kind::Array ? kSystems->items() : std::span<const Value>{}) {
        const Value* kName = kSystem.find("system");
        const Value* kTotal = kSystem.find("totalUs");
        const Value* kLongest = kSystem.find("mostUs");
        if (kName == nullptr || kName->text() == nullptr || kTotal == nullptr || kLongest == nullptr) {
            continue;
        }
        systems.emplace_back(kTotal->integer().value_or(0),
                             *kName->text() + ": mean " +
                                 milliseconds(kTotal->integer().value_or(0) / static_cast<std::int64_t>(ticks.size())) +
                                 ", longest " + milliseconds(kLongest->integer().value_or(0)));
    }
    std::ranges::sort(systems, std::greater<>{}, &std::pair<std::int64_t, std::string>::first);
    for (std::size_t each = 0; each < systems.size() && each < kSystemsShown; ++each) {
        kLine(systems[each].second, kQuiet);
    }
    ++held_.profilerReads;
    emitter_.log(diagnostics::Severity::Info,
                 kProfileRead,
                 "the played game's server's last ticks, as its profile says",
                 {diagnostics::field("ticks", ticks.size()),
                  diagnostics::field("systems", systems.size()),
                  diagnostics::field("longestUs", kMost),
                  diagnostics::field("reads", held_.profilerReads)});
}

} // namespace rawframe::studio
