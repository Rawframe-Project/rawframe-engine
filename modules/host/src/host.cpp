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
#include <vector>

#if RAWFRAME_THREADS
#include <thread>
#endif

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
constexpr EventIdentity kNotStarted{"host", "not_started"};
constexpr EventIdentity kUnknownSetting{"host", "unknown_setting"};
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

struct Host::State {
    explicit State(const HostRequest& given)
        : request(given), configuration(given.configuration != nullptr ? *given.configuration : emptyConfiguration()),
          write(given.log.write != nullptr ? given.log.write : &discard), process(processIdentity()),
          // Diagnostics first, so everything after can report. The sink's
          // buffer is the only allocation on the log path.
          sink(diagnostics::StreamInfo{.processIdentity = process,
                                       .processStartWallUnixNanoseconds = wallNanoseconds(),
                                       .buildReceipt = "unreleased",
                                       .profile = roleName(given.role),
                                       .configuration = RAWFRAME_CONFIGURATION_NAME,
                                       .targetRole = roleName(given.role),
                                       .session = ""},
               // A local log is operator-owned, so it may hold personal data;
               // a sink leaving the machine would declare a lower clearance
               // (ADR-0063).
               diagnostics::Sensitivity::Personal,
               kLogBufferBytes),
          badKey(readSettings(configuration, settings)), sinks{&sink},
          router(diagnostics::RouterSettings{.minimumSeverity = settings.minimumSeverity,
                                             .monotonic = &diagnostics::steadyClockNanoseconds,
                                             .wall = &wallNanoseconds},
                 sinks),
          emitter(router.emitter()), cpuWatch(clock.now()), ioWatch(clock.now()), drainStart(clock.now()) {
    }

    static const composition::Configuration& emptyConfiguration() noexcept {
        static const composition::Configuration kEmpty;
        return kEmpty;
    }

    void drainLog() noexcept {
        static_cast<void>(sink.drain(write, request.log.context));
    }

    void publish() noexcept {
        if (request.status.publish != nullptr) {
            request.status.publish(status, request.status.context);
        }
    }

    /// SPEC-0012's lifecycle, each move logged. Participants read it through
    /// their context; only the Host's caller moves it.
    void enter(composition::HostState to, std::string_view reason) noexcept {
        const composition::HostState kFrom = lifecycle.state();
        if (lifecycle.enter(to)) {
            status.state = to;
            publish();
            emitter.log(Severity::Info,
                        kLifecycle,
                        "host lifecycle",
                        {diagnostics::field("state", composition::describe(to)),
                         diagnostics::field("from", composition::describe(kFrom)),
                         diagnostics::field("reason", reason)});
        }
    }

    void shutDown() noexcept {
        root->cancel(execution::CancelReason::OwnerStopping);
        io->stop();
        cpu->stop();
    }

    /// The one record of why a refused start ends the run (SPEC-0012).
    void logExit(HostExit reason) noexcept {
        emitter.log(Severity::Critical,
                    kNotStarted,
                    "the host did not start",
                    {diagnostics::field("exit", describe(reason)),
                     diagnostics::field("exitCode", static_cast<std::uint64_t>(exitCode(reason)))});
    }

    void drainFrom(std::string_view reason) noexcept {
        drainStart = clock.now();
        enter(composition::HostState::Draining, reason);
    }

    /// Composes and starts; false, with everything rolled back and logged,
    /// when that was refused.
    bool start() noexcept {
        emitter.log(Severity::Info,
                    kLifecycle,
                    "host lifecycle",
                    {diagnostics::field("state", composition::describe(composition::HostState::Starting))});
        publish();
        if (badKey) {
            emitter.log(Severity::Critical,
                        kBadConfiguration,
                        "a host setting is malformed or out of range",
                        {diagnostics::field("key", *badKey)});
            startExit = HostExit::InvalidLaunchDescriptor;
            logExit(startExit);
            enter(composition::HostState::Failed, "bad_configuration");
            drainLog();
            return false;
        }

        enter(composition::HostState::Preparing, "configured");
        cpu.emplace(execution::ExecutorSettings{
            .kind = execution::ExecutorKind::Cpu, .workers = settings.cpuWorkers, .clock = &clock, .emitter = emitter});
        io.emplace(execution::ExecutorSettings{.kind = execution::ExecutorKind::BlockingIo,
                                               .workers = settings.ioWorkers,
                                               .clock = &clock,
                                               .emitter = emitter});
        root.emplace(clock);

        std::vector<composition::Problem> problems;
        auto composed = composition::compose(composition::CompositionRequest{.registrars = request.registrars,
                                                                             .role = request.role,
                                                                             .platform = request.platform,
                                                                             .shutdownBudget = settings.shutdownBudget},
                                             problems);
        if (!composed.has_value()) {
            for (const composition::Problem& problem : problems) {
                emitter.log(Severity::Error,
                            kPlanProblem,
                            problem.detail,
                            {diagnostics::field("problem", composition::describe(problem.kind)),
                             diagnostics::field("subject", std::string_view{problem.subject})});
            }
            emitter.log(Severity::Critical, kPlanRefused, "the composition plan was refused");
            startExit = HostExit::UnsupportedConfiguration;
            logExit(startExit);
            enter(composition::HostState::Failed, "plan_refused");
            shutDown();
            drainLog();
            return false;
        }
        plan.emplace(std::move(*composed));

        composition.emplace(*plan,
                            composition::HostServices{.clock = &clock,
                                                      .scope = &*root,
                                                      .cpu = &*cpu,
                                                      .blockingIo = &*io,
                                                      .emitter = emitter,
                                                      .configuration = &configuration,
                                                      .lifecycle = &lifecycle,
                                                      .files = request.files});
        if (auto started = composition->start(); !started.has_value()) {
            // What the failure was about, as its owners recorded it: `key:
            // value` pairs, such as a Kest program's first diagnostic.
            std::string context;
            for (const result::ContextField& each : started.error().context()) {
                context += context.empty() ? "" : "; ";
                context += each.key;
                context += ": ";
                context += each.value;
            }
            emitter.log(Severity::Critical,
                        kStartFailed,
                        started.error().description(),
                        {diagnostics::field("errorClass", result::describe(started.error().errorClass())),
                         diagnostics::field("context", std::string_view{context})});
            startExit = startupExit(started.error().errorClass());
            logExit(startExit);
            enter(composition::HostState::Failed, "start_failed");
            composition.reset();
            shutDown();
            drainLog();
            return false;
        }
        // Every key is some participant's, or the Host's: one nothing read is
        // not a setting of this process (SPEC-0012, D187).
        if (const std::vector<std::string_view> kUnknown = configuration.unread(); !kUnknown.empty()) {
            for (const std::string_view kKey : kUnknown) {
                emitter.log(Severity::Critical,
                            kUnknownSetting,
                            "a configuration key is no setting of this process",
                            {diagnostics::field("key", kKey)});
            }
            startExit = HostExit::InvalidLaunchDescriptor;
            logExit(startExit);
            enter(composition::HostState::Failed, "unknown_setting");
            composition->stop();
            composition.reset();
            shutDown();
            drainLog();
            return false;
        }
        emitter.log(Severity::Info,
                    kStarted,
                    "host started",
                    {diagnostics::field("participants", plan->participants().size()),
                     diagnostics::field("cpuWorkers", cpu->workerCount()),
                     diagnostics::field("shutdownBoundMs", shutdownBound(settings).nanoseconds / 1'000'000)});
        enter(composition::HostState::Ready, "started");
        // Immediate activation, the only policy until a supervised one is
        // accepted: admission opens as soon as the Host is ready.
        enter(composition::HostState::Active, "immediate");
        period = execution::MonotonicDuration{static_cast<std::int64_t>(1'000'000'000 / settings.iterationRate)};
        next = clock.now();
        return true;
    }

    /// One iteration of the Host schedule. False when the run has ended: the
    /// iteration bound reached, or a drain over.
    bool iterate() noexcept {
        if (settings.maximumIterations != 0 && iteration >= settings.maximumIterations) {
            return false;
        }
        // A stop request, or an unhealthy report, drains: admission closes
        // and play goes on until no participant serves a connection or the
        // drain's time is up. Repeated requests change nothing. The
        // iteration bound ends the run where it falls, draining or not.
        if (lifecycle.state() == composition::HostState::Active) {
            if (request.stopRequested != nullptr && request.stopRequested->load(std::memory_order_acquire)) {
                drainFrom("stop_requested");
            } else if (health.health == composition::Health::Unhealthy) {
                exit =
                    health.reason == composition::kOverloaded ? HostExit::ControlledOverload : HostExit::RuntimeFailure;
                drainFrom("unhealthy");
            }
        }
        if (lifecycle.state() == composition::HostState::Draining &&
            (composition->connections() == 0 || clock.now() - drainStart >= settings.drain)) {
            return false;
        }
        const composition::HostFrame kFrame{.iteration = iteration, .now = clock.now()};
        for (std::size_t phase = 0; phase < composition::kHostPhaseCount; ++phase) {
            composition->runHostPhase(static_cast<composition::HostPhase>(phase), kFrame);
            if (static_cast<composition::HostPhase>(phase) == composition::HostPhase::Maintenance) {
                drainLog();
            }
        }
#if !RAWFRAME_THREADS
        // Without workers the Host runs what was queued, between iterations
        // as a worker would have beside them.
        while (io->runOne() || cpu->runOne()) {
        }
#endif
        ++iteration;
        status.iteration = iteration;
        watchHealth();
        next = next + period;
        const execution::MonotonicInstant kNow = clock.now();
        if (kNow >= next && (kNow - next) > period) {
            // Far behind: pace from now instead of bursting to catch up. The
            // World pacer, not the Host loop, owns catching up on ticks.
            next = kNow;
        }
        return true;
    }

    /// The worst of the participants' reports and the Host's own watch on its
    /// executors.
    void watchHealth() noexcept {
        composition::HealthReport report = composition->health();
        const execution::MonotonicInstant kChecked = clock.now();
        const std::pair<StallWatch*, execution::Executor*> kWatched[] = {{&cpuWatch, &*cpu}, {&ioWatch, &*io}};
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
            emitter.log(health.health == composition::Health::Healthy ? Severity::Info : Severity::Warning,
                        kHealth,
                        "host health",
                        {diagnostics::field("health", composition::describe(health.health)),
                         diagnostics::field("reason", health.reason),
                         diagnostics::field("participant", health.participant)});
        }
        if (const std::size_t kConnections = composition->connections(); kConnections != status.connections) {
            status.connections = kConnections;
            changed = true;
        }
        if (changed) {
            publish();
        }
    }

    /// Drains if still active, then stops the composition, the executors,
    /// and the log, in that order.
    HostExit finish() noexcept {
        if (lifecycle.state() == composition::HostState::Active) {
            drainFrom("iteration_bound");
        }
        const std::size_t kConnections = composition->connections();
        enter(composition::HostState::Stopping, kConnections == 0 ? "drained" : "drain_ended");
        emitter.log(Severity::Info,
                    kStopping,
                    "host stopping",
                    {diagnostics::field("iterations", iteration),
                     diagnostics::field("drainMs", (clock.now() - drainStart).nanoseconds / 1'000'000),
                     diagnostics::field("connections", kConnections)});
        composition->stop();
        // A run that was otherwise clean says it outlived its budget; a
        // failure already said says more.
        if (composition->overran() && exit == HostExit::Stopped) {
            exit = HostExit::ShutdownTimeout;
        }
        shutDown();
        enter(composition::HostState::Stopped, "stopped");
        emitter.log(Severity::Info,
                    kStopped,
                    "host stopped",
                    {diagnostics::field("health", composition::describe(health.health)),
                     diagnostics::field("exit", describe(exit)),
                     diagnostics::field("exitCode", static_cast<std::uint64_t>(exitCode(exit))),
                     diagnostics::field("shutdownMs", (clock.now() - drainStart).nanoseconds / 1'000'000)});
        router.stop();
        drainLog();
        return exit;
    }

    const HostRequest request;
    const composition::Configuration& configuration;
    const diagnostics::WriteBytes write;
    const execution::SteadyClock clock;
    const std::string process;
    diagnostics::NdjsonSink sink;
    Settings settings;
    const std::optional<std::string_view> badKey;
    std::array<diagnostics::Sink*, 1> sinks;
    diagnostics::Router router;
    const diagnostics::Emitter emitter;
    composition::HostLifecycle lifecycle;
    HostStatus status;
    std::optional<execution::Executor> cpu;
    std::optional<execution::Executor> io;
    std::optional<execution::CancellationScope> root;
    std::optional<composition::Plan> plan;
    std::optional<composition::Composition> composition;

    execution::MonotonicDuration period{};
    execution::MonotonicInstant next{};
    std::uint64_t iteration = 0;
    HostExit exit = HostExit::Stopped;
    /// Why the start was refused, when it was.
    HostExit startExit = HostExit::StartupFailure;
    composition::HealthReport health;
    StallWatch cpuWatch;
    StallWatch ioWatch;
    execution::MonotonicInstant drainStart;
};

std::string_view describe(HostExit exit) noexcept {
    switch (exit) {
    case HostExit::Stopped:
        return "clean_stop";
    case HostExit::InvalidInvocation:
        return "invalid_invocation";
    case HostExit::InvalidLaunchDescriptor:
        return "invalid_launch_descriptor";
    case HostExit::IncompatibleArtifact:
        return "incompatible_artifact";
    case HostExit::ResourceUnavailable:
        return "required_resource_unavailable";
    case HostExit::UnsupportedConfiguration:
        return "unsupported_configuration";
    case HostExit::StartupFailure:
        return "startup_failure";
    case HostExit::RuntimeFailure:
        return "runtime_failure";
    case HostExit::ShutdownTimeout:
        return "shutdown_timeout";
    case HostExit::ControlledOverload:
        return "controlled_overload";
    }
    return "startup_failure";
}

int exitCode(HostExit exit) noexcept {
    switch (exit) {
    case HostExit::Stopped:
        return 0;
    case HostExit::InvalidInvocation:
        return 64;
    case HostExit::InvalidLaunchDescriptor:
    case HostExit::IncompatibleArtifact:
        return 65;
    case HostExit::ResourceUnavailable:
        return 69;
    case HostExit::UnsupportedConfiguration:
        return 78;
    case HostExit::StartupFailure:
    case HostExit::RuntimeFailure:
        return 70;
    case HostExit::ShutdownTimeout:
    case HostExit::ControlledOverload:
        return 75;
    }
    return 70;
}

HostExit startupExit(result::ErrorClass errorClass) noexcept {
    switch (errorClass) {
    case result::ErrorClass::InvalidArgument:
    case result::ErrorClass::OutOfRange:
        return HostExit::InvalidLaunchDescriptor;
    case result::ErrorClass::DataLoss:
    case result::ErrorClass::Unauthenticated:
    case result::ErrorClass::PermissionDenied:
        return HostExit::IncompatibleArtifact;
    case result::ErrorClass::NotFound:
    case result::ErrorClass::AlreadyExists:
    case result::ErrorClass::ResourceExhausted:
    case result::ErrorClass::Unavailable:
        return HostExit::ResourceUnavailable;
    case result::ErrorClass::FailedPrecondition:
    case result::ErrorClass::Unsupported:
        return HostExit::UnsupportedConfiguration;
    case result::ErrorClass::Conflict:
    case result::ErrorClass::Internal:
        return HostExit::StartupFailure;
    }
    return HostExit::StartupFailure;
}

Host::Host(const HostRequest& request) : state_(std::make_unique<State>(request)) {
    if (!state_->start()) {
        ended_ = state_->startExit;
    }
}

Host::~Host() {
    static_cast<void>(stop());
}

bool Host::iterate() noexcept {
    if (ended_.has_value()) {
        return false;
    }
    if (!state_->iterate()) {
        ended_ = state_->finish();
        return false;
    }
    return true;
}

execution::MonotonicInstant Host::due() const noexcept {
    return state_->next;
}

HostExit Host::stop() noexcept {
    if (!ended_.has_value()) {
        ended_ = state_->finish();
    }
    return *ended_;
}

#if RAWFRAME_THREADS
HostExit runHost(const HostRequest& request) noexcept {
    Host host{request};
    const execution::SteadyClock clock;
    // The host thread is not a CPU worker, so pacing by sleeping here blocks
    // no pool.
    while (host.iterate()) {
        const execution::MonotonicInstant kNow = clock.now();
        if (kNow < host.due()) {
            std::this_thread::sleep_for(std::chrono::nanoseconds{(host.due() - kNow).nanoseconds});
        }
    }
    return host.stop();
}
#endif

} // namespace rawframe::host
