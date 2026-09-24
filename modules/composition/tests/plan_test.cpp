// Plan construction: registrar behaviour, deterministic order, and every
// SPEC-0005 validation failure, all found before anything is constructed.

#include "fixtures.h"
#include "rawframe/test/test.h"

#include <array>
#include <string>
#include <type_traits>

using namespace rawframe::composition;
using namespace rawframe::composition::testing;

static_assert(!std::is_copy_constructible_v<ParticipantRegistrar> &&
                  !std::is_move_constructible_v<ParticipantRegistrar>,
              "a registrar's builder can be neither copied nor moved");

namespace {

constexpr std::string_view kClock[] = {"clock"};
constexpr std::string_view kWorld[] = {"world"};
constexpr std::string_view kRender[] = {"render"};

const std::array<RegistrarEntry, 2> kBoth = {
    RegistrarEntry{"module_a", &registerModuleA, kAllScopes},
    RegistrarEntry{"module_b", &registerModuleB, kAllScopes},
};

} // namespace

RAWFRAME_TEST(OrderIsDependencyThenScopeThenIdentity) {
    Fixture& setup = resetFixture();
    auto zeta = declare("zeta", LifetimeScope::Host);
    zeta.providedCapabilities = kClock;
    auto alpha = declare("alpha");
    alpha.requiredCapabilities = kClock;
    auto gamma = declare("gamma", LifetimeScope::World);
    constexpr std::string_view kAlpha[] = {"alpha"};
    gamma.requiredParticipants = kAlpha;
    setup.moduleA = {gamma, alpha};
    setup.moduleB = {declare("beta"), zeta};

    std::vector<Problem> problems;
    const auto kPlan = compose(request(kBoth), problems);
    RAWFRAME_EXPECT(kPlan.has_value() && problems.empty());
    if (!kPlan.has_value()) {
        return;
    }
    RAWFRAME_EXPECT((identities(*kPlan) == std::vector<std::string>{"zeta", "alpha", "beta", "gamma"}));
    const auto kParticipants = kPlan->participants();
    RAWFRAME_EXPECT(kParticipants[1].capabilities.size() == 1 && kParticipants[1].capabilities[0].provider == 0);
    RAWFRAME_EXPECT(kParticipants[3].dependencies == std::vector<std::size_t>{1});
    RAWFRAME_EXPECT(kParticipants[0].owningModule == "module_b");
    // Registration constructs nothing.
    RAWFRAME_EXPECT(setup.factoryCalls == 0);
}

RAWFRAME_TEST(RegistrarOrderCarriesNoMeaning) {
    Fixture& setup = resetFixture();
    auto provider = declare("provider");
    provider.providedCapabilities = kWorld;
    auto consumer = declare("consumer");
    consumer.requiredCapabilities = kWorld;
    setup.moduleA = {consumer, declare("other")};
    setup.moduleB = {provider};
    const std::array<RegistrarEntry, 2> kReversed = {kBoth[1], kBoth[0]};
    std::vector<Problem> problems;
    const auto kForward = compose(request(kBoth), problems);
    const auto kBackward = compose(request(kReversed), problems);
    RAWFRAME_EXPECT(kForward.has_value() && kBackward.has_value());
    if (kForward.has_value() && kBackward.has_value()) {
        RAWFRAME_EXPECT(identities(*kForward) == identities(*kBackward));
    }
}

RAWFRAME_TEST(EveryBadDeclarationIsReportedAfterThePass) {
    Fixture& setup = resetFixture();
    auto noFactory = declare("no_factory");
    noFactory.factory = nullptr;
    auto noBudget = declare("no_budget");
    noBudget.lifecycle.stopBudget = {};
    setup.moduleA = {declare(""), noFactory, noBudget, declare("host_thing", LifetimeScope::Host)};
    const std::array<RegistrarEntry, 1> kWorldOnly = {
        RegistrarEntry{"module_a", &registerModuleA, scopeBit(LifetimeScope::Runtime)}};
    std::vector<Problem> problems;
    const auto kPlan = compose(request(kWorldOnly), problems);
    RAWFRAME_EXPECT(!kPlan.has_value());
    RAWFRAME_EXPECT((kinds(problems) == std::vector<ProblemKind>{ProblemKind::EmptyIdentity,
                                                                 ProblemKind::MissingFactory,
                                                                 ProblemKind::MissingShutdownContract,
                                                                 ProblemKind::ScopeNotAllowed}));
    if (!kPlan.has_value()) {
        RAWFRAME_EXPECT(kPlan.error().errorClass() == rawframe::result::ErrorClass::FailedPrecondition);
        RAWFRAME_EXPECT(kPlan.error().code() == code(CompositionError::PlanRefused));
    }
}

namespace {

ParticipantRegistrar* retained = nullptr;

void retainingRegistrar(ParticipantRegistrar& registrar) noexcept {
    retained = &registrar;
    registrar.submit(declare("kept"));
}

void misusingRegistrar(ParticipantRegistrar& registrar) noexcept {
    registrar.submit(declare("fine"));
    // Through the first module's builder, after its registrar returned.
    retained->submit(declare("smuggled"));
}

} // namespace

RAWFRAME_TEST(ASubmitAfterReturnIsRefusedAndChangesNothing) {
    resetFixture();
    const std::array<RegistrarEntry, 2> kEntries = {
        RegistrarEntry{"module_b", &misusingRegistrar, kAllScopes},
        RegistrarEntry{"module_a", &retainingRegistrar, kAllScopes},
    };
    std::vector<Problem> problems;
    const auto kPlan = compose(request(kEntries), problems);
    RAWFRAME_EXPECT(!kPlan.has_value());
    RAWFRAME_EXPECT((kinds(problems) == std::vector<ProblemKind>{ProblemKind::SubmitAfterClose}));
    RAWFRAME_EXPECT(problems.size() == 1 && problems[0].subject == "module_a");
}

namespace {

void localStorageRegistrar(ParticipantRegistrar& registrar) noexcept {
    std::string identity = "original";
    std::string capability = "provided";
    const std::string_view kProvided[] = {capability};
    auto declaration = declare(identity);
    declaration.providedCapabilities = kProvided;
    registrar.submit(declaration);
    identity.assign("mutated!");
    capability.assign("changed!");
}

} // namespace

RAWFRAME_TEST(SubmitCopiesTheDeclaration) {
    resetFixture();
    const std::array<RegistrarEntry, 1> kEntries = {RegistrarEntry{"module_a", &localStorageRegistrar, kAllScopes}};
    std::vector<Problem> problems;
    const auto kPlan = compose(request(kEntries), problems);
    RAWFRAME_EXPECT(kPlan.has_value());
    if (kPlan.has_value()) {
        RAWFRAME_EXPECT(kPlan->participants()[0].identity == "original");
        RAWFRAME_EXPECT(kPlan->participants()[0].providedCapabilities == std::vector<std::string>{"provided"});
    }
}

RAWFRAME_TEST(ProvidersMustBeUniqueUnlessSelected) {
    Fixture& setup = resetFixture();
    auto first = declare("first");
    first.providedCapabilities = kWorld;
    auto second = declare("second");
    second.providedCapabilities = kWorld;
    auto consumer = declare("consumer");
    consumer.requiredCapabilities = kWorld;
    auto orphan = declare("orphan");
    orphan.requiredCapabilities = kClock;
    setup.moduleA = {first, consumer};
    setup.moduleB = {second, orphan};

    std::vector<Problem> problems;
    RAWFRAME_EXPECT(!compose(request(kBoth), problems).has_value());
    RAWFRAME_EXPECT(
        (kinds(problems) == std::vector<ProblemKind>{ProblemKind::AmbiguousProvider, ProblemKind::MissingProvider}));

    // Selecting one provider leaves the other out of the plan.
    setup.moduleB = {second};
    const Selection kSelections[] = {{"world", "second"}};
    auto selecting = request(kBoth);
    selecting.selections = kSelections;
    problems.clear();
    const auto kPlan = compose(selecting, problems);
    RAWFRAME_EXPECT(kPlan.has_value());
    if (kPlan.has_value()) {
        RAWFRAME_EXPECT((identities(*kPlan) == std::vector<std::string>{"second", "consumer"}));
    }

    const Selection kWrong[] = {{"world", "consumer"}};
    selecting.selections = kWrong;
    problems.clear();
    RAWFRAME_EXPECT(!compose(selecting, problems).has_value());
    RAWFRAME_EXPECT(kinds(problems).front() == ProblemKind::UnknownSelection);
}

RAWFRAME_TEST(ADedicatedServerRefusesPresentation) {
    Fixture& setup = resetFixture();
    auto renderer = declare("renderer");
    renderer.providedCapabilities = kRender;
    renderer.eligibility.roles = only(TargetRole::Client);
    auto scene = declare("scene");
    scene.requiredCapabilities = kRender;
    setup.moduleA = {renderer, scene};
    auto server = request(kBoth);
    server.role = TargetRole::DedicatedServer;
    std::vector<Problem> problems;
    RAWFRAME_EXPECT(!compose(server, problems).has_value());
    RAWFRAME_EXPECT((kinds(problems) == std::vector<ProblemKind>{ProblemKind::IneligibleProvider}));

    // Alone, an ineligible participant is simply absent.
    setup.moduleA = {renderer, declare("simulation")};
    problems.clear();
    const auto kPlan = compose(server, problems);
    RAWFRAME_EXPECT(kPlan.has_value());
    if (kPlan.has_value()) {
        RAWFRAME_EXPECT(identities(*kPlan) == std::vector<std::string>{"simulation"});
    }
}

RAWFRAME_TEST(LongerLivedParticipantsCannotDependOnShorterLived) {
    Fixture& setup = resetFixture();
    auto world = declare("world_state", LifetimeScope::World);
    world.providedCapabilities = kWorld;
    auto host = declare("host_service", LifetimeScope::Host);
    host.requiredCapabilities = kWorld;
    setup.moduleA = {world, host};
    std::vector<Problem> problems;
    RAWFRAME_EXPECT(!compose(request(kBoth), problems).has_value());
    RAWFRAME_EXPECT((kinds(problems) == std::vector<ProblemKind>{ProblemKind::LifetimeViolation}));
}

RAWFRAME_TEST(CyclesAreRefused) {
    Fixture& setup = resetFixture();
    auto left = declare("left");
    left.providedCapabilities = kClock;
    left.requiredCapabilities = kWorld;
    auto right = declare("right");
    right.providedCapabilities = kWorld;
    right.requiredCapabilities = kClock;
    setup.moduleA = {left, right, declare("bystander")};
    std::vector<Problem> problems;
    RAWFRAME_EXPECT(!compose(request(kBoth), problems).has_value());
    RAWFRAME_EXPECT(
        (kinds(problems) == std::vector<ProblemKind>{ProblemKind::DependencyCycle, ProblemKind::DependencyCycle}));
}

RAWFRAME_TEST(MissingExecutorsParticipantsAndBudgetsAreRefused) {
    Fixture& setup = resetFixture();
    auto io = declare("io_user");
    io.executor.blockingIo = true;
    io.executor.quota.maximumPendingTasks = 4;
    auto needy = declare("needy");
    constexpr std::string_view kGhost[] = {"ghost"};
    needy.requiredParticipants = kGhost;
    auto slow = declare("slow");
    slow.lifecycle.stopBudget = rawframe::execution::MonotonicDuration::fromMilliseconds(995);
    setup.moduleA = {io, needy, slow};
    auto noIo = request(kBoth);
    noIo.blockingIoExecutor = false;
    std::vector<Problem> problems;
    RAWFRAME_EXPECT(!compose(noIo, problems).has_value());
    RAWFRAME_EXPECT((kinds(problems) == std::vector<ProblemKind>{ProblemKind::MissingExecutor,
                                                                 ProblemKind::MissingParticipant,
                                                                 ProblemKind::BudgetNotNested}));
}

RAWFRAME_TEST(DuplicatesAreRefusedNeverOverwritten) {
    Fixture& setup = resetFixture();
    setup.moduleA = {declare("same")};
    setup.moduleB = {declare("same")};
    const std::array<RegistrarEntry, 3> kEntries = {kBoth[0], kBoth[1], kBoth[0]};
    std::vector<Problem> problems;
    RAWFRAME_EXPECT(!compose(request(kEntries), problems).has_value());
    RAWFRAME_EXPECT(
        (kinds(problems) == std::vector<ProblemKind>{ProblemKind::DuplicateRegistrar, ProblemKind::DuplicateIdentity}));
}

RAWFRAME_TEST(NoRegistrarsMeansAnEmptyPlan) {
    resetFixture();
    std::vector<Problem> problems;
    const auto kPlan = compose(request({}), problems);
    RAWFRAME_EXPECT(kPlan.has_value() && kPlan->participants().empty());
}
