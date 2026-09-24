#pragma once

#include "rawframe/composition/configuration.h"
#include "rawframe/composition/declaration.h"
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
};

/// Where diagnostics go: a writer for finished NDJSON bytes, called on the
/// Host thread in the `maintenance` phase and at shutdown.
struct LogDestination {
    diagnostics::WriteBytes write = nullptr;
    void* context = nullptr;
};

struct HostRequest {
    composition::TargetRole role = composition::TargetRole::DedicatedServer;
    composition::Platform platform = composition::Platform::Linux;
    std::span<const composition::RegistrarEntry> registrars;
    const composition::Configuration* configuration = nullptr;
    LogDestination log;
    /// Set from any thread (a signal handler included) to ask for an orderly
    /// stop at the end of the current iteration.
    const std::atomic<bool>* stopRequested = nullptr;
};

/// Runs one Host: owns the monotonic clock, diagnostic routing and its NDJSON
/// sink, the CPU and blocking-I/O executors, and the root cancellation scope;
/// composes the plan; starts it; runs the Host schedule until asked to stop;
/// then stops the composition, the executors, and drains the sink, in that
/// order (SPEC-0005 Host). Configuration keys:
///
///   host.iteration_rate        iterations per second (120)
///   host.maximum_iterations    stop after this many; 0 runs until stopped (0)
///   host.cpu_workers           explicit CPU worker count (derived)
///   host.io_workers            blocking-I/O workers (2)
///   host.shutdown_budget_ms    the composition's stop budget (5000)
///   diagnostics.minimum_severity  trace, debug, info, warning, error, critical (info)
[[nodiscard]] HostExit runHost(const HostRequest& request) noexcept;

} // namespace rawframe::host
