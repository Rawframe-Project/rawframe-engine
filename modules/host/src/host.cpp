#include "rawframe/host/host.h"

#include "rawframe/composition/composition.h"
#include "rawframe/composition/plan.h"
#include "rawframe/diagnostics/emitter.h"
#include "rawframe/diagnostics/router.h"
#include "rawframe/execution/bounds.h"
#include "rawframe/execution/cancellation.h"
#include "rawframe/execution/executor.h"
#include "rawframe/execution/time.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#if defined(__unix__) || defined(__APPLE__)
#include <unistd.h>
#endif

namespace rawframe::host {

namespace {

using diagnostics::EventIdentity;
using diagnostics::Severity;

constexpr EventIdentity kBadConfiguration{"host", "bad_configuration"};
constexpr EventIdentity kPlanProblem{"host", "plan_problem"};
constexpr EventIdentity kPlanRefused{"host", "plan_refused"};
constexpr EventIdentity kStartFailed{"host", "start_failed"};
constexpr EventIdentity kStarted{"host", "started"};
constexpr EventIdentity kStopping{"host", "stopping"};
constexpr EventIdentity kStopped{"host", "stopped"};
constexpr EventIdentity kLifecycle{"host", "lifecycle"};
constexpr EventIdentity kHealth{"host", "health"};

// The sink's buffer, allocated once. At 120 iterations per second a drain
// happens every 8 ms, so this holds far more than one iteration writes.
constexpr std::size_t kLogBufferBytes = 1024 * 1024;

std::int64_t wallNanoseconds() noexcept {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::system_clock::now().time_since_epoch())
        .count();
}

bool discard(void*, std::span<const char>) noexcept {
    return true;
}

std::string_view roleName(composition::TargetRole role) noexcept {
    switch (role) {
    case composition::TargetRole::DedicatedServer:
        return "dedicated_server";
    case composition::TargetRole::Client:
        return "client";
    case composition::TargetRole::Tool:
        return "tool";
    case composition::TargetRole::Test:
        return "test";
    }
    return "test";
}

std::string processIdentity() {
#if defined(__unix__) || defined(__APPLE__)
    return "pid-" + std::to_string(static_cast<long long>(::getpid()));
#else
    return "process";
#endif
}

std::optional<Severity> severityNamed(std::string_view name) noexcept {
    constexpr std::array<std::pair<std::string_view, Severity>, 6> kNames = {{
        {"trace", Severity::Trace},
        {"debug", Severity::Debug},
        {"info", Severity::Info},
        {"warning", Severity::Warning},
        {"error", Severity::Error},
        {"critical", Severity::Critical},
    }};
    for (const auto& [text, severity] : kNames) {
        if (text == name) {
            return severity;
        }
    }
    return std::nullopt;
}

struct Settings {
    std::uint64_t iterationRate = 120;
    std::uint64_t maximumIterations = 0;
    std::optional<std::size_t> cpuWorkers;
    std::size_t ioWorkers = execution::kDefaultBlockingIoWorkers;
    execution::MonotonicDuration shutdownBudget = execution::MonotonicDuration::fromSeconds(5);
    execution::MonotonicDuration drain = execution::MonotonicDuration::fromSeconds(5);
    execution::MonotonicDuration stall = execution::MonotonicDuration::fromSeconds(10);
    std::optional<execution::MonotonicDuration> supervisorGrace;
    Severity minimumSeverity = Severity::Info;
};

/// The longest a stop can take: the drain, the composition's stop, then the
/// blocking-I/O and CPU executors each draining and joining.
execution::MonotonicDuration shutdownBound(const Settings& settings) noexcept {
    const execution::MonotonicDuration kExecutor = execution::kExecutorDrainBudget + execution::kExecutorJoinBudget;
    return settings.drain + settings.shutdownBudget + kExecutor + kExecutor;
}

/// Reads the host keys. Returns the key at fault on failure.
std::optional<std::string_view> readSettings(const composition::Configuration& configuration, Settings& settings) {
    const auto kRate = configuration.unsignedInteger("host.iteration_rate", settings.iterationRate);
    if (!kRate.has_value() || *kRate == 0 || *kRate > 10'000) {
        return "host.iteration_rate";
    }
    settings.iterationRate = *kRate;
    const auto kMaximum = configuration.unsignedInteger("host.maximum_iterations", 0);
    if (!kMaximum.has_value()) {
        return "host.maximum_iterations";
    }
    settings.maximumIterations = *kMaximum;
    if (configuration.text("host.cpu_workers")) {
        const auto kWorkers = configuration.unsignedInteger("host.cpu_workers", 0);
        if (!kWorkers.has_value()) {
            return "host.cpu_workers";
        }
        settings.cpuWorkers = static_cast<std::size_t>(*kWorkers);
    }
    const auto kIo = configuration.unsignedInteger("host.io_workers", settings.ioWorkers);
    if (!kIo.has_value()) {
        return "host.io_workers";
    }
    settings.ioWorkers = static_cast<std::size_t>(*kIo);
    const auto kBudget = configuration.unsignedInteger("host.shutdown_budget_ms", 5000);
    if (!kBudget.has_value() || *kBudget == 0 || *kBudget > 600'000) {
        return "host.shutdown_budget_ms";
    }
    settings.shutdownBudget = execution::MonotonicDuration::fromMilliseconds(static_cast<std::int64_t>(*kBudget));
    const auto kDrain = configuration.unsignedInteger("host.drain_ms", 5000);
    if (!kDrain.has_value() || *kDrain > 600'000) {
        return "host.drain_ms";
    }
    settings.drain = execution::MonotonicDuration::fromMilliseconds(static_cast<std::int64_t>(*kDrain));
    const auto kStall = configuration.unsignedInteger("host.stall_ms", 10'000);
    if (!kStall.has_value() || *kStall < 100 || *kStall > 600'000) {
        return "host.stall_ms";
    }
    settings.stall = execution::MonotonicDuration::fromMilliseconds(static_cast<std::int64_t>(*kStall));
    if (configuration.text("host.supervisor_grace_ms")) {
        const auto kGrace = configuration.unsignedInteger("host.supervisor_grace_ms", 0);
        if (!kGrace.has_value() || *kGrace > 3'600'000) {
            return "host.supervisor_grace_ms";
        }
        settings.supervisorGrace = execution::MonotonicDuration::fromMilliseconds(static_cast<std::int64_t>(*kGrace));
    }
    if (settings.supervisorGrace) {
        // SPEC-0012: every shutdown phase together stays under the grace a
        // supervisor gives, with a margin of a tenth of it, at least a second.
        const execution::MonotonicDuration kMargin =
            std::max(execution::MonotonicDuration::fromSeconds(1),
                     execution::MonotonicDuration{settings.supervisorGrace->nanoseconds / 10});
        if (shutdownBound(settings) + kMargin > *settings.supervisorGrace) {
            return "host.supervisor_grace_ms";
        }
    }
    if (const auto kSeverity = configuration.text("diagnostics.minimum_severity")) {
        const auto kParsed = severityNamed(*kSeverity);
        if (!kParsed) {
            return "diagnostics.minimum_severity";
        }
        settings.minimumSeverity = *kParsed;
    }
    return std::nullopt;
}

/// SPEC-0012's executor progress evidence: work waits or runs while no task
/// finishes. A long task alone is a stall too; nothing a Host runs should
/// hold a worker for seconds.
class StallWatch {
public:
    explicit StallWatch(execution::MonotonicInstant start) noexcept : since_(start) {
    }

    composition::Health check(const execution::ExecutorProgress& progress,
                              execution::MonotonicInstant now,
                              execution::MonotonicDuration limit) noexcept {
        if (progress.waiting + progress.running == 0 || progress.completed != completed_) {
            completed_ = progress.completed;
            since_ = now;
            return composition::Health::Healthy;
        }
        const std::int64_t kStalled = (now - since_).nanoseconds;
        if (kStalled >= limit.nanoseconds) {
            return composition::Health::Unhealthy;
        }
        return kStalled * 2 >= limit.nanoseconds ? composition::Health::Degraded : composition::Health::Healthy;
    }

private:
    std::uint64_t completed_ = 0;
    execution::MonotonicInstant since_;
};

} // namespace

HostExit runHost(const HostRequest& request) noexcept {
    static const composition::Configuration kEmpty;
    const composition::Configuration& configuration =
        request.configuration != nullptr ? *request.configuration : kEmpty;
    const diagnostics::WriteBytes kWrite = request.log.write != nullptr ? request.log.write : &discard;
    const execution::SteadyClock clock;

    // Diagnostics first, so everything after can report. The sink's buffer is
    // the only allocation on the log path.
    const std::string kProcess = processIdentity();
    diagnostics::NdjsonSink sink{diagnostics::StreamInfo{.processIdentity = kProcess,
                                                         .processStartWallUnixNanoseconds = wallNanoseconds(),
                                                         .buildReceipt = "unreleased",
                                                         .profile = roleName(request.role),
                                                         .configuration = RAWFRAME_CONFIGURATION_NAME,
                                                         .targetRole = roleName(request.role),
                                                         .session = ""},
                                 // A local log is operator-owned, so it may hold personal data; a sink
                                 // leaving the machine would declare a lower clearance (ADR-0063).
                                 diagnostics::Sensitivity::Personal,
                                 kLogBufferBytes};
    const auto kDrain = [&] {
        static_cast<void>(sink.drain(kWrite, request.log.context));
    };

    Settings settings;
    const auto kBadKey = readSettings(configuration, settings);
    diagnostics::Sink* const kSinks[] = {&sink};
    diagnostics::Router router{diagnostics::RouterSettings{.minimumSeverity = settings.minimumSeverity,
                                                           .monotonic = &diagnostics::steadyClockNanoseconds,
                                                           .wall = &wallNanoseconds},
                               kSinks};
    const diagnostics::Emitter kEmitter = router.emitter();

    // SPEC-0012's lifecycle, each move logged. Participants read it through
    // their context; only this thread moves it.
    composition::HostLifecycle lifecycle;
    HostStatus status;
    const auto kPublish = [&] {
        if (request.status.publish != nullptr) {
            request.status.publish(status, request.status.context);
        }
    };
    const auto kEnter = [&](composition::HostState next, std::string_view reason) {
        const composition::HostState kFrom = lifecycle.state();
        if (lifecycle.enter(next)) {
            status.state = next;
            kPublish();
            kEmitter.log(Severity::Info,
                         kLifecycle,
                         "host lifecycle",
                         {diagnostics::field("state", composition::describe(next)),
                          diagnostics::field("from", composition::describe(kFrom)),
                          diagnostics::field("reason", reason)});
        }
    };
    kEmitter.log(Severity::Info,
                 kLifecycle,
                 "host lifecycle",
                 {diagnostics::field("state", composition::describe(composition::HostState::Starting))});
    kPublish();
    if (kBadKey) {
        kEmitter.log(Severity::Critical,
                     kBadConfiguration,
                     "a host setting is malformed or out of range",
                     {diagnostics::field("key", *kBadKey)});
        kEnter(composition::HostState::Failed, "bad_configuration");
        kDrain();
        return HostExit::StartupFailed;
    }

    kEnter(composition::HostState::Preparing, "configured");
    execution::Executor cpu{execution::ExecutorSettings{
        .kind = execution::ExecutorKind::Cpu, .workers = settings.cpuWorkers, .clock = &clock, .emitter = kEmitter}};
    execution::Executor io{execution::ExecutorSettings{.kind = execution::ExecutorKind::BlockingIo,
                                                       .workers = settings.ioWorkers,
                                                       .clock = &clock,
                                                       .emitter = kEmitter}};
    execution::CancellationScope root{clock};

    std::vector<composition::Problem> problems;
    auto plan = composition::compose(composition::CompositionRequest{.registrars = request.registrars,
                                                                     .role = request.role,
                                                                     .platform = request.platform,
                                                                     .shutdownBudget = settings.shutdownBudget},
                                     problems);
    const auto kShutDown = [&] {
        root.cancel(execution::CancelReason::OwnerStopping);
        io.stop();
        cpu.stop();
    };
    if (!plan.has_value()) {
        for (const composition::Problem& problem : problems) {
            kEmitter.log(Severity::Error,
                         kPlanProblem,
                         problem.detail,
                         {diagnostics::field("problem", composition::describe(problem.kind)),
                          diagnostics::field("subject", std::string_view{problem.subject})});
        }
        kEmitter.log(Severity::Critical, kPlanRefused, "the composition plan was refused");
        kEnter(composition::HostState::Failed, "plan_refused");
        kShutDown();
        kDrain();
        return HostExit::StartupFailed;
    }

    composition::Composition composition{*plan,
                                         composition::HostServices{.clock = &clock,
                                                                   .scope = &root,
                                                                   .cpu = &cpu,
                                                                   .blockingIo = &io,
                                                                   .emitter = kEmitter,
                                                                   .configuration = &configuration,
                                                                   .lifecycle = &lifecycle}};
    if (auto started = composition.start(); !started.has_value()) {
        kEmitter.log(Severity::Critical,
                     kStartFailed,
                     started.error().description(),
                     {diagnostics::field("errorClass", result::describe(started.error().errorClass()))});
        kEnter(composition::HostState::Failed, "start_failed");
        kShutDown();
        kDrain();
        return HostExit::StartupFailed;
    }
    kEmitter.log(Severity::Info,
                 kStarted,
                 "host started",
                 {diagnostics::field("participants", plan->participants().size()),
                  diagnostics::field("cpuWorkers", cpu.workerCount()),
                  diagnostics::field("shutdownBoundMs", shutdownBound(settings).nanoseconds / 1'000'000)});
    kEnter(composition::HostState::Ready, "started");
    // Immediate activation, the only policy until a supervised one is
    // accepted: admission opens as soon as the Host is ready.
    kEnter(composition::HostState::Active, "immediate");

    // The Host schedule. The host thread is not a CPU worker, so pacing by
    // sleeping here blocks no pool.
    const execution::MonotonicDuration kPeriod{static_cast<std::int64_t>(1'000'000'000 / settings.iterationRate)};
    execution::MonotonicInstant next = clock.now();
    std::uint64_t iteration = 0;
    HostExit exit = HostExit::Stopped;
    composition::HealthReport health;
    StallWatch cpuWatch{clock.now()};
    StallWatch ioWatch{clock.now()};
    execution::MonotonicInstant drainStart = clock.now();
    const auto kDrainFrom = [&](std::string_view reason) {
        drainStart = clock.now();
        kEnter(composition::HostState::Draining, reason);
    };
    // A stop request, or an unhealthy report, drains: admission closes and
    // play goes on until no participant serves a connection or the drain's
    // time is up. Repeated requests change nothing. The iteration bound ends
    // the run where it falls, draining or not.
    while (settings.maximumIterations == 0 || iteration < settings.maximumIterations) {
        if (lifecycle.state() == composition::HostState::Active) {
            if (request.stopRequested != nullptr && request.stopRequested->load(std::memory_order_acquire)) {
                kDrainFrom("stop_requested");
            } else if (health.health == composition::Health::Unhealthy) {
                exit = HostExit::Unhealthy;
                kDrainFrom("unhealthy");
            }
        }
        if (lifecycle.state() == composition::HostState::Draining &&
            (composition.connections() == 0 || clock.now() - drainStart >= settings.drain)) {
            break;
        }
        const composition::HostFrame kFrame{.iteration = iteration, .now = clock.now()};
        for (std::size_t phase = 0; phase < composition::kHostPhaseCount; ++phase) {
            composition.runHostPhase(static_cast<composition::HostPhase>(phase), kFrame);
            if (static_cast<composition::HostPhase>(phase) == composition::HostPhase::Maintenance) {
                kDrain();
            }
        }
        ++iteration;
        status.iteration = iteration;
        // The worst of the participants' reports and the Host's own watch.
        composition::HealthReport report = composition.health();
        const execution::MonotonicInstant kChecked = clock.now();
        const std::pair<StallWatch*, execution::Executor*> kWatched[] = {{&cpuWatch, &cpu}, {&ioWatch, &io}};
        for (const auto& [watch, executor] : kWatched) {
            const composition::Health kStall = watch->check(executor->progress(), kChecked, settings.stall);
            if (kStall > report.health) {
                report = composition::HealthReport{
                    .health = kStall,
                    .reason = "executor_stalled",
                    .participant = executor->kind() == execution::ExecutorKind::Cpu ? "host.cpu" : "host.blocking_io"};
            }
        }
        bool changed = false;
        if (report.health != health.health) {
            health = report;
            status.health = health.health;
            status.reason = health.reason;
            changed = true;
            kEmitter.log(health.health == composition::Health::Healthy ? Severity::Info : Severity::Warning,
                         kHealth,
                         "host health",
                         {diagnostics::field("health", composition::describe(health.health)),
                          diagnostics::field("reason", health.reason),
                          diagnostics::field("participant", health.participant)});
        }
        if (const std::size_t kConnections = composition.connections(); kConnections != status.connections) {
            status.connections = kConnections;
            changed = true;
        }
        if (changed) {
            kPublish();
        }
        next = next + kPeriod;
        const execution::MonotonicInstant kNow = clock.now();
        if (kNow < next) {
            std::this_thread::sleep_for(std::chrono::nanoseconds{(next - kNow).nanoseconds});
        } else if ((kNow - next) > kPeriod) {
            // Far behind: pace from now instead of bursting to catch up. The
            // World pacer, not the Host loop, owns catching up on ticks.
            next = kNow;
        }
    }

    if (lifecycle.state() == composition::HostState::Active) {
        kDrainFrom("iteration_bound");
    }
    const std::size_t kConnections = composition.connections();
    kEnter(composition::HostState::Stopping, kConnections == 0 ? "drained" : "drain_ended");
    kEmitter.log(Severity::Info,
                 kStopping,
                 "host stopping",
                 {diagnostics::field("iterations", iteration),
                  diagnostics::field("drainMs", (clock.now() - drainStart).nanoseconds / 1'000'000),
                  diagnostics::field("connections", kConnections)});
    composition.stop();
    kShutDown();
    kEnter(composition::HostState::Stopped, "stopped");
    kEmitter.log(Severity::Info,
                 kStopped,
                 "host stopped",
                 {diagnostics::field("health", composition::describe(health.health)),
                  diagnostics::field("exit", static_cast<std::uint64_t>(exit)),
                  diagnostics::field("shutdownMs", (clock.now() - drainStart).nanoseconds / 1'000'000)});
    router.stop();
    kDrain();
    return exit;
}

} // namespace rawframe::host
