// The Host end to end: configuration, composition, the Host schedule, the log,
// the lifecycle, draining, health, the status snapshot, and orderly stop,
// driven by a participant that counts its phases, plays at serving
// connections, and can hold the CPU worker.

#include "rawframe/composition/composition.h"
#include "rawframe/host/host.h"
#include "rawframe/test/test.h"

#include <array>
#include <atomic>
#include <chrono>
#include <initializer_list>
#include <string>
#include <vector>

#if RAWFRAME_THREADS
#include <thread>
#endif

using namespace rawframe;
using composition::HostPhase;

namespace {

struct Counts {
    std::atomic<int> runWorlds{0};
    std::atomic<int> maintenance{0};
    std::atomic<int> started{0};
    std::atomic<int> stopped{0};
    /// Iterations run with admission open, and closed.
    std::atomic<int> admitting{0};
    std::atomic<int> closed{0};
    /// Connections served; one closes each iteration once admission has
    /// closed, unless they are held.
    std::size_t connections = 0;
    bool holdConnections = false;
    /// The iteration a stop is asked for, and the one the participant turns
    /// unhealthy at.
    int stopAt = -1;
    int unhealthyAt = -1;
    std::string_view unhealthyReason = "test_failure";
    std::atomic<bool>* stop = nullptr;
    /// The iteration a task that holds the CPU worker is submitted at, and
    /// what lets it go.
    int holdWorkerAt = -1;
    std::atomic<bool> release{false};
    /// How long the participant's stop takes.
    int stopMilliseconds = 0;
    /// Every status the Host published.
    std::vector<host::HostStatus> statuses;
};

Counts counts;

class Counting final : public composition::Participant {
public:
    result::Status start(composition::ParticipantContext& context) noexcept override {
        context_ = &context;
        ++counts.started;
        return {};
    }
    void stop() noexcept override {
        ++counts.stopped;
#if RAWFRAME_THREADS
        std::this_thread::sleep_for(std::chrono::milliseconds(counts.stopMilliseconds));
#endif
    }
    void runHostPhase(HostPhase phase, const composition::HostFrame&) noexcept override {
        if (phase == HostPhase::RunWorlds) {
            ++counts.runWorlds;
            serve();
        } else if (phase == HostPhase::Maintenance) {
            ++counts.maintenance;
        }
    }

private:
    void serve() {
        if (context_->admitting()) {
            ++counts.admitting;
        } else {
            ++counts.closed;
            if (counts.connections > 0 && !counts.holdConnections) {
                --counts.connections;
            }
        }
        context_->reportConnections(counts.connections);
        if (counts.stop != nullptr && counts.runWorlds == counts.stopAt) {
            counts.stop->store(true, std::memory_order_release);
        }
#if RAWFRAME_THREADS
        if (counts.runWorlds == counts.holdWorkerAt && context_->cpuExecutor() != nullptr) {
            static_cast<void>(
                context_->cpuExecutor()->submit(context_->owner(), execution::Priority::Normal, []() noexcept {
                    while (!counts.release.load()) {
                        std::this_thread::yield();
                    }
                }));
        }
#endif
        if (counts.unhealthyAt >= 0 && counts.runWorlds >= counts.unhealthyAt) {
            context_->reportHealth(composition::Health::Unhealthy, counts.unhealthyReason);
        }
    }

    composition::ParticipantContext* context_ = nullptr;
};

result::Result<composition::ParticipantOwner> makeCounting(composition::ParticipantContext&) noexcept {
    return composition::ParticipantOwner{new Counting};
}

constexpr std::string_view kMissing[] = {"nobody.provides.this"};
bool requireMissing = false;

void registerCounting(composition::ParticipantRegistrar& registrar) noexcept {
    registrar.submit(composition::ParticipantDeclaration{
        .identity = "test.counting",
        .factory = &makeCounting,
        .scope = composition::LifetimeScope::Runtime,
        .requiredCapabilities =
            requireMissing ? std::span<const std::string_view>{kMissing} : std::span<const std::string_view>{},
        .executor = {.cpu = true, .blockingIo = false, .quota = {.maximumPendingTasks = 4}},
        .lifecycle = {.stopBudget = execution::MonotonicDuration::fromMilliseconds(10)},
        .hostPhases =
            composition::hostPhaseBit(HostPhase::RunWorlds) | composition::hostPhaseBit(HostPhase::Maintenance),
    });
}

const std::array<composition::RegistrarEntry, 1> kRegistrars = {composition::RegistrarEntry{
    "host_test", &registerCounting, composition::scopeBit(composition::LifetimeScope::Runtime)}};

bool collect(void* context, std::span<const char> bytes) noexcept {
    static_cast<std::string*>(context)->append(bytes.data(), bytes.size());
    return true;
}

void reset() {
    counts.runWorlds = 0;
    counts.maintenance = 0;
    counts.started = 0;
    counts.stopped = 0;
    counts.admitting = 0;
    counts.closed = 0;
    counts.connections = 0;
    counts.holdConnections = false;
    counts.stopAt = -1;
    counts.unhealthyAt = -1;
    counts.unhealthyReason = "test_failure";
    counts.stop = nullptr;
    counts.holdWorkerAt = -1;
    counts.release = false;
    counts.stopMilliseconds = 0;
    counts.statuses.clear();
    requireMissing = false;
}

#if RAWFRAME_THREADS
/// Keeps each status, and lets the held worker go once the Host is unhealthy.
void observe(const host::HostStatus& status, void*) noexcept {
    counts.statuses.push_back(status);
    if (status.health == composition::Health::Unhealthy) {
        counts.release = true;
    }
}

host::HostExit run(std::string_view configurationText, std::string& log, const std::atomic<bool>* stop = nullptr) {
    const auto kConfiguration = composition::Configuration::parse(configurationText);
    RAWFRAME_EXPECT(kConfiguration.has_value());
    return host::runHost(host::HostRequest{.role = composition::TargetRole::Test,
                                           .registrars = kRegistrars,
                                           .configuration = &*kConfiguration,
                                           .log = {.write = &collect, .context = &log},
                                           .stopRequested = stop,
                                           .status = {.publish = &observe, .context = nullptr}});
}

#endif

bool mentions(const std::string& log, std::string_view text) {
    return log.find(text) != std::string::npos;
}

#if RAWFRAME_THREADS
/// Whether the log names these lifecycle states, in this order.
bool movesThrough(const std::string& log, std::initializer_list<std::string_view> states) {
    std::size_t at = 0;
    for (const std::string_view kState : states) {
        at = log.find("\"state\":\"" + std::string{kState} + "\"", at);
        if (at == std::string::npos) {
            return false;
        }
    }
    return true;
}

#endif

} // namespace

// A process run paces by sleeping, where there are threads.
#if RAWFRAME_THREADS
RAWFRAME_TEST(AHostRunsItsIterationsAndStopsInOrder) {
    reset();
    std::string log;
    const auto kExit = run("host.maximum_iterations = 5\nhost.iteration_rate = 1000\nhost.cpu_workers = 1", log);
    RAWFRAME_EXPECT(kExit == host::HostExit::Stopped);
    RAWFRAME_EXPECT(counts.runWorlds == 5 && counts.maintenance == 5);
    RAWFRAME_EXPECT(counts.started == 1 && counts.stopped == 1);
    RAWFRAME_EXPECT(log.starts_with("{\"schema\":1,\"kind\":\"stream\""));
    const std::size_t kStarted = log.find("\"code\":\"started\"");
    const std::size_t kStopped = log.find("\"code\":\"stopped\"");
    RAWFRAME_EXPECT(kStarted != std::string::npos && kStopped != std::string::npos && kStarted < kStopped);
    RAWFRAME_EXPECT(mentions(log, "\"iterations\":5"));
    // The bound ends the run where it falls: every state, in order.
    RAWFRAME_EXPECT(movesThrough(log, {"starting", "preparing", "ready", "active", "draining", "stopping", "stopped"}));
    RAWFRAME_EXPECT(mentions(log, "\"reason\":\"iteration_bound\"") && counts.admitting == 5 && counts.closed == 0);
}

RAWFRAME_TEST(AStopRequestDrainsTheConnectionsBeforeStopping) {
    reset();
    std::string log;
    std::atomic<bool> stop{false};
    counts.connections = 3;
    counts.stop = &stop;
    counts.stopAt = 4;
    // The drain may wait a minute; the connections end it.
    const auto kExit =
        run("host.iteration_rate = 1000\nhost.drain_ms = 60000\nhost.maximum_iterations = 1000", log, &stop);
    RAWFRAME_EXPECT(kExit == host::HostExit::Stopped);
    // Admission closed with the drain; play went on until the last
    // connection closed, and not an iteration longer.
    RAWFRAME_EXPECT(counts.admitting == 4 && counts.closed == 3 && counts.connections == 0);
    RAWFRAME_EXPECT(movesThrough(log, {"active", "draining", "stopping", "stopped"}));
    RAWFRAME_EXPECT(mentions(log, "\"reason\":\"stop_requested\"") && mentions(log, "\"reason\":\"drained\""));
    RAWFRAME_EXPECT(mentions(log, "\"health\":\"healthy\""));

    // The snapshot followed: every state once, in order, and the
    // connections as they closed.
    std::vector<composition::HostState> states;
    std::vector<std::size_t> connections;
    for (const host::HostStatus& kStatus : counts.statuses) {
        if (states.empty() || states.back() != kStatus.state) {
            states.push_back(kStatus.state);
        }
        if (connections.empty() || connections.back() != kStatus.connections) {
            connections.push_back(kStatus.connections);
        }
    }
    using composition::HostState;
    RAWFRAME_EXPECT((states == std::vector<HostState>{HostState::Starting,
                                                      HostState::Preparing,
                                                      HostState::Ready,
                                                      HostState::Active,
                                                      HostState::Draining,
                                                      HostState::Stopping,
                                                      HostState::Stopped}));
    RAWFRAME_EXPECT((connections == std::vector<std::size_t>{0, 3, 2, 1, 0}));
    RAWFRAME_EXPECT(counts.statuses.back().iteration == 7);
}

RAWFRAME_TEST(AStalledExecutorIsDegradedThenUnhealthy) {
    reset();
    std::string log;
    counts.holdWorkerAt = 2;
    // The one worker is held, and the task behind it waits: nothing
    // finishes. The observer lets the worker go once the Host is unhealthy.
    const auto kExit = run(
        "host.iteration_rate = 1000\nhost.cpu_workers = 1\nhost.stall_ms = 100\nhost.maximum_iterations = 100000", log);
    RAWFRAME_EXPECT(kExit == host::HostExit::RuntimeFailure);
    std::vector<composition::Health> seen;
    for (const host::HostStatus& kStatus : counts.statuses) {
        if (seen.empty() || seen.back() != kStatus.health) {
            seen.push_back(kStatus.health);
        }
    }
    RAWFRAME_EXPECT((seen == std::vector<composition::Health>{composition::Health::Healthy,
                                                              composition::Health::Degraded,
                                                              composition::Health::Unhealthy}));
    RAWFRAME_EXPECT(counts.statuses.back().reason == "executor_stalled");
    RAWFRAME_EXPECT(mentions(log, "\"participant\":\"host.cpu\"") && mentions(log, "\"reason\":\"unhealthy\""));
}

RAWFRAME_TEST(ADrainEndsWhenItsTimeIsUp) {
    reset();
    std::string log;
    std::atomic<bool> stop{false};
    counts.connections = 2;
    counts.holdConnections = true;
    counts.stop = &stop;
    counts.stopAt = 1;
    const auto kExit = run("host.iteration_rate = 1000\nhost.drain_ms = 30", log, &stop);
    RAWFRAME_EXPECT(kExit == host::HostExit::Stopped);
    RAWFRAME_EXPECT(counts.closed > 0 && counts.connections == 2);
    RAWFRAME_EXPECT(mentions(log, "\"reason\":\"drain_ended\"") && mentions(log, "\"connections\":2"));
}

RAWFRAME_TEST(AnUnhealthyReportDrainsAndEndsTheRunUnhealthy) {
    reset();
    std::string log;
    counts.unhealthyAt = 3;
    const auto kExit = run("host.iteration_rate = 1000\nhost.maximum_iterations = 1000", log);
    RAWFRAME_EXPECT(kExit == host::HostExit::RuntimeFailure);
    // Reported in the third iteration, drained at the top of the fourth,
    // with nothing connected.
    RAWFRAME_EXPECT(counts.admitting == 3 && counts.closed == 0);
    RAWFRAME_EXPECT(mentions(log, "\"code\":\"health\"") && mentions(log, "\"reason\":\"test_failure\"") &&
                    mentions(log, "\"participant\":\"test.counting\""));
    RAWFRAME_EXPECT(mentions(log, "\"reason\":\"unhealthy\"") &&
                    mentions(log, "\"exit\":\"runtime_failure\",\"exitCode\":70"));
}

RAWFRAME_TEST(AStopRequestEndsAnUnboundedRun) {
    reset();
    std::string log;
    std::atomic<bool> stop{false};
    // The request comes once the loop is running, however long starting took
    // on a loaded machine.
    std::thread requester{[&stop] {
        while (counts.runWorlds.load() == 0 && counts.stopped.load() == 0) {
            std::this_thread::yield();
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        stop.store(true, std::memory_order_release);
    }};
    const auto kExit = run("host.iteration_rate = 500", log, &stop);
    requester.join();
    RAWFRAME_EXPECT(kExit == host::HostExit::Stopped);
    RAWFRAME_EXPECT(counts.runWorlds > 0 && counts.stopped == 1);
}

RAWFRAME_TEST(AnOverloadedReportEndsTheRunAsAControlledOverload) {
    // Drained and stopped in order as any unhealthy run, but a retry may
    // help, so its reason and code say so (D186).
    reset();
    std::string log;
    counts.unhealthyAt = 3;
    counts.unhealthyReason = composition::kOverloaded;
    RAWFRAME_EXPECT(run("host.iteration_rate = 1000\nhost.maximum_iterations = 1000", log) ==
                    host::HostExit::ControlledOverload);
    RAWFRAME_EXPECT(mentions(log, "\"reason\":\"overloaded\"") &&
                    mentions(log, "\"exit\":\"controlled_overload\",\"exitCode\":75"));
}

RAWFRAME_TEST(AKeyNothingReadsRefusesTheStart) {
    // A setting of no participant here, most often a typing mistake, is
    // refused rather than ignored, and what started is rolled back (D187).
    reset();
    std::string log;
    RAWFRAME_EXPECT(run("host.maximum_iterations = 2\nhost.iteration_rat = 1000", log) ==
                    host::HostExit::InvalidLaunchDescriptor);
    RAWFRAME_EXPECT(mentions(log, "\"code\":\"unknown_setting\"") && mentions(log, "host.iteration_rat") &&
                    mentions(log, "\"exit\":\"invalid_launch_descriptor\",\"exitCode\":65"));
    RAWFRAME_EXPECT(counts.started == 1 && counts.stopped == 1 && counts.runWorlds == 0);
    RAWFRAME_EXPECT(!mentions(log, "\"state\":\"ready\""));
}

RAWFRAME_TEST(AStopPastItsBudgetEndsTheRunAsATimeout) {
    // A participant that takes 60 ms to stop, under a 20 ms shutdown budget:
    // the run ends in order, and says the stop outlived it (D185).
    reset();
    std::string log;
    counts.stopMilliseconds = 60;
    RAWFRAME_EXPECT(run("host.maximum_iterations = 2\nhost.shutdown_budget_ms = 20", log) ==
                    host::HostExit::ShutdownTimeout);
    RAWFRAME_EXPECT(counts.stopped == 1 && mentions(log, "\"exit\":\"shutdown_timeout\",\"exitCode\":75"));
    // Within its budget, the same run is clean.
    reset();
    log.clear();
    counts.stopMilliseconds = 5;
    RAWFRAME_EXPECT(run("host.maximum_iterations = 2\nhost.shutdown_budget_ms = 500", log) == host::HostExit::Stopped);
}

RAWFRAME_TEST(StartupFailuresAreReportedAndNothingRuns) {
    reset();
    std::string log;
    RAWFRAME_EXPECT(run("host.iteration_rate = 0", log) == host::HostExit::InvalidLaunchDescriptor);
    RAWFRAME_EXPECT(mentions(log, "\"code\":\"bad_configuration\"") && mentions(log, "host.iteration_rate"));
    // Why, once, with the process's exit code (SPEC-0012, D184).
    RAWFRAME_EXPECT(mentions(log, "\"code\":\"not_started\"") &&
                    mentions(log, "\"exit\":\"invalid_launch_descriptor\",\"exitCode\":65"));
    RAWFRAME_EXPECT(movesThrough(log, {"starting", "failed"}) && !mentions(log, "\"state\":\"preparing\""));

    log.clear();
    RAWFRAME_EXPECT(run("diagnostics.minimum_severity = loud", log) == host::HostExit::InvalidLaunchDescriptor);

    // A supervisor's grace too short for the drain, the shutdown budget, and
    // the executors' budgets (5 + 5 + 6 seconds) and a tenth of it.
    log.clear();
    RAWFRAME_EXPECT(run("host.supervisor_grace_ms = 17000", log) == host::HostExit::InvalidLaunchDescriptor);
    RAWFRAME_EXPECT(mentions(log, "host.supervisor_grace_ms"));
    log.clear();
    RAWFRAME_EXPECT(run("host.supervisor_grace_ms = 18000\nhost.maximum_iterations = 1", log) ==
                    host::HostExit::Stopped);
    RAWFRAME_EXPECT(mentions(log, "\"shutdownBoundMs\":16000") && mentions(log, "\"shutdownMs\":"));

    log.clear();
    reset();
    requireMissing = true;
    RAWFRAME_EXPECT(run("host.maximum_iterations = 3", log) == host::HostExit::UnsupportedConfiguration);
    RAWFRAME_EXPECT(mentions(log, "\"code\":\"plan_problem\"") && mentions(log, "missing_provider"));
    RAWFRAME_EXPECT(mentions(log, "\"exit\":\"unsupported_configuration\",\"exitCode\":78"));
    RAWFRAME_EXPECT(counts.started == 0 && counts.runWorlds == 0);
    RAWFRAME_EXPECT(movesThrough(log, {"starting", "preparing", "failed"}) && !mentions(log, "\"state\":\"ready\""));
}

#endif

RAWFRAME_TEST(AHostIsDrivenAnIterationAtATime) {
    // As a browser's event loop drives it (D168): one iteration a call, no
    // sleeping, and a stop wherever the caller ends it.
    reset();
    std::string log;
    const auto kConfiguration = composition::Configuration::parse("host.iteration_rate = 60");
    {
        host::Host driven{host::HostRequest{.role = composition::TargetRole::Test,
                                            .registrars = kRegistrars,
                                            .configuration = &*kConfiguration,
                                            .log = {.write = &collect, .context = &log}}};
        const execution::MonotonicInstant kFirst = driven.due();
        RAWFRAME_EXPECT(driven.iterate() && driven.iterate() && driven.iterate());
        // Each iteration is due a period after the last, called early or not.
        RAWFRAME_EXPECT((driven.due() - kFirst).nanoseconds == 3 * (1'000'000'000 / 60));
        RAWFRAME_EXPECT(counts.runWorlds == 3 && counts.stopped == 0);
        RAWFRAME_EXPECT(driven.stop() == host::HostExit::Stopped && driven.stop() == host::HostExit::Stopped);
        RAWFRAME_EXPECT(!driven.iterate() && counts.runWorlds == 3 && counts.stopped == 1);
    }
    RAWFRAME_EXPECT(mentions(log, "\"iterations\":3") && counts.stopped == 1);

    // A refused start has ended before the first iteration.
    log.clear();
    const auto kBad = composition::Configuration::parse("host.iteration_rate = 0");
    host::Host refused{host::HostRequest{.role = composition::TargetRole::Test,
                                         .registrars = kRegistrars,
                                         .configuration = &*kBad,
                                         .log = {.write = &collect, .context = &log}}};
    RAWFRAME_EXPECT(!refused.iterate() && refused.stop() == host::HostExit::InvalidLaunchDescriptor);
    RAWFRAME_EXPECT(mentions(log, "\"code\":\"bad_configuration\""));
}

RAWFRAME_TEST(EachExitReasonHasItsPortableCategory) {
    // SPEC-0012's table: a supervisor retries what may pass and fixes what
    // will not.
    RAWFRAME_EXPECT(
        host::exitCode(host::HostExit::Stopped) == 0 && host::exitCode(host::HostExit::InvalidInvocation) == 64 &&
        host::exitCode(host::HostExit::InvalidLaunchDescriptor) == 65 &&
        host::exitCode(host::HostExit::IncompatibleArtifact) == 65 &&
        host::exitCode(host::HostExit::ResourceUnavailable) == 69 &&
        host::exitCode(host::HostExit::StartupFailure) == 70 && host::exitCode(host::HostExit::RuntimeFailure) == 70 &&
        host::exitCode(host::HostExit::UnsupportedConfiguration) == 78);
    // A participant's refusal is sorted by its error's class.
    using result::ErrorClass;
    RAWFRAME_EXPECT(host::startupExit(ErrorClass::InvalidArgument) == host::HostExit::InvalidLaunchDescriptor &&
                    host::startupExit(ErrorClass::DataLoss) == host::HostExit::IncompatibleArtifact &&
                    host::startupExit(ErrorClass::Unauthenticated) == host::HostExit::IncompatibleArtifact &&
                    host::startupExit(ErrorClass::NotFound) == host::HostExit::ResourceUnavailable &&
                    host::startupExit(ErrorClass::Unavailable) == host::HostExit::ResourceUnavailable &&
                    host::startupExit(ErrorClass::FailedPrecondition) == host::HostExit::UnsupportedConfiguration &&
                    host::startupExit(ErrorClass::Internal) == host::HostExit::StartupFailure);
    RAWFRAME_EXPECT(host::describe(host::HostExit::ResourceUnavailable) == "required_resource_unavailable");
}
