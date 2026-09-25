#pragma once

#include "rawframe/diagnostics/emitter.h"
#include "rawframe/execution/bounds.h"
#include "rawframe/execution/task.h"
#include "rawframe/execution/time.h"
#include "rawframe/result/result.h"

#include <array>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <optional>
#include <thread>
#include <vector>

namespace rawframe::execution {

/// SPEC-0005's baseline priorities. Priority never defines gameplay order.
enum class Priority : std::uint8_t {
    Critical,
    Normal,
    Background
};

/// A Host has at most one of each (SPEC-0005 executor topology).
enum class ExecutorKind : std::uint8_t {
    Cpu,
    BlockingIo
};

/// Who submits work. Supplied by the caller; the executor only accounts for it.
struct OwnerId {
    std::uint64_t value = 0;
    friend constexpr bool operator==(const OwnerId&, const OwnerId&) noexcept = default;
};

/// An owner's accepted admission. There is no default quota: an owner the
/// executor has not admitted cannot submit (SPEC-0048 quotas).
struct Quota {
    /// Tasks this owner may have waiting at once, independent of priority.
    std::size_t maximumPendingTasks = 0;
    /// `critical` is for trusted owners only; scripts and packages never get it.
    bool mayUseCritical = false;
};

/// Service progress, for a watchdog: work that waits or runs, and how many
/// tasks have finished. Work present while the count stands still is a
/// stall (SPEC-0012 health evidence).
struct ExecutorProgress {
    std::size_t waiting = 0;
    std::size_t running = 0;
    std::uint64_t completed = 0;
};

struct ExecutorSettings {
    ExecutorKind kind = ExecutorKind::Cpu;
    /// Explicit worker count from the target or profile, clamped to the kind's
    /// bounds. Absent: CPU executors derive it from effective parallelism and
    /// blocking-I/O executors use kDefaultBlockingIoWorkers.
    std::optional<std::size_t> workers;
    /// Measures the drain and join budgets and background starvation.
    const MonotonicSource* clock = nullptr;
    /// Receives the one boundary diagnostic if shutdown overruns its budget.
    diagnostics::Emitter emitter;
};

/// A fixed pool of workers over one bounded queue. Submission never blocks and
/// never allocates; a full queue refuses. Workers are the only threads this
/// module starts, and nothing else in the engine starts threads (ADR-0010).
class Executor {
public:
    explicit Executor(const ExecutorSettings& settings);
    Executor(const Executor&) = delete;
    Executor& operator=(const Executor&) = delete;
    /// Stops if `stop` was not called.
    ~Executor();

    /// Accepts an owner with its quota. Fails if the owner is already admitted,
    /// the quota is zero, or kMaximumQuotaOwners owners are admitted.
    [[nodiscard]] result::Status admitOwner(OwnerId owner, Quota quota);

    /// Withdraws an owner's quota, so its identity can be admitted again later.
    /// Fails if it has tasks waiting, or was never admitted.
    [[nodiscard]] result::Status retireOwner(OwnerId owner);

    /// Queues a task. Fails typed, and queues nothing, when admission is closed
    /// (`unavailable`), the owner has no quota or asks for a priority it may not
    /// use (`permission_denied`), or the owner's quota or the queue is full
    /// (`resource_exhausted`).
    [[nodiscard]] result::Status submit(OwnerId owner, Priority priority, Task&& task);

    /// Runs one queued task on the calling thread if there is one. This is the
    /// worker-safe wait: a thread waiting on child work helps instead of
    /// blocking the pool.
    bool runOne() noexcept;

    /// Closes admission, lets the workers drain what was accepted within
    /// kExecutorDrainBudget, and joins them within kExecutorJoinBudget. Work
    /// that overruns is never abandoned while it may still run: the executor
    /// reports it and takes the fatal path instead. Idempotent.
    void stop() noexcept;

    [[nodiscard]] ExecutorKind kind() const noexcept {
        return kind_;
    }
    [[nodiscard]] std::size_t workerCount() const noexcept {
        return workers_.size();
    }
    [[nodiscard]] std::size_t pendingTasks() const noexcept;
    [[nodiscard]] ExecutorProgress progress() const noexcept;

    /// The executor whose worker is the calling thread, or null.
    [[nodiscard]] static const Executor* current() noexcept;

private:
    static constexpr std::uint32_t kNone = UINT32_MAX;
    static constexpr std::size_t kPriorityCount = 3;

    struct Slot {
        Task task;
        std::uint32_t owner = 0; // index into owners_
        std::uint32_t next = kNone;
    };

    struct Queue {
        std::uint32_t head = kNone;
        std::uint32_t tail = kNone;
        std::size_t size = 0;
    };

    struct OwnerEntry {
        OwnerId id;
        Quota quota;
        std::size_t pending = 0;
        bool active = false; // a retired entry is reused by the next admission
    };

    /// The active entry for `owner`, or ownerCount_. Requires mutex_.
    [[nodiscard]] std::size_t findOwnerLocked(OwnerId owner) const noexcept;
    void workerLoop() noexcept;
    /// Takes the next task by priority, with background promoted once it has
    /// waited half the starvation interval. Requires mutex_.
    [[nodiscard]] std::optional<Slot> popLocked() noexcept;
    void runSlot(Slot& slot) noexcept;
    [[noreturn]] void overrun(const char* phase) noexcept;

    ExecutorKind kind_;
    const MonotonicSource* clock_;
    diagnostics::Emitter emitter_;

    mutable std::mutex mutex_;
    std::condition_variable workAvailable_;
    std::condition_variable idle_;
    std::vector<Slot> slots_; // kMaximumPendingTasksPerExecutor, allocated once
    std::uint32_t freeHead_ = kNone;
    std::array<Queue, kPriorityCount> queues_{};
    std::size_t pending_ = 0;
    std::size_t running_ = 0;
    std::uint64_t completed_ = 0;
    std::optional<MonotonicInstant> backgroundWaitingSince_;
    std::array<OwnerEntry, kMaximumQuotaOwners> owners_{};
    std::size_t ownerCount_ = 0;
    bool admissionClosed_ = false;
    bool stopping_ = false;
    bool stopped_ = false;
    std::size_t exitedWorkers_ = 0;

    std::vector<std::thread> workers_;
};

/// Blocking I/O is allowed on this thread: `failed_precondition` on a CPU
/// worker, where it could stall the pool (SPEC-0005). Every blocking call site
/// in the engine asks first.
[[nodiscard]] result::Status requireBlockingAllowed();

} // namespace rawframe::execution
