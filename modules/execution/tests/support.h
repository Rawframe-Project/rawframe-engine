#pragma once

// Shared pieces for the execution tests: a gate that holds a worker busy so
// that queued work stays queued, and a one-worker executor with one owner.

#include "rawframe/execution/executor.h"

#include <atomic>
#include <thread>

namespace rawframe::execution::testing {

inline constexpr OwnerId kOwner{1};

/// Holds whoever waits on it until it opens.
struct Gate {
    std::atomic<bool> open{false};
    std::atomic<bool> entered{false};

    void wait() noexcept {
        entered.store(true);
        while (!open.load()) {
            std::this_thread::yield();
        }
    }
};

/// Occupies the executor's only worker until `gate` opens, and returns once it
/// is running, so later submissions stay pending.
inline void occupyWorker(Executor& executor, Gate& gate) {
    const auto kStatus = executor.submit(kOwner, Priority::Normal, [&gate]() noexcept {
        gate.wait();
    });
    static_cast<void>(kStatus);
    while (!gate.entered.load()) {
        std::this_thread::yield();
    }
}

inline ExecutorSettings oneWorker(const MonotonicSource* clock = nullptr) {
    return ExecutorSettings{.kind = ExecutorKind::Cpu, .workers = 1, .clock = clock};
}

} // namespace rawframe::execution::testing
