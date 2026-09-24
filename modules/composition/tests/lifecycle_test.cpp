// Running a plan: construction and start order, exact reverse rollback at
// every participant position, repeated lifecycle, typed capabilities, and
// shutdown budgets.

#include "fixtures.h"
#include "rawframe/diagnostics/router.h"
#include "rawframe/test/test.h"

#include <array>
#include <string>
#include <vector>

using namespace rawframe::composition;
using namespace rawframe::composition::testing;
using rawframe::execution::CancellationScope;
using rawframe::execution::ManualClock;

namespace {

const std::array<RegistrarEntry, 1> kModuleA = {RegistrarEntry{"module_a", &registerModuleA, kAllScopes}};

/// Three independent participants: plan order is identity order.
Plan threeParticipants() {
    resetFixture().moduleA = {declare("a"), declare("b"), declare("c")};
    std::vector<Problem> problems;
    auto plan = compose(request(kModuleA), problems);
    RAWFRAME_EXPECT(plan.has_value());
    return plan.has_value() ? std::move(*plan) : Plan{};
}

std::vector<std::string> expectedUnwind(std::size_t constructed, std::size_t started) {
    const std::array<std::string, 3> kNames = {"a", "b", "c"};
    std::vector<std::string> events;
    for (std::size_t index = started; index-- > 0;) {
        events.push_back("quiesce " + kNames[index]);
    }
    for (std::size_t index = started; index-- > 0;) {
        events.push_back("stop " + kNames[index]);
    }
    for (std::size_t index = constructed; index-- > 0;) {
        events.push_back("destroy " + kNames[index]);
    }
    return events;
}

} // namespace

RAWFRAME_TEST(StartRunsInPlanOrderAndStopInReverse) {
    const Plan kPlan = threeParticipants();
    ManualClock clock;
    CancellationScope root{clock};
    {
        Composition composition{kPlan, HostServices{.clock = &clock, .scope = &root}};
        RAWFRAME_EXPECT(composition.start().has_value());
        RAWFRAME_EXPECT(composition.running());
        RAWFRAME_EXPECT(composition.state("b") == ParticipantState::Running);
    }
    std::vector<std::string> expected = {"construct a", "construct b", "construct c", "start a", "start b", "start c"};
    for (const auto& event : expectedUnwind(3, 3)) {
        expected.push_back(event);
    }
    RAWFRAME_EXPECT(fixture().journal == expected);
    RAWFRAME_EXPECT(fixture().quiesceSawCancellation);
    RAWFRAME_EXPECT(!root.cancelled());
}

RAWFRAME_TEST(AFailedConstructionUnwindsExactlyWhatWasBuilt) {
    const std::array<std::string, 3> kNames = {"a", "b", "c"};
    for (std::size_t failAt = 0; failAt < kNames.size(); ++failAt) {
        const Plan kPlan = threeParticipants();
        fixture().failConstruct = kNames[failAt];
        ManualClock clock;
        CancellationScope root{clock};
        Composition composition{kPlan, HostServices{.clock = &clock, .scope = &root}};
        const auto kStarted = composition.start();
        RAWFRAME_EXPECT(!kStarted.has_value() && kStarted.error().code() == kInjected);
        RAWFRAME_EXPECT(!composition.running());

        std::vector<std::string> expected;
        for (std::size_t index = 0; index < failAt; ++index) {
            expected.push_back("construct " + kNames[index]);
        }
        for (const auto& event : expectedUnwind(failAt, 0)) {
            expected.push_back(event);
        }
        RAWFRAME_EXPECT(fixture().journal == expected);
    }
}

RAWFRAME_TEST(AFailedStartUnwindsInReverseIncludingTheFailure) {
    const std::array<std::string, 3> kNames = {"a", "b", "c"};
    for (std::size_t failAt = 0; failAt < kNames.size(); ++failAt) {
        const Plan kPlan = threeParticipants();
        fixture().failStart = kNames[failAt];
        ManualClock clock;
        CancellationScope root{clock};
        Composition composition{kPlan, HostServices{.clock = &clock, .scope = &root}};
        const auto kStarted = composition.start();
        RAWFRAME_EXPECT(!kStarted.has_value() && kStarted.error().code() == kInjected);

        std::vector<std::string> expected = {"construct a", "construct b", "construct c"};
        for (std::size_t index = 0; index <= failAt; ++index) {
            expected.push_back("start " + kNames[index]);
        }
        // The participant whose start failed may own work, so it quiesces too.
        for (const auto& event : expectedUnwind(3, failAt + 1)) {
            expected.push_back(event);
        }
        RAWFRAME_EXPECT(fixture().journal == expected);
        // Unwinding twice is harmless.
        composition.stop();
        RAWFRAME_EXPECT(fixture().journal == expected);
    }
}

namespace {

struct Counter {
    int value = 0;
};

constexpr Capability<Counter> kCounter{"counter"};
constexpr Capability<int> kWrongType{"counter"};

class CounterProvider final : public Participant {
public:
    CapabilityObject provide(std::string_view capability) noexcept override {
        return capability == kCounter.name ? provideAs(counter_) : CapabilityObject{};
    }

private:
    Counter counter_;
};

rawframe::result::Result<ParticipantOwner> makeProvider(ParticipantContext&) noexcept {
    return ParticipantOwner{new CounterProvider};
}

rawframe::result::Result<ParticipantOwner> makeConsumer(ParticipantContext& context) noexcept {
    RAWFRAME_TRY_ASSIGN(Counter* const counter, context.capability(kCounter));
    ++counter->value;
    fixture().journal.push_back("counted " + std::to_string(counter->value));
    // Admitted with its declared quota; it may submit work from start on.
    RAWFRAME_EXPECT(context.cpuExecutor() != nullptr);
    RAWFRAME_EXPECT(context.blockingIoExecutor() == nullptr);
    return ParticipantOwner{new Participant};
}

rawframe::result::Result<ParticipantOwner> makeConfused(ParticipantContext& context) noexcept {
    RAWFRAME_TRY_ASSIGN(int* const wrong, context.capability(kWrongType));
    static_cast<void>(wrong);
    return ParticipantOwner{new Participant};
}

constexpr std::string_view kCounterName[] = {"counter"};

Plan counterPlan(FactoryFunction consumerFactory) {
    Fixture& setup = resetFixture();
    auto provider = declare("provider");
    provider.factory = &makeProvider;
    provider.providedCapabilities = kCounterName;
    auto consumer = declare("consumer");
    consumer.factory = consumerFactory;
    consumer.requiredCapabilities = kCounterName;
    consumer.executor = {.cpu = true, .quota = {.maximumPendingTasks = 4}};
    setup.moduleA = {provider, consumer};
    std::vector<Problem> problems;
    auto plan = compose(request(kModuleA), problems);
    RAWFRAME_EXPECT(plan.has_value());
    return plan.has_value() ? std::move(*plan) : Plan{};
}

} // namespace

RAWFRAME_TEST(CapabilitiesArriveTypedAndRepeatedLifecyclesLeakNothing) {
    const Plan kPlan = counterPlan(&makeConsumer);
    ManualClock clock;
    CancellationScope root{clock};
    rawframe::execution::Executor cpu{
        rawframe::execution::ExecutorSettings{.kind = rawframe::execution::ExecutorKind::Cpu, .workers = 1}};
    Composition composition{kPlan, HostServices{.clock = &clock, .scope = &root, .cpu = &cpu}};
    // Each start is a new generation of participants; the executor owner is
    // retired at each stop, so it can be admitted again.
    for (int round = 0; round < 3; ++round) {
        RAWFRAME_EXPECT(composition.start().has_value());
        composition.stop();
    }
    RAWFRAME_EXPECT((fixture().journal == std::vector<std::string>{"counted 1", "counted 1", "counted 1"}));
    RAWFRAME_EXPECT(cpu.admitOwner(Composition::ownerFor("consumer"), {.maximumPendingTasks = 1}).has_value());
}

RAWFRAME_TEST(AWrongCapabilityTypeFailsTyped) {
    const Plan kPlan = counterPlan(&makeConfused);
    ManualClock clock;
    CancellationScope root{clock};
    rawframe::execution::Executor cpu{
        rawframe::execution::ExecutorSettings{.kind = rawframe::execution::ExecutorKind::Cpu, .workers = 1}};
    Composition composition{kPlan, HostServices{.clock = &clock, .scope = &root, .cpu = &cpu}};
    const auto kStarted = composition.start();
    RAWFRAME_EXPECT(!kStarted.has_value() && kStarted.error().code() == code(CompositionError::CapabilityTypeMismatch));
}

namespace {

struct Captured final : rawframe::diagnostics::Sink {
    std::vector<std::string> codes;
    void accept(const rawframe::diagnostics::Record& record) noexcept override {
        codes.emplace_back(record.identity.code);
    }
};

} // namespace

RAWFRAME_TEST(AnOverrunStopIsReportedNotFatal) {
    const Plan kPlan = threeParticipants();
    ManualClock clock;
    fixture().clock = &clock;
    fixture().stopTakes = rawframe::execution::MonotonicDuration::fromMilliseconds(50);
    CancellationScope root{clock};
    Captured sink;
    rawframe::diagnostics::Sink* const kSinks[] = {&sink};
    rawframe::diagnostics::Router router{rawframe::diagnostics::RouterSettings{}, kSinks};
    {
        Composition composition{kPlan, HostServices{.clock = &clock, .scope = &root, .emitter = router.emitter()}};
        RAWFRAME_EXPECT(composition.start().has_value());
    }
    RAWFRAME_EXPECT((sink.codes == std::vector<std::string>(3, "participant_stop_overran")));
}
