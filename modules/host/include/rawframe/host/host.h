#pragma once

#include "rawframe/base/platform.h"
#include "rawframe/composition/configuration.h"
#include "rawframe/composition/declaration.h"
#include "rawframe/composition/held_files.h"
#include "rawframe/composition/host_lifecycle.h"
#include "rawframe/composition/registrar.h"
#include "rawframe/diagnostics/ndjson_sink.h"
#include "rawframe/execution/time.h"
#include "rawframe/result/result.h"

#include <atomic>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string_view>

namespace rawframe::host {

/// Why a Host run ended: SPEC-0012's exit reasons that a Host tells apart
/// (D184). The process exits with `exitCode`, the reason's portable category;
/// the reason itself, more precise, is logged once as the run ends.
enum class HostExit : std::uint8_t {
    /// Stopped on request, or after `host.maximum_iterations`, in order.
    Stopped = 0,
    /// The process was invoked wrongly: an unknown argument, a missing file.
    InvalidInvocation,
    /// A setting was malformed or out of range, the Host's own or a
    /// participant's.
    InvalidLaunchDescriptor,
    /// Content the process was given did not verify or read as what it
    /// claims: a Build, a Composition, a signature.
    IncompatibleArtifact,
    /// Something the start needs could not be had: a file, a resource, a
    /// listener, a secret.
    ResourceUnavailable,
    /// Valid, but not what this process runs: a plan its registrars refuse,
    /// a precondition its configuration leaves unmet.
    UnsupportedConfiguration,
    /// The start was refused for any other reason. Nothing ran; everything
    /// that started was rolled back.
    StartupFailure,
    /// A participant reported the Host unhealthy: it drained and stopped in
    /// order, and a supervisor should not count the run a success.
    RuntimeFailure,
};

/// The reason's SPEC-0012 name, such as `invalid_launch_descriptor`.
[[nodiscard]] std::string_view describe(HostExit exit) noexcept;
/// The reason's portable OS category: 0 success, 64 invocation, 65 data,
/// 69 unavailable, 70 software, 78 configuration.
[[nodiscard]] int exitCode(HostExit exit) noexcept;
/// The reason a start refused with `errorClass` ended for.
[[nodiscard]] HostExit startupExit(result::ErrorClass errorClass) noexcept;

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
    /// The files the Host holds, from which every configured path is read
    /// (D167); null where paths name the file system.
    const composition::HeldFiles* files = nullptr;
};

/// One Host run, driven by its caller one iteration at a time: a process's
/// loop (`runHost`), or a browser's event loop, which calls `iterate` from
/// each frame and never sleeps (D168). Constructing it composes and starts;
/// a refused start has already stopped and logged.
class Host {
public:
    explicit Host(const HostRequest& request);
    Host(const Host&) = delete;
    Host& operator=(const Host&) = delete;
    /// Stops if `stop` was not called.
    ~Host();

    /// Runs one iteration of the Host schedule. False once the run has
    /// ended (a refused start, the iteration bound, or a drain over), after
    /// which the Host has stopped. Without threads it also runs the work
    /// its executors were given.
    [[nodiscard]] bool iterate() noexcept;
    /// When the next iteration is due, for a caller pacing the run; one
    /// called early is allowed and runs at once.
    [[nodiscard]] execution::MonotonicInstant due() const noexcept;
    /// Ends the run where it is: drains, stops, and says how it ended.
    /// Idempotent.
    HostExit stop() noexcept;

private:
    struct State;
    std::unique_ptr<State> state_;
    std::optional<HostExit> ended_;
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
///   host.supervisor_grace_ms   the grace a supervisor gives a stop; the
///                              drain, the shutdown budget, and the executors'
///                              budgets together, plus a tenth of it (at least
///                              a second), must fit (none)
///   diagnostics.minimum_severity  trace, debug, info, warning, error, critical (info)
///
/// It paces by sleeping, so it exists only where there are threads.
#if RAWFRAME_THREADS
[[nodiscard]] HostExit runHost(const HostRequest& request) noexcept;
#endif

} // namespace rawframe::host
