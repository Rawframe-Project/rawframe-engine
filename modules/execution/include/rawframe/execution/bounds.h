#pragma once

// The SPEC-0048 bounds, in the one owned header that specification requires.
// Every value is a hard ceiling or a contract constant, never a performance
// target. No use site restates any of them as a literal.

#include "rawframe/execution/time.h"

#include <cstddef>

namespace rawframe::execution {

// CPU worker derivation.
inline constexpr std::size_t kMinimumCpuWorkers = 1;
inline constexpr std::size_t kMaximumCpuWorkers = 64;
inline constexpr std::size_t kReservedSchedulingThreads = 1; // the thread driving the Host schedule

// Blocking-I/O workers: bounded by the device, not derived from the CPU count.
inline constexpr std::size_t kMinimumBlockingIoWorkers = 1;
inline constexpr std::size_t kMaximumBlockingIoWorkers = 8;
inline constexpr std::size_t kDefaultBlockingIoWorkers = 2;

// The foundation admits zero dedicated service loops; this caps later gates.
inline constexpr std::size_t kMaximumAdmittedDedicatedLoops = 4;

// Capacities.
inline constexpr std::size_t kMaximumPendingTasksPerExecutor = 4096;
inline constexpr std::size_t kMaximumInFlightAsyncOperationsPerScope = 1024;
inline constexpr std::size_t kMaximumCancellationScopeDepth = 32;

// Progress and shutdown.
inline constexpr MonotonicDuration kMaximumBackgroundStarvationInterval = MonotonicDuration::fromMilliseconds(100);
inline constexpr MonotonicDuration kExecutorDrainBudget = MonotonicDuration::fromSeconds(1);
inline constexpr MonotonicDuration kExecutorJoinBudget = MonotonicDuration::fromSeconds(2);
// kDefaultOperationDeadline is deliberately absent: deadlines are explicit or
// there is none.

// Implementation bounds this module adds (work/decisions.md D15). They keep
// storage finite and are not SPEC-0048 authority values.

/// Owners with an accepted quota on one executor.
inline constexpr std::size_t kMaximumQuotaOwners = 64;
/// Bytes a task's callable may occupy inline. A task owning more state holds it
/// behind one pointer.
inline constexpr std::size_t kTaskInlineBytes = 48;

} // namespace rawframe::execution
