#pragma once

// The engine's one mutex and one condition, over the standard ones where the
// target has threads (RAWFRAME_THREADS, rawframe/base/platform.h). Where it
// has none, the web's main thread (ADR-0084), a lock has nothing to exclude
// and is empty, and a wait could only be ended by work on this same thread:
// every engine wait first runs what it waits on (the executors' helping
// waits), so a wait still unsatisfied is a deadlock and takes the fatal path
// instead of spinning forever.
//
// Hold a Mutex with std::lock_guard or std::unique_lock, which both kinds
// meet; std::scoped_lock does not exist without threads.

#include "rawframe/base/assert.h"
#include "rawframe/base/platform.h"

#include <chrono>
#include <condition_variable>
#include <mutex>

namespace rawframe::base {

#if RAWFRAME_THREADS

using Mutex = std::mutex;
using Condition = std::condition_variable;

#else

// The standard's member names, so both kinds serve the same code.
class Mutex {
public:
    void lock() noexcept {
    }
    void unlock() noexcept {
    }
    [[nodiscard]] bool try_lock() noexcept {
        return true;
    }
};

class Condition {
public:
    void notify_one() noexcept {
    }
    void notify_all() noexcept {
    }

    template <typename Lock, typename Predicate> void wait(Lock& /*lock*/, Predicate satisfied) {
        if (!satisfied()) {
            RAWFRAME_PANIC("a wait on the only thread that nothing here can end");
        }
    }

    template <typename Lock, typename Rep, typename Period>
    void wait_for(Lock& /*lock*/, const std::chrono::duration<Rep, Period>& /*interval*/) {
        RAWFRAME_PANIC("a wait on the only thread that nothing here can end");
    }
};

#endif

} // namespace rawframe::base
