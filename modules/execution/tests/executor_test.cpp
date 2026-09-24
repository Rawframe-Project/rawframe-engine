// The executor: admission, quotas, ceilings, priorities, background progress,
// the blocking rule, worker derivation, and shutdown.

#include "rawframe/execution/bounds.h"
#include "rawframe/execution/budget.h"
#include "rawframe/execution/errors.h"
#include "rawframe/execution/executor.h"
#include "rawframe/execution/parallelism.h"
#include "rawframe/test/test.h"
#include "support.h"

#include <array>
#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <string>
#include <thread>
#include <vector>

#if defined(__linux__)
#include <sched.h>
#endif

using namespace rawframe::execution;
using rawframe::execution::testing::Gate;
using rawframe::execution::testing::kOwner;
using rawframe::execution::testing::occupyWorker;
using rawframe::execution::testing::oneWorker;
using rawframe::result::ErrorClass;

namespace {

bool refusedWith(const rawframe::result::Status& status, ErrorClass errorClass, ExecutionError error) {
    return !status.has_value() && status.error().errorClass() == errorClass && status.error().code() == code(error);
}

} // namespace

RAWFRAME_TEST(SubmittedTasksAllRunAndStopDrainsThem) {
    std::atomic<int> ran{0};
    Executor executor{oneWorker()};
    RAWFRAME_EXPECT(executor.workerCount() == 1);
    RAWFRAME_EXPECT(executor.admitOwner(kOwner, Quota{.maximumPendingTasks = 1000}).has_value());
    for (int index = 0; index < 500; ++index) {
        RAWFRAME_EXPECT(executor
                            .submit(kOwner,
                                    Priority::Normal,
                                    [&ran]() noexcept {
                                        ++ran;
                                    })
                            .has_value());
    }
    executor.stop();
    RAWFRAME_EXPECT(ran.load() == 500);
    RAWFRAME_EXPECT(refusedWith(executor.submit(kOwner, Priority::Normal, []() noexcept {}),
                                ErrorClass::Unavailable,
                                ExecutionError::AdmissionClosed));
    executor.stop();
}

RAWFRAME_TEST(OwnersNeedAnAcceptedQuota) {
    Executor executor{oneWorker()};
    RAWFRAME_EXPECT(refusedWith(executor.submit(OwnerId{9}, Priority::Normal, []() noexcept {}),
                                ErrorClass::PermissionDenied,
                                ExecutionError::OwnerHasNoQuota));
    RAWFRAME_EXPECT(executor.pendingTasks() == 0);
    RAWFRAME_EXPECT(refusedWith(executor.admitOwner(kOwner, Quota{.maximumPendingTasks = 0}),
                                ErrorClass::InvalidArgument,
                                ExecutionError::ZeroQuota));
    RAWFRAME_EXPECT(executor.admitOwner(kOwner, Quota{.maximumPendingTasks = 1}).has_value());
    RAWFRAME_EXPECT(refusedWith(executor.admitOwner(kOwner, Quota{.maximumPendingTasks = 1}),
                                ErrorClass::AlreadyExists,
                                ExecutionError::OwnerAlreadyAdmitted));
    for (std::size_t index = 1; index < kMaximumQuotaOwners; ++index) {
        RAWFRAME_EXPECT(executor.admitOwner(OwnerId{100 + index}, Quota{.maximumPendingTasks = 1}).has_value());
    }
    RAWFRAME_EXPECT(refusedWith(executor.admitOwner(OwnerId{99}, Quota{.maximumPendingTasks = 1}),
                                ErrorClass::ResourceExhausted,
                                ExecutionError::QuotaTableFull));
}

RAWFRAME_TEST(CriticalIsOnlyForTrustedOwners) {
    constexpr OwnerId kScript{2};
    constexpr OwnerId kHost{3};
    Executor executor{oneWorker()};
    RAWFRAME_EXPECT(executor.admitOwner(kScript, Quota{.maximumPendingTasks = 8}).has_value());
    RAWFRAME_EXPECT(executor.admitOwner(kHost, Quota{.maximumPendingTasks = 8, .mayUseCritical = true}).has_value());
    RAWFRAME_EXPECT(refusedWith(executor.submit(kScript, Priority::Critical, []() noexcept {}),
                                ErrorClass::PermissionDenied,
                                ExecutionError::PriorityNotPermitted));
    RAWFRAME_EXPECT(executor.submit(kScript, Priority::Background, []() noexcept {}).has_value());
    RAWFRAME_EXPECT(executor.submit(kHost, Priority::Critical, []() noexcept {}).has_value());
}

RAWFRAME_TEST(AnOwnersQuotaRefusesAtExactlyItsBound) {
    constexpr OwnerId kOther{2};
    Gate gate;
    Executor executor{oneWorker()};
    RAWFRAME_EXPECT(executor.admitOwner(kOwner, Quota{.maximumPendingTasks = 8}).has_value());
    RAWFRAME_EXPECT(executor.admitOwner(kOther, Quota{.maximumPendingTasks = 5}).has_value());
    occupyWorker(executor, gate);
    for (int index = 0; index < 5; ++index) {
        RAWFRAME_EXPECT(executor.submit(kOther, Priority::Background, []() noexcept {}).has_value());
    }
    RAWFRAME_EXPECT(refusedWith(executor.submit(kOther, Priority::Normal, []() noexcept {}),
                                ErrorClass::ResourceExhausted,
                                ExecutionError::OwnerQuotaExhausted));
    // Quotas are per owner: another owner is unaffected.
    RAWFRAME_EXPECT(executor.submit(kOwner, Priority::Normal, []() noexcept {}).has_value());
    RAWFRAME_EXPECT(executor.pendingTasks() == 6);
    gate.open.store(true);
}

RAWFRAME_TEST(TheQueueRefusesAtExactlyItsBound) {
    Gate gate;
    Executor executor{oneWorker()};
    RAWFRAME_EXPECT(
        executor.admitOwner(kOwner, Quota{.maximumPendingTasks = kMaximumPendingTasksPerExecutor + 10}).has_value());
    occupyWorker(executor, gate);
    std::size_t accepted = 0;
    while (executor.submit(kOwner, Priority::Normal, []() noexcept {}).has_value()) {
        ++accepted;
    }
    RAWFRAME_EXPECT(accepted == kMaximumPendingTasksPerExecutor);
    RAWFRAME_EXPECT(refusedWith(executor.submit(kOwner, Priority::Normal, []() noexcept {}),
                                ErrorClass::ResourceExhausted,
                                ExecutionError::QueueFull));
    gate.open.store(true);
    executor.stop();
    RAWFRAME_EXPECT(executor.pendingTasks() == 0);
}

RAWFRAME_TEST(PriorityOrdersAndBackgroundIsPromotedBeforeItStarves) {
    ManualClock clock;
    Gate gate;
    Executor executor{oneWorker(&clock)};
    RAWFRAME_EXPECT(executor.admitOwner(kOwner, Quota{.maximumPendingTasks = 64, .mayUseCritical = true}).has_value());
    occupyWorker(executor, gate);

    std::vector<char> order;
    const auto kRecord = [&order](char label) {
        return [&order, label]() noexcept {
            order.push_back(label);
        };
    };
    RAWFRAME_EXPECT(executor.submit(kOwner, Priority::Background, kRecord('b')).has_value());
    RAWFRAME_EXPECT(executor.submit(kOwner, Priority::Normal, kRecord('n')).has_value());
    RAWFRAME_EXPECT(executor.submit(kOwner, Priority::Critical, kRecord('c')).has_value());
    RAWFRAME_EXPECT(executor.submit(kOwner, Priority::Normal, kRecord('n')).has_value());
    // The test thread helps, so it decides the order while the worker is held.
    RAWFRAME_EXPECT(executor.runOne());
    RAWFRAME_EXPECT(executor.runOne());
    // Background has waited half the starvation interval: it goes next, ahead
    // of the normal task still queued.
    clock.advance(MonotonicDuration{kMaximumBackgroundStarvationInterval.nanoseconds / 2});
    RAWFRAME_EXPECT(executor.runOne());
    RAWFRAME_EXPECT(executor.runOne());
    RAWFRAME_EXPECT(!executor.runOne());
    RAWFRAME_EXPECT((order == std::vector<char>{'c', 'n', 'b', 'n'}));
    gate.open.store(true);
}

RAWFRAME_TEST(BlockingIsRefusedOnCpuWorkersOnly) {
    RAWFRAME_EXPECT(requireBlockingAllowed().has_value());

    std::atomic<int> cpuRefused{-1};
    std::atomic<int> ioAllowed{-1};
    {
        Executor cpu{oneWorker()};
        Executor io{ExecutorSettings{.kind = ExecutorKind::BlockingIo}};
        RAWFRAME_EXPECT(cpu.admitOwner(kOwner, Quota{.maximumPendingTasks = 1}).has_value());
        RAWFRAME_EXPECT(io.admitOwner(kOwner, Quota{.maximumPendingTasks = 1}).has_value());
        RAWFRAME_EXPECT(cpu.submit(kOwner,
                                   Priority::Normal,
                                   [&cpuRefused]() noexcept {
                                       const auto kStatus = requireBlockingAllowed();
                                       cpuRefused = !kStatus.has_value() &&
                                                    kStatus.error().code() == code(ExecutionError::BlockingOnCpuWorker);
                                   })
                            .has_value());
        RAWFRAME_EXPECT(io.submit(kOwner, Priority::Normal, [&ioAllowed]() noexcept {
                              ioAllowed = requireBlockingAllowed().has_value();
                          }).has_value());
    }
    RAWFRAME_EXPECT(cpuRefused.load() == 1);
    RAWFRAME_EXPECT(ioAllowed.load() == 1);
}

RAWFRAME_TEST(WorkerCountsStayWithinTheirBounds) {
    RAWFRAME_EXPECT(deriveCpuWorkerCount(0) == kMinimumCpuWorkers);
    RAWFRAME_EXPECT(deriveCpuWorkerCount(3) == 3);
    RAWFRAME_EXPECT(deriveCpuWorkerCount(1000) == kMaximumCpuWorkers);
    const std::size_t kParallelism = effectiveParallelism();
    RAWFRAME_EXPECT(kParallelism >= 1);
    RAWFRAME_EXPECT(deriveCpuWorkerCount() == std::max<std::size_t>(kParallelism - 1, 1));

    const Executor kDefaultIo{ExecutorSettings{.kind = ExecutorKind::BlockingIo}};
    const Executor kManyIo{ExecutorSettings{.kind = ExecutorKind::BlockingIo, .workers = 100}};
    const Executor kNoIo{ExecutorSettings{.kind = ExecutorKind::BlockingIo, .workers = 0}};
    RAWFRAME_EXPECT(kDefaultIo.workerCount() == kDefaultBlockingIoWorkers);
    RAWFRAME_EXPECT(kManyIo.workerCount() == kMaximumBlockingIoWorkers);
    RAWFRAME_EXPECT(kNoIo.workerCount() == kMinimumBlockingIoWorkers);
}

RAWFRAME_TEST(CgroupQuotaLinesParse) {
    RAWFRAME_EXPECT(cpusFromCpuMax("max 100000\n") == std::nullopt);
    RAWFRAME_EXPECT(cpusFromCpuMax("200000 100000\n") == 2);
    RAWFRAME_EXPECT(cpusFromCpuMax("150000 100000") == 2);
    RAWFRAME_EXPECT(cpusFromCpuMax("50000 100000") == 1);
    RAWFRAME_EXPECT(cpusFromCpuMax("100000 0") == std::nullopt);
    RAWFRAME_EXPECT(cpusFromCpuMax("garbage") == std::nullopt);
    RAWFRAME_EXPECT(cpusFromCpuMax("") == std::nullopt);
}

#if defined(__linux__)
RAWFRAME_TEST(ParallelismFollowsTheAffinityMask) {
    if (std::thread::hardware_concurrency() < 2) {
        return;
    }
    // A child process, so the narrowed mask does not leak into other tests.
    const auto kOutcome = rawframe::test::runInChild([] {
        cpu_set_t mask;
        CPU_ZERO(&mask);
        CPU_SET(0, &mask);
        CPU_SET(1, &mask);
        if (sched_setaffinity(0, sizeof mask, &mask) != 0) {
            std::_Exit(99);
        }
        std::_Exit(static_cast<int>(effectiveParallelism()));
    });
    RAWFRAME_EXPECT(kOutcome.exitCode == 2);
}
#endif

RAWFRAME_TEST(BudgetsMustNest) {
    const auto kSecond = MonotonicDuration::fromSeconds(1);
    const std::array<MonotonicDuration, 2> kFits{MonotonicDuration::fromMilliseconds(400),
                                                 MonotonicDuration::fromMilliseconds(600)};
    const std::array<MonotonicDuration, 1> kEqual{kSecond};
    const std::array<MonotonicDuration, 2> kOversum{MonotonicDuration::fromMilliseconds(600),
                                                    MonotonicDuration::fromMilliseconds(600)};
    RAWFRAME_EXPECT(checkBudgetNesting(kSecond, kFits).has_value());
    RAWFRAME_EXPECT(
        refusedWith(checkBudgetNesting(kSecond, kEqual), ErrorClass::InvalidArgument, ExecutionError::BudgetNotNested));
    RAWFRAME_EXPECT(refusedWith(
        checkBudgetNesting(kSecond, kOversum), ErrorClass::InvalidArgument, ExecutionError::BudgetNotNested));
    // The executor's own budgets nest inside SPEC-0013's 8 s shutdown bound.
    const std::array<MonotonicDuration, 2> kExecutor{kExecutorDrainBudget, kExecutorJoinBudget};
    RAWFRAME_EXPECT(checkBudgetNesting(MonotonicDuration::fromSeconds(8), kExecutor).has_value());
}

RAWFRAME_TEST(ShutdownOverrunTakesTheFatalPathWithoutFreeingWork) {
    const auto kOutcome = rawframe::test::runInChild([] {
        ManualClock clock;
        Executor executor{oneWorker(&clock)};
        static_cast<void>(executor.admitOwner(kOwner, Quota{.maximumPendingTasks = 1}));
        // Uncooperative work that never finishes.
        static_cast<void>(executor.submit(kOwner, Priority::Normal, []() noexcept {
            for (;;) {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        }));
        std::thread mover{[&clock] {
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
            clock.advance(kExecutorDrainBudget);
        }};
        mover.detach();
        executor.stop();
        std::_Exit(0);
    });
    RAWFRAME_EXPECT(kOutcome.signalled && kOutcome.signal == SIGABRT);
    RAWFRAME_EXPECT(kOutcome.standardError.find("executor shutdown exceeded its budget") != std::string::npos);
}
