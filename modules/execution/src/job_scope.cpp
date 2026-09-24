#include "rawframe/execution/job_scope.h"

#include "rawframe/base/assert.h"
#include "rawframe/execution/bounds.h"
#include "rawframe/execution/errors.h"

#include <chrono>

namespace rawframe::execution {

namespace {

// A waiting thread rechecks the queue this often, so it helps with work queued
// after it began waiting.
constexpr auto kHelpInterval = std::chrono::milliseconds(1);

} // namespace

result::Result<JobScope> JobScope::create(
    Executor& executor, OwnerId owner, CancellationScope& parent, FailurePolicy policy, Priority priority) {
    if (parent.depth() >= kMaximumCancellationScopeDepth) {
        return result::fail(result::ErrorClass::ResourceExhausted,
                            kExecutionDomain,
                            code(ExecutionError::ScopeTooDeep),
                            "cancellation scopes nest deeper than kMaximumCancellationScopeDepth");
    }
    return result::Result<JobScope>{std::in_place, ConstructionTag{}, executor, owner, parent, policy, priority};
}

JobScope::JobScope(ConstructionTag,
                   Executor& executor,
                   OwnerId owner,
                   CancellationScope& parent,
                   FailurePolicy policy,
                   Priority priority) noexcept
    : executor_(&executor), owner_(owner), priority_(priority), scope_(CancellationScope::child(parent, policy)) {
    RAWFRAME_CHECK(scope_.has_value(), "create checked the depth");
}

JobScope::~JobScope() {
    wait();
}

void JobScope::begin() noexcept {
    const std::scoped_lock kLock{mutex_};
    ++outstanding_;
}

void JobScope::finish(result::Status status) noexcept {
    if (!status.has_value()) {
        scope_->reportFailure();
    }
    // Notify under the lock: once outstanding_ reaches zero the joiner may
    // destroy this scope as soon as the lock is released.
    const std::scoped_lock kLock{mutex_};
    if (!status.has_value() && !firstError_) {
        firstError_.emplace(std::move(status).error());
    }
    --outstanding_;
    if (outstanding_ == 0) {
        done_.notify_all();
    }
}

void JobScope::wait() noexcept {
    for (;;) {
        {
            const std::scoped_lock kLock{mutex_};
            if (outstanding_ == 0) {
                return;
            }
        }
        if (executor_->runOne()) {
            continue;
        }
        std::unique_lock lock{mutex_};
        if (outstanding_ != 0) {
            done_.wait_for(lock, kHelpInterval);
        }
    }
}

TaskOutcome<void> JobScope::join() {
    wait();
    if (firstError_) {
        result::Error error = std::move(*firstError_);
        firstError_.reset();
        return TaskOutcome<void>::failed(std::move(error));
    }
    if (const auto kReason = scope_->reason()) {
        return TaskOutcome<void>::cancelled(*kReason);
    }
    return TaskOutcome<void>::success();
}

} // namespace rawframe::execution
