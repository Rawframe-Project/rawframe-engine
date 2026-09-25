#include "rawframe/execution/executor.h"

#include "rawframe/base/assert.h"
#include "rawframe/base/fatal.h"
#include "rawframe/base/threads.h"
#include "rawframe/execution/errors.h"
#include "rawframe/execution/parallelism.h"

#include <algorithm>
#include <chrono>

namespace rawframe::execution {

namespace {

// Set on each worker thread for its lifetime.
thread_local const Executor* currentExecutor = nullptr;

// How long a stopping thread sleeps between reading the budget clock. Short
// enough to notice a budget promptly, long enough not to spin.
constexpr auto kStopPollInterval = std::chrono::milliseconds(1);

constexpr diagnostics::EventIdentity kShutdownOverrun{"execution", "shutdown_overrun"};

#if RAWFRAME_THREADS
std::size_t workerCountFor(const ExecutorSettings& settings) noexcept {
    if (settings.kind == ExecutorKind::BlockingIo) {
        return std::clamp(
            settings.workers.value_or(kDefaultBlockingIoWorkers), kMinimumBlockingIoWorkers, kMaximumBlockingIoWorkers);
    }
    return deriveCpuWorkerCount(settings.workers);
}
#endif

const MonotonicSource& defaultClock() noexcept {
    static const SteadyClock kClock;
    return kClock;
}

} // namespace

Executor::Executor(const ExecutorSettings& settings)
    : kind_(settings.kind), clock_(settings.clock != nullptr ? settings.clock : &defaultClock()),
      emitter_(settings.emitter), slots_(kMaximumPendingTasksPerExecutor) {
    for (std::size_t index = 0; index < slots_.size(); ++index) {
        slots_[index].next = index + 1 < slots_.size() ? static_cast<std::uint32_t>(index + 1) : kNone;
    }
    freeHead_ = 0;
#if RAWFRAME_THREADS
    const std::size_t kWorkers = workerCountFor(settings);
    workers_.reserve(kWorkers);
    for (std::size_t index = 0; index < kWorkers; ++index) {
        workers_.emplace_back([this] {
            workerLoop();
        });
    }
#endif
}

Executor::~Executor() {
    stop();
}

std::size_t Executor::findOwnerLocked(OwnerId owner) const noexcept {
    for (std::size_t index = 0; index < ownerCount_; ++index) {
        if (owners_[index].active && owners_[index].id == owner) {
            return index;
        }
    }
    return ownerCount_;
}

result::Status Executor::admitOwner(OwnerId owner, Quota quota) {
    const std::lock_guard kLock{mutex_};
    if (quota.maximumPendingTasks == 0) {
        return result::fail(result::ErrorClass::InvalidArgument,
                            kExecutionDomain,
                            code(ExecutionError::ZeroQuota),
                            "a quota admits at least one pending task");
    }
    if (findOwnerLocked(owner) != ownerCount_) {
        return result::fail(result::ErrorClass::AlreadyExists,
                            kExecutionDomain,
                            code(ExecutionError::OwnerAlreadyAdmitted),
                            "the owner already has a quota on this executor");
    }
    std::size_t index = 0;
    while (index < ownerCount_ && owners_[index].active) {
        ++index;
    }
    if (index == owners_.size()) {
        return result::fail(result::ErrorClass::ResourceExhausted,
                            kExecutionDomain,
                            code(ExecutionError::QuotaTableFull),
                            "kMaximumQuotaOwners owners are already admitted");
    }
    owners_[index] = OwnerEntry{.id = owner, .quota = quota, .pending = 0, .active = true};
    ownerCount_ = std::max(ownerCount_, index + 1);
    return {};
}

result::Status Executor::retireOwner(OwnerId owner) {
    const std::lock_guard kLock{mutex_};
    const std::size_t kIndex = findOwnerLocked(owner);
    if (kIndex == ownerCount_) {
        return result::fail(result::ErrorClass::NotFound,
                            kExecutionDomain,
                            code(ExecutionError::OwnerHasNoQuota),
                            "the owner has no quota to retire");
    }
    if (owners_[kIndex].pending != 0) {
        return result::fail(result::ErrorClass::FailedPrecondition,
                            kExecutionDomain,
                            code(ExecutionError::OwnerHasPendingWork),
                            "the owner still has tasks waiting");
    }
    owners_[kIndex].active = false;
    return {};
}

result::Status Executor::submit(OwnerId owner, Priority priority, Task&& task) {
    RAWFRAME_ASSERT(static_cast<bool>(task), "submitting an empty task");
    {
        const std::lock_guard kLock{mutex_};
        if (admissionClosed_) {
            return result::fail(result::ErrorClass::Unavailable,
                                kExecutionDomain,
                                code(ExecutionError::AdmissionClosed),
                                "the executor is stopping");
        }
        const std::size_t kOwnerIndex = findOwnerLocked(owner);
        if (kOwnerIndex == ownerCount_) {
            return result::fail(result::ErrorClass::PermissionDenied,
                                kExecutionDomain,
                                code(ExecutionError::OwnerHasNoQuota),
                                "the owner has no accepted quota");
        }
        OwnerEntry& entry = owners_[kOwnerIndex];
        if (priority == Priority::Critical && !entry.quota.mayUseCritical) {
            return result::fail(result::ErrorClass::PermissionDenied,
                                kExecutionDomain,
                                code(ExecutionError::PriorityNotPermitted),
                                "the owner may not submit critical work");
        }
        if (entry.pending == entry.quota.maximumPendingTasks) {
            return result::fail(result::ErrorClass::ResourceExhausted,
                                kExecutionDomain,
                                code(ExecutionError::OwnerQuotaExhausted),
                                "the owner's pending-task quota is full");
        }
        if (freeHead_ == kNone) {
            return result::fail(result::ErrorClass::ResourceExhausted,
                                kExecutionDomain,
                                code(ExecutionError::QueueFull),
                                "kMaximumPendingTasksPerExecutor tasks are already pending");
        }

        const std::uint32_t kIndex = freeHead_;
        Slot& slot = slots_[kIndex];
        freeHead_ = slot.next;
        slot.task = std::move(task);
        slot.owner = static_cast<std::uint32_t>(kOwnerIndex);
        slot.next = kNone;

        Queue& queue = queues_[static_cast<std::size_t>(priority)];
        if (queue.tail == kNone) {
            queue.head = kIndex;
        } else {
            slots_[queue.tail].next = kIndex;
        }
        queue.tail = kIndex;
        ++queue.size;
        if (priority == Priority::Background && queue.size == 1) {
            backgroundWaitingSince_ = clock_->now();
        }
        ++entry.pending;
        ++pending_;
    }
    workAvailable_.notify_one();
    return {};
}

std::optional<Executor::Slot> Executor::popLocked() noexcept {
    if (pending_ == 0) {
        return std::nullopt;
    }
    Queue& background = queues_[static_cast<std::size_t>(Priority::Background)];
    std::size_t chosen = kPriorityCount;
    // Promote background at half the interval, so the other half is left for a
    // worker to become free and actually begin it.
    if (background.size != 0 && backgroundWaitingSince_ &&
        (clock_->now() - *backgroundWaitingSince_).nanoseconds * 2 >=
            kMaximumBackgroundStarvationInterval.nanoseconds) {
        chosen = static_cast<std::size_t>(Priority::Background);
    } else {
        for (std::size_t index = 0; index < kPriorityCount; ++index) {
            if (queues_[index].size != 0) {
                chosen = index;
                break;
            }
        }
    }
    Queue& queue = queues_[chosen];
    const std::uint32_t kIndex = queue.head;
    Slot& slot = slots_[kIndex];
    queue.head = slot.next;
    if (queue.head == kNone) {
        queue.tail = kNone;
    }
    --queue.size;
    if (chosen == static_cast<std::size_t>(Priority::Background)) {
        // The next background task waits from now.
        backgroundWaitingSince_ = queue.size != 0 ? std::optional{clock_->now()} : std::nullopt;
    }

    Slot taken{.task = std::move(slot.task), .owner = slot.owner, .next = kNone};
    slot.next = freeHead_;
    freeHead_ = kIndex;
    --owners_[taken.owner].pending;
    --pending_;
    ++running_;
    return taken;
}

void Executor::runSlot(Slot& slot) noexcept {
    slot.task();
    slot.task = Task{};
    bool nowIdle = false;
    {
        const std::lock_guard kLock{mutex_};
        --running_;
        ++completed_;
        nowIdle = running_ == 0 && pending_ == 0;
    }
    if (nowIdle) {
        idle_.notify_all();
    }
}

bool Executor::runOne() noexcept {
    std::optional<Slot> slot;
    {
        const std::lock_guard kLock{mutex_};
        slot = popLocked();
    }
    if (!slot) {
        return false;
    }
    runSlot(*slot);
    return true;
}

#if RAWFRAME_THREADS
void Executor::workerLoop() noexcept {
    currentExecutor = this;
    for (;;) {
        std::optional<Slot> slot;
        {
            std::unique_lock lock{mutex_};
            workAvailable_.wait(lock, [this] {
                return pending_ != 0 || stopping_;
            });
            slot = popLocked();
            if (!slot) {
                // Stopping and nothing left.
                ++exitedWorkers_;
                idle_.notify_all();
                return;
            }
        }
        runSlot(*slot);
    }
}
#endif

void Executor::stop() noexcept {
    {
        const std::lock_guard kLock{mutex_};
        if (stopped_) {
            return;
        }
        stopped_ = true;
        admissionClosed_ = true;
    }

    // Drain: accepted work finishes, within its budget.
    const MonotonicInstant kDrainDeadline = clock_->now() + kExecutorDrainBudget;
    {
        std::unique_lock lock{mutex_};
        while (pending_ != 0 || running_ != 0) {
            if (clock_->now() >= kDrainDeadline) {
                lock.unlock();
                overrun("drain");
            }
#if !RAWFRAME_THREADS
            // No worker will: the stopping thread runs what was accepted.
            if (pending_ != 0) {
                lock.unlock();
                runOne();
                lock.lock();
                continue;
            }
#endif
            idle_.wait_for(lock, kStopPollInterval);
        }
        stopping_ = true;
    }
    workAvailable_.notify_all();
#if RAWFRAME_THREADS

    // Join: the workers see stopping_ and leave, within their budget.
    const MonotonicInstant kJoinDeadline = clock_->now() + kExecutorJoinBudget;
    {
        std::unique_lock lock{mutex_};
        while (exitedWorkers_ != workers_.size()) {
            if (clock_->now() >= kJoinDeadline) {
                lock.unlock();
                overrun("join");
            }
            idle_.wait_for(lock, kStopPollInterval);
        }
    }
    for (std::thread& worker : workers_) {
        worker.join();
    }
    workers_.clear();
#endif
}

void Executor::overrun(const char* phase) noexcept {
    // Uncooperative work may still be running on a worker and still reference
    // code and owner state, so nothing is freed or unloaded: one boundary
    // diagnostic, then the fatal path (SPEC-0005 shutdown).
    emitter_.log(diagnostics::Severity::Critical,
                 kShutdownOverrun,
                 "executor shutdown exceeded its budget",
                 {diagnostics::field("phase", phase)});
    RAWFRAME_PANIC("executor shutdown exceeded its budget");
}

std::size_t Executor::pendingTasks() const noexcept {
    const std::lock_guard kLock{mutex_};
    return pending_;
}

ExecutorProgress Executor::progress() const noexcept {
    const std::lock_guard kLock{mutex_};
    return ExecutorProgress{.waiting = pending_, .running = running_, .completed = completed_};
}

const Executor* Executor::current() noexcept {
    return currentExecutor;
}

result::Status requireBlockingAllowed() {
    const Executor* executor = Executor::current();
    if (executor != nullptr && executor->kind() == ExecutorKind::Cpu) {
        return result::fail(result::ErrorClass::FailedPrecondition,
                            kExecutionDomain,
                            code(ExecutionError::BlockingOnCpuWorker),
                            "blocking I/O on a CPU worker; use the blocking-I/O executor");
    }
    return {};
}

} // namespace rawframe::execution
