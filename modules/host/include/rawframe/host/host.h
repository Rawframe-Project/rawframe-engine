#pragma once

#include "rawframe/composition/configuration.h"
#include "rawframe/composition/declaration.h"
#include "rawframe/composition/host_lifecycle.h"
#include "rawframe/composition/registrar.h"
#include "rawframe/diagnostics/ndjson_sink.h"
#include "rawframe/result/result.h"

#include <atomic>
#include <cstdint>
#include <span>
#include <string_view>

namespace rawframe::host {

/// How a Host run ended, for the process's exit code.
enum class HostExit : std::uint8_t {
    /// Stopped on request, or after `host.maximum_iterations`, in order.
    Stopped = 0,
    /// The configuration, plan, or a participant's start was refused. Nothing
    /// ran; everything that started was rolled back.
    StartupFailed = 1,
    /// A participant reported the Host unhealthy: it drained and stopped in
    /// order, and a supervisor should not count the run a success.
    Unhealthy = 2,
};

/// Where diagnostics go: a writer for finished NDJSON bytes, called on the
/// Host thread in the `maintenance` phase and at shutdown.
struct LogDestination {
    diagnostics::WriteBytes write = nullptr;
    void* context = nullptr;
};

/// SPEC-0012's typed snapshot at the Host boundary: what an adapter for a
/// supervisor or a listing reads, instead of parsing the log.
struct HostStatus {
    composition::HostState state = composition::HostState::Starting;
    composition::Health health = composition::Health::Healthy;
    /// The health's fixed category, such as `tick_failed`; empty while healthy.
    std::string_view reason;
    /// Admitted connections the participants serve.
    std::size_t connections = 0;
    std::uint64_t iteration = 0;
};

/// Told, on the Host thread, each time the state, health, or connections
/// change. It must return promptly and must not call back into the Host.
struct StatusObserver {
    void (*publish)(const HostStatus& status, void* context) noexcept = nullptr;
    void* context = nullptr;
};

struct HostRequest {
    composition::TargetRole role = composition::TargetRole::DedicatedServer;
    composition::Platform platform = composition::Platform::Linux;
    std::span<const composition::RegistrarEntry> registrars;
    const composition::Configuration* configuration = nullptr;
    LogDestination log;
    /// Set from any thread (a signal handler included) to ask for an orderly
    /// stop: the Host drains, then stops. Setting it again changes nothing.
    const std::atomic<bool>* stopRequested = nullptr;
    StatusObserver status;
};

/// Runs one Host: owns the monotonic clock, diagnostic routing and its NDJSON
/// sink, the CPU and blocking-I/O executors, and the root cancellation scope;
/// composes the plan; starts it; runs the Host schedule until asked to stop;
/// drains; then stops the composition, the executors, and drains the sink,
/// in that order (SPEC-0005 Host). It moves through SPEC-0012's lifecycle,
/// logging each state, activates at once, and drains on a stop request or an
/// unhealthy report. Health is the worst of the participants' reports and the
/// Host's own watch on its executors: work present while none finishes is
/// degraded after half of `host.stall_ms` and unhealthy after all of it.
/// Configuration keys:
///
///   host.iteration_rate        iterations per second (120)
///   host.maximum_iterations    stop after this many; 0 runs until stopped (0)
///   host.cpu_workers           explicit CPU worker count (derived)
///   host.io_workers            blocking-I/O workers (2)
///   host.shutdown_budget_ms    the composition's stop budget (5000)
///   host.drain_ms              longest a drain waits for connections (5000)
///   host.stall_ms              an executor stall that is unhealthy (10000)
///   diagnostics.minimum_severity  trace, debug, info, warning, error, critical (info)
[[nodiscard]] HostExit runHost(const HostRequest& request) noexcept;

} // namespace rawframe::host
