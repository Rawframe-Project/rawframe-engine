#include "shell.h"

#include <cstdio>

namespace rawframe::studio {

namespace {

std::string linkWords(const LinkChoice& link) {
    if (link.latencyMs == 0 && link.jitterMs == 0 && link.lossPpm == 0) {
        return "link: as this machine's, no conditions";
    }
    std::string made = "link: " + std::to_string(link.latencyMs) + " ms each way, up to " +
                       std::to_string(link.jitterMs) + " ms more for datagrams";
    if (link.lossPpm > 0) {
        char loss[32];
        std::snprintf(loss, sizeof loss, ", %.1f%% of them lost", link.lossPpm / 10'000.0);
        made += loss;
    }
    return made + " (from the next play)";
}

} // namespace

result::Status ShellParticipant::makeNetwork() {
    RAWFRAME_TRY_ASSIGN(
        networkColumn_,
        box(std::nullopt,
            ui::Layout{.minHeight = ui::pixels(0), .direction = ui::Direction::Column, .gap = 4, .grow = 1},
            kPanel));
    RAWFRAME_TRY(tree_->setRole(networkColumn_, ui::Role::Group));
    RAWFRAME_TRY(tree_->setName(networkColumn_, "Network"));
    RAWFRAME_TRY_ASSIGN(const ui::Node kBar,
                        box(networkColumn_, ui::Layout{.height = ui::pixels(26), .gap = 4, .shrink = 0}, 0));
    network_.linkButtons.clear();
    for (std::size_t each = 0; each < kLinks.size(); ++each) {
        RAWFRAME_TRY_ASSIGN(const ui::Node kButton,
                            box(kBar, ui::Layout{.shrink = 0, .padding = {10, 4, 10, 4}}, kRow));
        RAWFRAME_TRY(pressWords(kButton, kLinks[each].name, kText, 13));
        network_.linkButtons.emplace_back(kButton, each);
    }
    network_.lines.clear();
    showNetwork({});
    return {};
}

bool ShellParticipant::pressNetwork(ui::Node node) {
    const auto kButton = std::ranges::find(network_.linkButtons, node, &std::pair<ui::Node, std::size_t>::first);
    if (kButton == network_.linkButtons.end()) {
        return false;
    }
    network_.link = kButton->second;
    const LinkChoice& kLink = kLinks[network_.link];
    play_->link = kLink.latencyMs == 0 && kLink.jitterMs == 0 && kLink.lossPpm == 0
                      ? std::string{}
                      : "network.quic.latency_ms = " + std::to_string(kLink.latencyMs) +
                            "\nnetwork.quic.jitter_ms = " + std::to_string(kLink.jitterMs) +
                            "\nnetwork.quic.loss_ppm = " + std::to_string(kLink.lossPpm) + "\nnetwork.quic.seed = 1\n";
    showNetwork({});
    say(std::string{kLink.name} + (playing_.has_value() ? " from the next play" : ""));
    return true;
}

void ShellParticipant::readNetwork(double seconds) {
    if (!playing_.has_value() || scene_.empty() || dockShown_ != Dock::Network || seconds < network_.nextRead) {
        return;
    }
    network_.nextRead = seconds + 1;
    const std::string kReply = ask(trafficRecord(next(), scene_));
    const auto kParsed = document::parse(kReply);
    const Value* kAnswer = kParsed.has_value() ? kParsed->find("answer") : nullptr;
    if (kAnswer == nullptr) {
        showNetwork({"the game's server: " + answeredOf(kReply).message});
        return;
    }
    const auto kCount = [&](std::string_view name) {
        const Value* kRead = kAnswer->find(name);
        return kRead != nullptr ? kRead->integer().value_or(0) : 0;
    };
    const TrafficRead kNow{.at = seconds,
                           .stateBytes = kCount("stateBytes"),
                           .stateDatagrams = kCount("stateDatagrams"),
                           .inputWindows = kCount("inputWindows")};
    network_.connections = kCount("connections");
    std::vector<std::string> lines;
    lines.push_back("server at tick " + std::to_string(kCount("tick")) + ", " + std::to_string(network_.connections) +
                    (network_.connections == 1 ? " connection" : " connections"));
    if (network_.last.has_value() && kNow.at > network_.last->at) {
        const double kSpan = kNow.at - network_.last->at;
        char rates[160];
        std::snprintf(rates,
                      sizeof rates,
                      "state out: %.1f KiB/s in %.0f datagrams/s; input in: %.0f windows/s",
                      static_cast<double>(kNow.stateBytes - network_.last->stateBytes) / 1024.0 / kSpan,
                      static_cast<double>(kNow.stateDatagrams - network_.last->stateDatagrams) / kSpan,
                      static_cast<double>(kNow.inputWindows - network_.last->inputWindows) / kSpan);
        lines.emplace_back(rates);
    }
    lines.push_back("inputs stale " + std::to_string(kCount("inputsStale")) + ", refused " +
                    std::to_string(kCount("inputsRefused")) + "; messages sent " +
                    std::to_string(kCount("messagesSent")));
    network_.last = kNow;
    ++network_.reads;
    showNetwork(lines);
    say("network: " + lines.front());
}

void ShellParticipant::showNetwork(const std::vector<std::string>& lines) {
    for (const auto& [kButton, kEach] : network_.linkButtons) {
        static_cast<void>(
            tree_->setLook(kButton, ui::Look{.fill = kEach == network_.link ? kChosen : kRow, .radius = 4}));
    }
    for (const ui::Node kLine : network_.lines) {
        static_cast<void>(tree_->remove(kLine));
    }
    network_.lines.clear();
    std::vector<std::string> shown{linkWords(kLinks[network_.link])};
    if (lines.empty()) {
        shown.emplace_back(playing_.has_value() ? "reading the game's server" : "play the game to see its traffic");
    }
    shown.insert(shown.end(), lines.begin(), lines.end());
    for (const std::string& kLine : shown) {
        if (auto made = row(networkColumn_, kLine, kText, kPanel); made.has_value()) {
            network_.lines.push_back(*made);
        }
    }
}

} // namespace rawframe::studio
