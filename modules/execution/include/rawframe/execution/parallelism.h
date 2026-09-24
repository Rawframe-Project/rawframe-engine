#pragma once

#include <cstddef>
#include <optional>
#include <string_view>

namespace rawframe::execution {

/// How many CPUs this process may actually use: the affinity mask, reduced by a
/// cgroup v2 `cpu.max` quota on Linux, or the process affinity mask on Windows.
/// Never the host-wide count alone (SPEC-0048 CPU worker derivation). At
/// least 1.
[[nodiscard]] std::size_t effectiveParallelism() noexcept;

/// The CPU worker count: `explicitCount` if the target or profile supplied one,
/// otherwise `effectiveParallelism() - kReservedSchedulingThreads`, clamped to
/// [kMinimumCpuWorkers, kMaximumCpuWorkers] either way.
[[nodiscard]] std::size_t deriveCpuWorkerCount(std::optional<std::size_t> explicitCount = std::nullopt) noexcept;

/// The CPUs a cgroup v2 `cpu.max` line allows, rounded up, or nothing for
/// `max`, a malformed line, or a zero period. Exposed for tests.
[[nodiscard]] std::optional<std::size_t> cpusFromCpuMax(std::string_view line) noexcept;

} // namespace rawframe::execution
