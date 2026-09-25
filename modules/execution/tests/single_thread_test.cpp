// Execution without threads, as the web's main thread runs it (ADR-0084,
// rawframe/base/threads.h): an executor has no workers, so queued work runs
// only when its host asks, in priority order; every helping wait runs what
// it waits on; and stopping runs what was accepted on the calling thread.
// Built for the web only: natively the executors have workers.

#include "rawframe/execution/job_scope.h"
#include "rawframe/execution/operation.h"
#include "rawframe/test/test.h"

#include <vector>

using namespace rawframe::execution;
using rawframe::result::Result;
using rawframe::result::Status;

namespace {

constexpr OwnerId kOwner{1};

ExecutorSettings settings(const MonotonicSource& clock) {
    // A worker count asked for is not a thread made.
    return ExecutorSettings{.kind = ExecutorKind::Cpu, .workers = 4, .clock = &clock};
}

} // namespace

RAWFRAME_TEST(QueuedWorkRunsOnlyWhenTheHostAsks) {
    ManualClock clock;
    Executor executor{settings(clock)};
    RAWFRAME_EXPECT(executor.workerCount() == 0);
    RAWFRAME_EXPECT(executor.admitOwner(kOwner, Quota{.maximumPendingTasks = 8, .mayUseCritical = true}));
    std::vector<int> order;
    RAWFRAME_EXPECT(executor.submit(kOwner, Priority::Background, [&order]() noexcept {
        order.push_back(3);
    }));
    RAWFRAME_EXPECT(executor.submit(kOwner, Priority::Normal, [&order]() noexcept {
        order.push_back(2);
    }));
    RAWFRAME_EXPECT(executor.submit(kOwner, Priority::Critical, [&order]() noexcept {
        order.push_back(1);
    }));
    RAWFRAME_EXPECT(order.empty() && executor.progress().waiting == 3);
    RAWFRAME_EXPECT(executor.runOne() && executor.runOne() && executor.runOne() && !executor.runOne());
    RAWFRAME_EXPECT((order == std::vector<int>{1, 2, 3}) && executor.progress().completed == 3);
}

RAWFRAME_TEST(StoppingRunsWhatWasAcceptedHere) {
    ManualClock clock;
    Executor executor{settings(clock)};
    RAWFRAME_EXPECT(executor.admitOwner(kOwner, Quota{.maximumPendingTasks = 8}));
    int ran = 0;
    for (int index = 0; index < 5; ++index) {
        RAWFRAME_EXPECT(executor.submit(kOwner, Priority::Normal, [&ran]() noexcept {
            ++ran;
        }));
    }
    executor.stop();
    RAWFRAME_EXPECT(ran == 5 && executor.pendingTasks() == 0);
    RAWFRAME_EXPECT(!executor.submit(kOwner, Priority::Normal, []() noexcept {}).has_value());
}

RAWFRAME_TEST(HelpingWaitsRunWhatTheyWaitOn) {
    ManualClock clock;
    CancellationScope root{clock};
    Executor executor{settings(clock)};
    RAWFRAME_EXPECT(executor.admitOwner(kOwner, Quota{.maximumPendingTasks = 64}));

    // Jobs, some of which start more jobs of their own scope.
    int ran = 0;
    {
        auto jobs = JobScope::create(executor, kOwner, root);
        for (int index = 0; index < 10; ++index) {
            jobs->run([&ran, &executor, &root](CancellationToken) -> Status {
                auto inner = JobScope::create(executor, kOwner, root);
                inner->run([&ran](CancellationToken) -> Status {
                    ++ran;
                    return {};
                });
                ++ran;
                RAWFRAME_EXPECT(inner->join().hasValue());
                return {};
            });
        }
        RAWFRAME_EXPECT(jobs->join().hasValue());
    }
    RAWFRAME_EXPECT(ran == 20);

    // An operation's handle waits by running it.
    auto operations = OperationScope::create(executor, kOwner, root);
    auto answer = operations->start(Priority::Normal, [](CancellationToken) -> Result<int> {
        return 42;
    });
    RAWFRAME_EXPECT(answer.has_value() && !answer->ready());
    auto outcome = answer->wait();
    RAWFRAME_EXPECT(outcome.hasValue() && *outcome == 42);
    operations->join();
    RAWFRAME_EXPECT(operations->inFlight() == 0);
}
