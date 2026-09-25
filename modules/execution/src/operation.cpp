#include "rawframe/execution/operation.h"

#include "rawframe/base/assert.h"
#include "rawframe/base/threads.h"

#include <chrono>

namespace rawframe::execution {

namespace {

// A waiting thread rechecks the queue this often, so it helps with work queued
// after it began waiting.
constexpr auto kHelpInterval = std::chrono::milliseconds(1);

} // namespace

result::Result<OperationScope>
OperationScope::create(Executor& executor, OwnerId owner, CancellationScope& parent, FailurePolicy policy) {
    if (parent.depth() >= kMaximumCancellationScopeDepth) {
        return result::fail(result::ErrorClass::ResourceExhausted,
                            kExecutionDomain,
                            code(ExecutionError::ScopeTooDeep),
                            "cancellation scopes nest deeper than kMaximumCancellationScopeDepth");
    }
    return result::Result<OperationScope>{std::in_place, ConstructionTag{}, executor, owner, parent, policy};
}

OperationScope::OperationScope(
    ConstructionTag, Executor& executor, OwnerId owner, CancellationScope& parent, FailurePolicy policy) noexcept
    : executor_(&executor), owner_(owner), scope_(CancellationScope::child(parent, policy)) {
    RAWFRAME_CHECK(scope_.has_value(), "create checked the depth");
}

OperationScope::~OperationScope() {
    scope_->cancel(CancelReason::OwnerStopping);
    join();
}

result::Result<std::uint64_t> OperationScope::reserve() {
    const std::lock_guard kLock{mutex_};
    if (inFlight_ == kMaximumInFlightAsyncOperationsPerScope) {
        return result::fail(result::ErrorClass::ResourceExhausted,
                            kExecutionDomain,
                            code(ExecutionError::TooManyInFlightOperations),
                            "kMaximumInFlightAsyncOperationsPerScope operations are in flight");
    }
    ++inFlight_;
    return generation_;
}

void OperationScope::unreserve() noexcept {
    const std::lock_guard kLock{mutex_};
    --inFlight_;
    changed_.notify_all();
}

bool OperationScope::current(std::uint64_t generation) const noexcept {
    const std::lock_guard kLock{mutex_};
    return generation == generation_;
}

void OperationScope::invalidate() noexcept {
    const std::lock_guard kLock{mutex_};
    ++generation_;
}

void OperationScope::completed(bool failed) noexcept {
    if (failed) {
        scope_->reportFailure();
    }
    // Notify under the lock: once inFlight_ reaches zero a joiner may destroy
    // this scope as soon as the lock is released.
    const std::lock_guard kLock{mutex_};
    --inFlight_;
    changed_.notify_all();
}

void OperationScope::waitUntil(const detail::OperationBase& operation) noexcept {
    while (!operation.ready()) {
        if (executor_->runOne()) {
            continue;
        }
        std::unique_lock lock{mutex_};
        if (!operation.ready()) {
            changed_.wait_for(lock, kHelpInterval);
        }
    }
}

void OperationScope::join() noexcept {
    for (;;) {
        {
            const std::lock_guard kLock{mutex_};
            if (inFlight_ == 0) {
                return;
            }
        }
        if (executor_->runOne()) {
            continue;
        }
        std::unique_lock lock{mutex_};
        if (inFlight_ != 0) {
            changed_.wait_for(lock, kHelpInterval);
        }
    }
}

std::size_t OperationScope::inFlight() const noexcept {
    const std::lock_guard kLock{mutex_};
    return inFlight_;
}

} // namespace rawframe::execution
