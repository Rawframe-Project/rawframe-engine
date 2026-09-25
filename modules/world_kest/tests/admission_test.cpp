// A game's own admission rule: what it answers reaches the client as the
// reason, a rule that fails or answers out of turn refuses as unavailable,
// and a rule of the wrong shape stops the game loading.

#include "game_harness.h"
#include "rawframe/test/test.h"
#include "rawframe/world_kest/game.h"
#include "rawframe/world_replication/plan.h"

#include <string>
#include <string_view>

using namespace rawframe;

namespace {

world_replication::ReplicationPlan* plan = nullptr;

result::Result<composition::ParticipantOwner> makeAsker(composition::ParticipantContext& context) noexcept {
    RAWFRAME_TRY_ASSIGN(plan, context.capability(world_replication::kReplicationPlan));
    struct Asker final : composition::Participant {};
    return composition::ParticipantOwner{new Asker{}};
}

constexpr std::string_view kNeeds[] = {world_replication::kReplicationPlan.name};

void registerAsker(composition::ParticipantRegistrar& registrar) noexcept {
    registrar.submit(composition::ParticipantDeclaration{
        .identity = "test.asker",
        .factory = &makeAsker,
        .scope = composition::LifetimeScope::World,
        .requiredCapabilities = kNeeds,
        .lifecycle = {.stopBudget = execution::MonotonicDuration::fromMilliseconds(10)},
    });
}

const std::array<composition::RegistrarEntry, 3> kAsked = {
    game_test::kRegistrars[0],
    game_test::kRegistrars[1],
    composition::RegistrarEntry{"test", &registerAsker, world_runtime::kScopes}};

std::vector<std::byte> bytes(std::string_view text) {
    std::vector<std::byte> made;
    for (const char kCharacter : text) {
        made.push_back(static_cast<std::byte>(kCharacter));
    }
    return made;
}

network::Hello hello(std::string_view session, std::string_view ticket) {
    return network::Hello{.requestedSession = bytes(session), .ticket = bytes(ticket)};
}

std::optional<network::RejectReason> verdict(std::string_view session, std::string_view ticket) {
    const auto kRefusal = plan->admit(hello(session, ticket));
    return kRefusal.has_value() ? std::optional{kRefusal->reason} : std::nullopt;
}

struct Run {
    execution::ManualClock clock;
    execution::CancellationScope root{clock};
    std::optional<composition::Plan> composed;
    std::optional<composition::Configuration> configuration;
    std::optional<composition::Composition> composition;

    result::Status start(std::string_view game) {
        std::vector<composition::Problem> problems;
        auto made = composition::compose(
            composition::CompositionRequest{.registrars = kAsked,
                                            .shutdownBudget = execution::MonotonicDuration::fromSeconds(1)},
            problems);
        RAWFRAME_EXPECT(made.has_value());
        composed = std::move(*made);
        configuration = *composition::Configuration::parse("kest.game = game/" + std::string{game} + "\n");
        composition.emplace(
            *composed,
            composition::HostServices{
                .clock = &clock, .scope = &root, .configuration = &*configuration, .files = &game_test::heldGames()});
        return composition->start();
    }
};

} // namespace

RAWFRAME_TEST(AnAdmissionLineIsParsedAndChecked) {
    const auto kGame = world_kest::parseGame(
        "program p.kest\ncomponent 2b7d4e61-0c93-4a58-b1f2-7e9a3c5d8f04 g.s S\nreplicate g.s\nadmission admit\n");
    RAWFRAME_EXPECT(kGame.has_value() && kGame->admission == "admit");
    // Twice, with nothing or too much, and without a networked game.
    RAWFRAME_EXPECT(!world_kest::parseGame("program p.kest\nadmission a\nadmission b\n").has_value());
    RAWFRAME_EXPECT(!world_kest::parseGame("program p.kest\nadmission\n").has_value());
    RAWFRAME_EXPECT(!world_kest::parseGame("program p.kest\nadmission a b\n").has_value());
    RAWFRAME_EXPECT(!world_kest::parseGame("program p.kest\nadmission a\n").has_value());
}

RAWFRAME_TEST(AGamesRuleDecidesWhoJoins) {
    Run run;
    const auto kStarted = run.start("gate.game");
    RAWFRAME_EXPECT(kStarted.has_value() && plan != nullptr);
    if (!kStarted.has_value() || plan == nullptr) {
        return;
    }
    RAWFRAME_EXPECT(verdict("solo", "pass") == std::nullopt);
    RAWFRAME_EXPECT(verdict("solo", "nope") == network::RejectReason::TicketInvalid);
    RAWFRAME_EXPECT(verdict("solo", "") == network::RejectReason::TicketInvalid);
    RAWFRAME_EXPECT(verdict("full", "pass") == network::RejectReason::Capacity);
    RAWFRAME_EXPECT(verdict("late", "pass") == network::RejectReason::Unavailable);
    // A rule that runs out of fuel, or answers what it may not, keeps the
    // client out; the next hello is asked afresh.
    RAWFRAME_EXPECT(verdict("loop", "pass") == network::RejectReason::Unavailable);
    RAWFRAME_EXPECT(verdict("odd ", "pass") == network::RejectReason::Unavailable);
    RAWFRAME_EXPECT(verdict("solo", "pass") == std::nullopt);
    run.composition->stop();
    plan = nullptr;
}

RAWFRAME_TEST(ARuleOfTheWrongShapeStopsTheGameLoading) {
    Run run;
    const auto kStarted = run.start("misadmitted.game");
    RAWFRAME_EXPECT(!kStarted.has_value());
    plan = nullptr;
}
