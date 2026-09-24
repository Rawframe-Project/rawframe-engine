// Scoped jobs: joining after success, Error, and cancellation, inline execution
// on a full queue, and nested joins on one worker.

#include "rawframe/execution/errors.h"
#include "rawframe/execution/job_scope.h"
#include "rawframe/test/test.h"
#include "support.h"

#include <atomic>
#include <thread>

using namespace rawframe::execution;
using rawframe::execution::testing::Gate;
using rawframe::execution::testing::kOwner;
using rawframe::execution::testing::occupyWorker;
using rawframe::execution::testing::oneWorker;
using rawframe::result::Status;

namespace {

constexpr rawframe::result::ErrorCode kJobFailed{77};

Status failJob() {
    return rawframe::result::fail(
        rawframe::result::ErrorClass::Internal, kExecutionDomain, kJobFailed, "the job failed on purpose");
}

} // namespace

RAWFRAME_TEST(EveryJobRunsBeforeJoinReturns) {
    ManualClock clock;
    CancellationScope root{clock};
    Executor executor{oneWorker()};
    RAWFRAME_EXPECT(executor.admitOwner(kOwner, Quota{.maximumPendingTasks = 64}).has_value());
    std::atomic<int> ran{0};
    auto jobs = JobScope::create(executor, kOwner, root);
    for (int index = 0; index < 200; ++index) {
        jobs->run([&ran](CancellationToken) -> Status {
            ++ran;
            return {};
        });
    }
    RAWFRAME_EXPECT(jobs->join().hasValue());
    RAWFRAME_EXPECT(ran.load() == 200);
}

RAWFRAME_TEST(JoinWaitsForEveryJobEvenAfterAnError) {
    ManualClock clock;
    CancellationScope root{clock};
    Executor executor{oneWorker()};
    RAWFRAME_EXPECT(executor.admitOwner(kOwner, Quota{.maximumPendingTasks = 64}).has_value());
    std::atomic<int> ran{0};
    auto jobs = JobScope::create(executor, kOwner, root);
    jobs->run([](CancellationToken) {
        return failJob();
    });
    for (int index = 0; index < 20; ++index) {
        jobs->run([&ran](CancellationToken) -> Status {
            std::this_thread::yield();
            ++ran;
            return {};
        });
    }
    auto outcome = jobs->join();
    RAWFRAME_EXPECT(outcome.isError());
    RAWFRAME_EXPECT(outcome.isError() && outcome.error().code() == kJobFailed);
    // Isolation: the failure stopped nothing else.
    RAWFRAME_EXPECT(ran.load() == 20);
    RAWFRAME_EXPECT(!root.cancelled());
}

RAWFRAME_TEST(FailFastStopsJobsThatHaveNotBegun) {
    ManualClock clock;
    CancellationScope root{clock};
    Gate gate;
    Executor executor{oneWorker()};
    RAWFRAME_EXPECT(executor.admitOwner(kOwner, Quota{.maximumPendingTasks = 64}).has_value());
    occupyWorker(executor, gate);
    std::atomic<int> ran{0};
    {
        auto jobs = JobScope::create(executor, kOwner, root, FailurePolicy::FailFast);
        jobs->run([](CancellationToken) {
            return failJob();
        });
        for (int index = 0; index < 5; ++index) {
            jobs->run([&ran](CancellationToken) -> Status {
                ++ran;
                return {};
            });
        }
        // The worker is held, so this thread runs the jobs in order while it
        // joins: the failure comes first and the rest never begin.
        auto outcome = jobs->join();
        RAWFRAME_EXPECT(outcome.isError());
    }
    RAWFRAME_EXPECT(ran.load() == 0);
    RAWFRAME_EXPECT(!root.cancelled());
    gate.open.store(true);
}

RAWFRAME_TEST(ACancelledScopeReportsCancellationNotError) {
    ManualClock clock;
    CancellationScope root{clock};
    Executor executor{oneWorker()};
    RAWFRAME_EXPECT(executor.admitOwner(kOwner, Quota{.maximumPendingTasks = 64}).has_value());
    std::atomic<int> ran{0};
    auto jobs = JobScope::create(executor, kOwner, root);
    root.cancel(CancelReason::OwnerStopping);
    jobs->run([&ran](CancellationToken) -> Status {
        ++ran;
        return {};
    });
    auto outcome = jobs->join();
    RAWFRAME_EXPECT(outcome.isCancelled() && outcome.cancelReason() == CancelReason::OwnerStopping);
    RAWFRAME_EXPECT(!outcome.isError());
    RAWFRAME_EXPECT(ran.load() == 0);
}

RAWFRAME_TEST(AFullQueueRunsTheJobInline) {
    ManualClock clock;
    CancellationScope root{clock};
    Gate gate;
    Executor executor{oneWorker()};
    RAWFRAME_EXPECT(executor.admitOwner(kOwner, Quota{.maximumPendingTasks = 2}).has_value());
    occupyWorker(executor, gate);
    const std::thread::id kHere = std::this_thread::get_id();
    std::atomic<bool> ranHere{false};
    {
        auto jobs = JobScope::create(executor, kOwner, root);
        jobs->run([](CancellationToken) -> Status {
            return {};
        });
        jobs->run([](CancellationToken) -> Status {
            return {};
        });
        // The owner's quota is full: the submitter runs this one itself.
        jobs->run([&ranHere, kHere](CancellationToken) -> Status {
            ranHere = std::this_thread::get_id() == kHere;
            return {};
        });
        RAWFRAME_EXPECT(ranHere.load());
        RAWFRAME_EXPECT(jobs->join().hasValue());
    }
    {
        // Inline execution still honours cancellation.
        auto jobs = JobScope::create(executor, kOwner, root);
        jobs->run([](CancellationToken) -> Status {
            return {};
        });
        jobs->run([](CancellationToken) -> Status {
            return {};
        });
        jobs->cancel(CancelReason::Requested);
        std::atomic<bool> ran{false};
        jobs->run([&ran](CancellationToken) -> Status {
            ran = true;
            return {};
        });
        RAWFRAME_EXPECT(!ran.load());
        RAWFRAME_EXPECT(jobs->join().isCancelled());
    }
    gate.open.store(true);
}

RAWFRAME_TEST(NestedJoinsOnOneWorkerDoNotDeadlock) {
    ManualClock clock;
    CancellationScope root{clock};
    Executor executor{oneWorker()};
    RAWFRAME_EXPECT(executor.admitOwner(kOwner, Quota{.maximumPendingTasks = 4096}).has_value());
    std::atomic<int> leaves{0};
    auto outer = JobScope::create(executor, kOwner, root);
    for (int index = 0; index < 16; ++index) {
        outer->run([&executor, &root, &leaves](CancellationToken) -> Status {
            auto inner = JobScope::create(executor, kOwner, root);
            for (int leaf = 0; leaf < 16; ++leaf) {
                inner->run([&leaves](CancellationToken) -> Status {
                    ++leaves;
                    return {};
                });
            }
            return inner->join().hasValue() ? Status{} : failJob();
        });
    }
    RAWFRAME_EXPECT(outer->join().hasValue());
    RAWFRAME_EXPECT(leaves.load() == 256);
}
