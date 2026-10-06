// Avoidance (D411): agents steering round each other by ORCA, the same
// velocities in any order, and settings and agents out of range refused.

#include "rawframe/navigation/avoidance.h"
#include "rawframe/test/test.h"

#include <array>
#include <cmath>
#include <vector>

using namespace rawframe;
using namespace rawframe::navigation;

namespace {

/// Two agents walking at each other along x, a metre a second, each half a
/// meter round, stepped until they would have met and passed.
struct HeadOn {
    std::array<AvoidingAgent, 2> agents{
        AvoidingAgent{
            .position = {.x = -4, .y = 0}, .preferred = {.x = 1, .y = 0}, .radius = 0.5, .maxSpeed = 1.5, .id = 1},
        AvoidingAgent{
            .position = {.x = 4, .y = 0}, .preferred = {.x = -1, .y = 0}, .radius = 0.5, .maxSpeed = 1.5, .id = 2}};
    double closest = 100;

    void walk(Avoidance& avoidance, int steps) {
        std::array<Ground, 2> velocities{};
        for (int step = 0; step < steps; ++step) {
            RAWFRAME_EXPECT(avoidance.avoid(agents, 0.1, velocities).has_value());
            for (std::size_t at = 0; at < agents.size(); ++at) {
                agents[at].velocity = velocities[at];
                agents[at].position.x += velocities[at].x * 0.1;
                agents[at].position.y += velocities[at].y * 0.1;
            }
            closest = std::min(
                closest,
                std::hypot(agents[0].position.x - agents[1].position.x, agents[0].position.y - agents[1].position.y));
        }
    }
};

} // namespace

RAWFRAME_TEST(AgentsWalkingAtEachOtherPassWithoutTouching) {
    auto avoidance = Avoidance::create({});
    RAWFRAME_EXPECT(avoidance.has_value());
    if (!avoidance.has_value()) {
        return;
    }
    HeadOn walked;
    walked.walk(**avoidance, 120);
    // Never closer than their radii, and each past where the other began.
    RAWFRAME_EXPECT(walked.closest >= 0.99);
    RAWFRAME_EXPECT(walked.agents[0].position.x > 3 && walked.agents[1].position.x < -3);

    // Alone, an agent keeps its preferred velocity, its maximum held.
    const std::array<AvoidingAgent, 1> kAlone{
        AvoidingAgent{.position = {}, .preferred = {.x = 3, .y = 4}, .radius = 0.5, .maxSpeed = 2.5, .id = 7}};
    std::array<Ground, 1> velocity{};
    RAWFRAME_EXPECT((*avoidance)->avoid(kAlone, 0.1, velocity).has_value());
    RAWFRAME_EXPECT(std::abs(std::hypot(velocity[0].x, velocity[0].y) - 2.5) < 1e-6);
}

RAWFRAME_TEST(TheSameAgentsGiveTheSameVelocitiesInAnyOrder) {
    auto avoidance = *Avoidance::create({});
    std::vector<AvoidingAgent> agents;
    for (int at = 0; at < 12; ++at) {
        const double kTurn = at * 0.5235987755982988;
        agents.push_back(AvoidingAgent{.position = {.x = 5 * std::cos(kTurn), .y = 5 * std::sin(kTurn)},
                                       .preferred = {.x = -std::cos(kTurn), .y = -std::sin(kTurn)},
                                       .radius = 0.4,
                                       .maxSpeed = 1.2,
                                       .id = static_cast<std::uint64_t>(at + 1)});
    }
    std::vector<Ground> forward(agents.size());
    RAWFRAME_EXPECT(avoidance->avoid(agents, 0.1, forward).has_value());
    std::vector<AvoidingAgent> reversed{agents.rbegin(), agents.rend()};
    std::vector<Ground> backward(agents.size());
    RAWFRAME_EXPECT(avoidance->avoid(reversed, 0.1, backward).has_value());
    for (std::size_t at = 0; at < agents.size(); ++at) {
        const Ground& kOther = backward[agents.size() - 1 - at];
        RAWFRAME_EXPECT(forward[at].x == kOther.x && forward[at].y == kOther.y);
    }
}

RAWFRAME_TEST(SettingsAndAgentsOutOfRangeAreRefused) {
    RAWFRAME_EXPECT(!Avoidance::create({.maximumAgents = 0}).has_value());
    RAWFRAME_EXPECT(!Avoidance::create({.neighbours = 257}).has_value());
    auto avoidance = *Avoidance::create({.maximumAgents = 1});
    std::array<Ground, 2> velocities{};
    const std::array<AvoidingAgent, 2> kTwo{AvoidingAgent{.id = 1}, AvoidingAgent{.position = {.x = 2}, .id = 2}};
    RAWFRAME_EXPECT(!avoidance->avoid(kTwo, 0.1, velocities).has_value());
    const std::array<AvoidingAgent, 1> kNoRadius{AvoidingAgent{.radius = 0, .id = 1}};
    std::array<Ground, 1> one{};
    RAWFRAME_EXPECT(!avoidance->avoid(kNoRadius, 0.1, one).has_value());
    // One velocity for each agent.
    RAWFRAME_EXPECT(!avoidance->avoid(std::span{kNoRadius}, 0.1, std::span{velocities}).has_value());
}
