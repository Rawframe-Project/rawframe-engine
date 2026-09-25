#pragma once

#include "rawframe/base/threads.h"
#include "rawframe/execution/cancellation.h"
#include "rawframe/execution/executor.h"
#include "rawframe/execution/outcome.h"
#include "rawframe/result/result.h"

#include <concepts>
#include <cstddef>
#include <mutex>
#include <optional>
#include <utility>

namespace rawframe::execution {

/// A group of scoped jobs: work that finishes before the scope does. Jobs may
/// borrow anything that outlives the scope. `join`, or the destructor, waits
/// for every job even after one fails or the scope is cancelled, helping run
/// queued work meanwhile rather than blocking a worker (SPEC-0005 scoped job).
///
/// Submission cannot fail: when the queue or the owner's quota is full, the
/// submitting thread, which is committed to waiting anyway, runs the job
/// itself (SPEC-0048 capacities).
class JobScope {
    struct ConstructionTag {};

public:
    /// Fails with `resource_exhausted` when the scope's cancellation child would
    /// be too deep. The JobScope lives inside the Result; use it in place.
    [[nodiscard]] static result::Result<JobScope> create(Executor& executor,
                                                         OwnerId owner,
                                                         CancellationScope& parent,
                                                         FailurePolicy policy = FailurePolicy::Isolate,
                                                         Priority priority = Priority::Normal);

    JobScope(ConstructionTag,
             Executor& executor,
             OwnerId owner,
             CancellationScope& parent,
             FailurePolicy policy,
             Priority priority) noexcept;
    JobScope(const JobScope&) = delete;
    JobScope& operator=(const JobScope&) = delete;
    ~JobScope();

    /// Runs `job(token)` on the executor or, if it is full, here. A job that
    /// has not begun when the scope is cancelled never begins.
    template <typename Job>
        requires std::same_as<std::invoke_result_t<Job&, CancellationToken>, result::Status>
    void run(Job&& job) {
        begin();
        Task task{[this, work = std::forward<Job>(job)]() mutable noexcept {
            if (!scope_->cancelled()) {
                finish(work(CancellationToken{*scope_}));
            } else {
                finish({});
            }
        }};
        if (!executor_->submit(owner_, priority_, std::move(task)).has_value()) {
            task();
        }
    }

    /// Waits for every job, then reports the first Error any job returned, or
    /// the cancellation if the scope was cancelled, or success.
    [[nodiscard]] TaskOutcome<void> join();

    void cancel(CancelReason reason) noexcept {
        scope_->cancel(reason);
    }
    [[nodiscard]] CancellationToken token() noexcept {
        return CancellationToken{*scope_};
    }

private:
    void begin() noexcept;
    void finish(result::Status status) noexcept;
    void wait() noexcept;

    Executor* executor_;
    OwnerId owner_;
    Priority priority_;
    result::Result<CancellationScope> scope_;

    base::Mutex mutex_;
    base::Condition done_;
    std::size_t outstanding_ = 0;
    std::optional<result::Error> firstError_;
};

} // namespace rawframe::execution
