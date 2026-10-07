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
    if (!functions_.empty()) {
        static_cast<void>(machine.breakAt(functions_));
    }
}

std::size_t GameDebugging::breakAt(std::vector<std::string> functions) noexcept {
    functions_ = std::move(functions);
    return machine_ != nullptr ? machine_->breakAt(functions_) : 0;
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
        frames.push_back(
            world_runtime::DebugFrame{.function = std::move(each.function), .locals = std::move(each.locals)});
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
