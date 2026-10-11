#include "game_debugging.h"

#include "rawframe/base/platform.h"

#include <optional>
#include <utility>

#if RAWFRAME_THREADS
#include <chrono>
#include <thread>
#endif

namespace rawframe::world_kest {

void GameDebugging::attach(kest::Machine& machine) noexcept {
    machine_ = &machine;
    machine.whenStopped([this] {
        stop();
    });
    if (!functions_.empty() || !lines_.empty()) {
        static_cast<void>(machine.breakAt(functions_, lines_));
    }
}

world_runtime::DebugBroken GameDebugging::breakAt(std::vector<std::string> functions,
                                                  std::vector<world_runtime::DebugLine> lines) noexcept {
    functions_ = std::move(functions);
    lines_.clear();
    for (world_runtime::DebugLine& each : lines) {
        lines_.push_back(kest::SourceLine{.file = std::move(each.file), .line = each.line});
    }
    if (machine_ == nullptr) {
        return world_runtime::DebugBroken{.functions = 0, .lines = std::vector<bool>(lines_.size(), false)};
    }
    kest::Breaks broken = machine_->breakAt(functions_, lines_);
    return world_runtime::DebugBroken{.functions = broken.functions, .lines = std::move(broken.lines)};
}

void GameDebugging::tellStood(const execution::MonotonicSource& clock,
                              std::function<void(execution::MonotonicDuration)> stood) noexcept {
    clock_ = &clock;
    stood_ = std::move(stood);
}

void GameDebugging::whileStopped(std::function<bool()> serve) noexcept {
    serve_ = std::move(serve);
}

bool GameDebugging::stopped() const noexcept {
    return stopped_;
}

std::uint64_t GameDebugging::stops() const noexcept {
    return stops_;
}

std::vector<world_runtime::DebugFrame> GameDebugging::stack() const {
    std::vector<world_runtime::DebugFrame> frames;
    if (!stopped_ || machine_ == nullptr) {
        return frames;
    }
    for (kest::StoppedFrame& each : machine_->stack()) {
        std::optional<world_runtime::DebugLine> at;
        if (each.at.has_value()) {
            at = world_runtime::DebugLine{.file = std::move(each.at->file), .line = each.at->line};
        }
        frames.push_back(world_runtime::DebugFrame{
            .function = std::move(each.function), .at = std::move(at), .locals = std::move(each.locals)});
    }
    return frames;
}

void GameDebugging::stop() noexcept {
    ++stops_;
    stopped_ = true;
    const std::optional<execution::MonotonicInstant> kStood =
        clock_ != nullptr ? std::optional{clock_->now()} : std::nullopt;
    // The tick waits here, served a few milliseconds apart, until whoever
    // debugs it says to carry on; with none, it carries on at once. Without
    // threads there is no one to wait for.
#if RAWFRAME_THREADS
    while (serve_ && !serve_()) {
        std::this_thread::sleep_for(std::chrono::milliseconds{5});
    }
#endif
    stopped_ = false;
    if (kStood.has_value() && stood_) {
        stood_(clock_->now() - *kStood);
    }
}

} // namespace rawframe::world_kest
