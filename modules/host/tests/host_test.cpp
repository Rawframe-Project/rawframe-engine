// The Host end to end: configuration, composition, the Host schedule, the log,
// and orderly stop, driven by a participant that counts its phases.

#include "rawframe/composition/composition.h"
#include "rawframe/host/host.h"
#include "rawframe/test/test.h"

#include <array>
#include <atomic>
#include <chrono>
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
};

Counts counts;

class Counting final : public composition::Participant {
public:
    result::Status start(composition::ParticipantContext&) noexcept override {
        ++counts.started;
        return {};
    }
    void stop() noexcept override {
        ++counts.stopped;
    }
    void runHostPhase(HostPhase phase, const composition::HostFrame&) noexcept override {
        if (phase == HostPhase::RunWorlds) {
            ++counts.runWorlds;
        } else if (phase == HostPhase::Maintenance) {
            ++counts.maintenance;
        }
    }
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
}

RAWFRAME_TEST(AStopRequestEndsAnUnboundedRun) {
    reset();
    std::string log;
    std::atomic<bool> stop{false};
    std::thread requester{[&stop] {
        std::this_thread::sleep_for(std::chrono::milliseconds(30));
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

    log.clear();
    RAWFRAME_EXPECT(run("diagnostics.minimum_severity = loud", log) == host::HostExit::StartupFailed);

    log.clear();
    requireMissing = true;
    RAWFRAME_EXPECT(run("host.maximum_iterations = 3", log) == host::HostExit::StartupFailed);
    RAWFRAME_EXPECT(mentions(log, "\"code\":\"plan_problem\"") && mentions(log, "missing_provider"));
    RAWFRAME_EXPECT(counts.started == 0 && counts.runWorlds == 0);
}
