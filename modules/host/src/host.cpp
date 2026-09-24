#include "rawframe/host/host.h"

#include "rawframe/composition/composition.h"
#include "rawframe/composition/plan.h"
#include "rawframe/diagnostics/emitter.h"
#include "rawframe/diagnostics/router.h"
#include "rawframe/execution/cancellation.h"
#include "rawframe/execution/executor.h"
#include "rawframe/execution/time.h"

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
    Severity minimumSeverity = Severity::Info;
};

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
    if (const auto kSeverity = configuration.text("diagnostics.minimum_severity")) {
        const auto kParsed = severityNamed(*kSeverity);
        if (!kParsed) {
            return "diagnostics.minimum_severity";
        }
        settings.minimumSeverity = *kParsed;
    }
    return std::nullopt;
}

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
    if (kBadKey) {
        kEmitter.log(Severity::Critical,
                     kBadConfiguration,
                     "a host setting is malformed or out of range",
                     {diagnostics::field("key", *kBadKey)});
        kDrain();
        return HostExit::StartupFailed;
    }

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
                                                                   .configuration = &configuration}};
    if (auto started = composition.start(); !started.has_value()) {
        kEmitter.log(Severity::Critical,
                     kStartFailed,
                     started.error().description(),
                     {diagnostics::field("errorClass", result::describe(started.error().errorClass()))});
        kShutDown();
        kDrain();
        return HostExit::StartupFailed;
    }
    kEmitter.log(Severity::Info,
                 kStarted,
                 "host started",
                 {diagnostics::field("participants", plan->participants().size()),
                  diagnostics::field("cpuWorkers", cpu.workerCount())});

    // The Host schedule. The host thread is not a CPU worker, so pacing by
    // sleeping here blocks no pool.
    const execution::MonotonicDuration kPeriod{static_cast<std::int64_t>(1'000'000'000 / settings.iterationRate)};
    execution::MonotonicInstant next = clock.now();
    std::uint64_t iteration = 0;
    while ((request.stopRequested == nullptr || !request.stopRequested->load(std::memory_order_acquire)) &&
           (settings.maximumIterations == 0 || iteration < settings.maximumIterations)) {
        const composition::HostFrame kFrame{.iteration = iteration, .now = clock.now()};
        for (std::size_t phase = 0; phase < composition::kHostPhaseCount; ++phase) {
            composition.runHostPhase(static_cast<composition::HostPhase>(phase), kFrame);
            if (static_cast<composition::HostPhase>(phase) == composition::HostPhase::Maintenance) {
                kDrain();
            }
        }
        ++iteration;
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

    kEmitter.log(Severity::Info, kStopping, "host stopping", {diagnostics::field("iterations", iteration)});
    composition.stop();
    kShutDown();
    kEmitter.log(Severity::Info, kStopped, "host stopped");
    router.stop();
    kDrain();
    return HostExit::Stopped;
}

} // namespace rawframe::host
