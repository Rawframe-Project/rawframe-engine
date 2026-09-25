// The Host end to end: configuration, composition, the Host schedule, the log,
// the lifecycle, draining, health, and orderly stop, driven by a participant
// that counts its phases and plays at serving connections.

#include "rawframe/composition/composition.h"
#include "rawframe/host/host.h"
#include "rawframe/test/test.h"

#include <array>
#include <atomic>
#include <chrono>
#include <initializer_list>
#include <string>
#include <thread>

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
    std::atomic<bool>* stop = nullptr;
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
        if (counts.unhealthyAt >= 0 && counts.runWorlds >= counts.unhealthyAt) {
            context_->reportHealth(composition::Health::Unhealthy, "test_failure");
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
    counts.stop = nullptr;
    requireMissing = false;
}

host::HostExit run(std::string_view configurationText, std::string& log, const std::atomic<bool>* stop = nullptr) {
    const auto kConfiguration = composition::Configuration::parse(configurationText);
    RAWFRAME_EXPECT(kConfiguration.has_value());
    return host::runHost(host::HostRequest{.role = composition::TargetRole::Test,
                                           .registrars = kRegistrars,
                                           .configuration = &*kConfiguration,
                                           .log = {.write = &collect, .context = &log},
                                           .stopRequested = stop});
}

bool mentions(const std::string& log, std::string_view text) {
    return log.find(text) != std::string::npos;
}

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

} // namespace

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
    RAWFRAME_EXPECT(kExit == host::HostExit::Unhealthy);
    // Reported in the third iteration, drained at the top of the fourth,
    // with nothing connected.
    RAWFRAME_EXPECT(counts.admitting == 3 && counts.closed == 0);
    RAWFRAME_EXPECT(mentions(log, "\"code\":\"health\"") && mentions(log, "\"reason\":\"test_failure\"") &&
                    mentions(log, "\"participant\":\"test.counting\""));
    RAWFRAME_EXPECT(mentions(log, "\"reason\":\"unhealthy\"") && mentions(log, "\"exit\":2"));
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

RAWFRAME_TEST(StartupFailuresAreReportedAndNothingRuns) {
    reset();
    std::string log;
    RAWFRAME_EXPECT(run("host.iteration_rate = 0", log) == host::HostExit::StartupFailed);
    RAWFRAME_EXPECT(mentions(log, "\"code\":\"bad_configuration\"") && mentions(log, "host.iteration_rate"));
    RAWFRAME_EXPECT(movesThrough(log, {"starting", "failed"}) && !mentions(log, "\"state\":\"preparing\""));

    log.clear();
    RAWFRAME_EXPECT(run("diagnostics.minimum_severity = loud", log) == host::HostExit::StartupFailed);

    log.clear();
    requireMissing = true;
    RAWFRAME_EXPECT(run("host.maximum_iterations = 3", log) == host::HostExit::StartupFailed);
    RAWFRAME_EXPECT(mentions(log, "\"code\":\"plan_problem\"") && mentions(log, "missing_provider"));
    RAWFRAME_EXPECT(counts.started == 0 && counts.runWorlds == 0);
    RAWFRAME_EXPECT(movesThrough(log, {"starting", "preparing", "failed"}) && !mentions(log, "\"state\":\"ready\""));
}
