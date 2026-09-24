// Async operations and TaskOutcome: channel separation, boundary mapping,
// refusal on a full queue, the in-flight ceiling, handle drop, stale
// completion, and owner shutdown.

#include "rawframe/execution/errors.h"
#include "rawframe/execution/operation.h"
#include "rawframe/test/test.h"
#include "support.h"

#include <atomic>
#include <optional>
#include <thread>
#include <vector>

using namespace rawframe::execution;
using rawframe::execution::testing::Gate;
using rawframe::execution::testing::kOwner;
using rawframe::execution::testing::occupyWorker;
using rawframe::execution::testing::oneWorker;
using rawframe::result::ErrorClass;
using rawframe::result::Result;

namespace {

constexpr CancellationMapping kMapping{.errorClass = ErrorClass::Unavailable,
                                       .domain = kExecutionDomain,
                                       .code = rawframe::result::ErrorCode{500},
                                       .description = "the request was cancelled"};

} // namespace

RAWFRAME_TEST(OutcomeChannelsStaySeparate) {
    auto value = TaskOutcome<int>::success(5);
    auto cancelled = TaskOutcome<int>::cancelled(CancelReason::Superseded);
    auto failed = TaskOutcome<int>::failed(
        rawframe::result::fail(ErrorClass::DataLoss, kExecutionDomain, rawframe::result::ErrorCode{1}, "lost").error());
    RAWFRAME_EXPECT(value.hasValue() && *value == 5);
    RAWFRAME_EXPECT(cancelled.isCancelled() && !cancelled.isError() && !cancelled.hasValue());
    RAWFRAME_EXPECT(failed.isError() && !failed.isCancelled());

    // Cancellation becomes an Error only through an explicit mapping.
    const Result<int> kMappedCancel = toResult(std::move(cancelled), kMapping);
    RAWFRAME_EXPECT(!kMappedCancel.has_value() && kMappedCancel.error().code() == kMapping.code);
    const Result<int> kMappedError = toResult(std::move(failed), kMapping);
    RAWFRAME_EXPECT(!kMappedError.has_value() && kMappedError.error().errorClass() == ErrorClass::DataLoss);
    const Result<int> kMappedValue = toResult(std::move(value), kMapping);
    RAWFRAME_EXPECT(kMappedValue.has_value() && *kMappedValue == 5);
}

RAWFRAME_TEST(AnOperationDeliversItsValueOrError) {
    ManualClock clock;
    CancellationScope root{clock};
    Executor executor{oneWorker()};
    RAWFRAME_EXPECT(executor.admitOwner(kOwner, Quota{.maximumPendingTasks = 8}).has_value());
    auto operations = OperationScope::create(executor, kOwner, root);
    auto answer = operations->start(Priority::Normal, [](CancellationToken) -> Result<int> {
        return 42;
    });
    auto broken = operations->start(Priority::Normal, [](CancellationToken) -> Result<int> {
        return rawframe::result::fail(ErrorClass::NotFound, kExecutionDomain, rawframe::result::ErrorCode{2}, "gone");
    });
    auto direct = operations->start(Priority::Background, [](CancellationToken) {
        return TaskOutcome<void>::cancelled(CancelReason::Requested);
    });
    RAWFRAME_EXPECT(answer.has_value() && broken.has_value() && direct.has_value());
    auto answerOutcome = answer->wait();
    RAWFRAME_EXPECT(answerOutcome.hasValue() && *answerOutcome == 42);
    RAWFRAME_EXPECT(broken->wait().isError());
    RAWFRAME_EXPECT(direct->wait().cancelReason() == CancelReason::Requested);
    RAWFRAME_EXPECT(!answer->take().has_value());
}

RAWFRAME_TEST(AFullQueueRefusesAndQueuesNothing) {
    ManualClock clock;
    CancellationScope root{clock};
    Gate gate;
    Executor executor{oneWorker()};
    RAWFRAME_EXPECT(executor.admitOwner(kOwner, Quota{.maximumPendingTasks = 3}).has_value());
    occupyWorker(executor, gate);
    {
        auto operations = OperationScope::create(executor, kOwner, root);
        auto first = operations->start(Priority::Normal, [](CancellationToken) -> Result<int> {
            return 1;
        });
        auto second = operations->start(Priority::Normal, [](CancellationToken) -> Result<int> {
            return 2;
        });
        auto third = operations->start(Priority::Normal, [](CancellationToken) -> Result<int> {
            return 3;
        });
        auto refused = operations->start(Priority::Normal, [](CancellationToken) -> Result<int> {
            return 4;
        });
        RAWFRAME_EXPECT(first.has_value() && second.has_value() && third.has_value());
        RAWFRAME_EXPECT(!refused.has_value() && refused.error().errorClass() == ErrorClass::ResourceExhausted);
        RAWFRAME_EXPECT(operations->inFlight() == 3);
        RAWFRAME_EXPECT(executor.pendingTasks() == 3);
        gate.open.store(true);
    }
}

RAWFRAME_TEST(InFlightOperationsRefuseAtExactlyTheirBound) {
    ManualClock clock;
    CancellationScope root{clock};
    Gate gate;
    Executor executor{oneWorker()};
    RAWFRAME_EXPECT(executor.admitOwner(kOwner, Quota{.maximumPendingTasks = 4000}).has_value());
    occupyWorker(executor, gate);
    {
        auto operations = OperationScope::create(executor, kOwner, root);
        std::vector<AsyncHandle<int>> handles;
        for (;;) {
            auto started = operations->start(Priority::Normal, [](CancellationToken) -> Result<int> {
                return 0;
            });
            if (!started.has_value()) {
                RAWFRAME_EXPECT(started.error().code() == code(ExecutionError::TooManyInFlightOperations));
                break;
            }
            handles.push_back(std::move(*started));
        }
        RAWFRAME_EXPECT(handles.size() == kMaximumInFlightAsyncOperationsPerScope);
        gate.open.store(true);
    }
}

RAWFRAME_TEST(DroppingAHandleNeitherCancelsNorDetaches) {
    ManualClock clock;
    CancellationScope root{clock};
    Gate gate;
    Executor executor{oneWorker()};
    RAWFRAME_EXPECT(executor.admitOwner(kOwner, Quota{.maximumPendingTasks = 8}).has_value());
    occupyWorker(executor, gate);
    std::atomic<int> finishedUncancelled{0};
    {
        auto operations = OperationScope::create(executor, kOwner, root);
        {
            auto dropped =
                operations->start(Priority::Normal, [&finishedUncancelled](CancellationToken token) -> Result<int> {
                    finishedUncancelled = token.cancelled() ? 2 : 1;
                    return 0;
                });
            RAWFRAME_EXPECT(dropped.has_value());
        }
        RAWFRAME_EXPECT(operations->inFlight() == 1);
        gate.open.store(true);
        operations->join();
        RAWFRAME_EXPECT(finishedUncancelled.load() == 1);
    }
}

RAWFRAME_TEST(ACompletionForAReplacedGenerationIsNotDelivered) {
    ManualClock clock;
    CancellationScope root{clock};
    Gate gate;
    Executor executor{oneWorker()};
    RAWFRAME_EXPECT(executor.admitOwner(kOwner, Quota{.maximumPendingTasks = 8}).has_value());
    occupyWorker(executor, gate);
    auto operations = OperationScope::create(executor, kOwner, root);
    auto stale = operations->start(Priority::Normal, [](CancellationToken) -> Result<int> {
        return 7;
    });
    operations->invalidate();
    auto fresh = operations->start(Priority::Normal, [](CancellationToken) -> Result<int> {
        return 8;
    });
    gate.open.store(true);
    auto staleOutcome = stale->wait();
    auto freshOutcome = fresh->wait();
    RAWFRAME_EXPECT(staleOutcome.isCancelled() && staleOutcome.cancelReason() == CancelReason::Superseded);
    RAWFRAME_EXPECT(freshOutcome.hasValue() && *freshOutcome == 8);
}

RAWFRAME_TEST(DestroyingTheOwnerCancelsAndJoins) {
    ManualClock clock;
    CancellationScope root{clock};
    Executor executor{oneWorker()};
    RAWFRAME_EXPECT(executor.admitOwner(kOwner, Quota{.maximumPendingTasks = 8}).has_value());
    std::atomic<bool> started{false};
    std::optional<AsyncHandle<void>> handle;
    {
        auto operations = OperationScope::create(executor, kOwner, root);
        auto running = operations->start(Priority::Normal, [&started](CancellationToken token) {
            started = true;
            while (!token.cancelled()) {
                std::this_thread::yield();
            }
            return TaskOutcome<void>::cancelled(*token.reason());
        });
        RAWFRAME_EXPECT(running.has_value());
        handle.emplace(std::move(*running));
        while (!started.load()) {
            std::this_thread::yield();
        }
    }
    // The scope is gone, and its operation finished before it went.
    RAWFRAME_EXPECT(handle->ready());
    auto outcome = handle->take();
    RAWFRAME_EXPECT(outcome && outcome->isCancelled() && outcome->cancelReason() == CancelReason::OwnerStopping);
    RAWFRAME_EXPECT(!root.cancelled());
}
