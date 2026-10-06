#include "rawframe/navigation/avoidance.h"

#include "rawframe/navigation/errors.h"

#include <maul-nav/avoidance.h>
#include <string>
#include <vector>

namespace rawframe::navigation {

namespace {

std::unexpected<result::Error> refuse(NavigationError error, std::string_view why) {
    const result::ErrorClass kClass =
        error == NavigationError::Invalid ? result::ErrorClass::InvalidArgument : result::ErrorClass::ResourceExhausted;
    return std::unexpected<result::Error>{result::fail(kClass, kNavigationDomain, code(error), why).error()};
}

result::Status checked(mnavResult outcome, std::string_view why) {
    switch (outcome) {
    case mnav_success:
        return {};
    case mnav_errorLimit:
        return refuse(NavigationError::Limit, why);
    case mnav_errorCapacity:
        return refuse(NavigationError::Capacity, why);
    default:
        return std::unexpected<result::Error>{
            refuse(NavigationError::Invalid, why).error().withContext("outcome", std::to_string(outcome))};
    }
}

mnavPos2 groundOf(const Ground& ground) noexcept {
    return mnavPos2{.x = ground.x, .y = ground.y};
}

} // namespace

struct Avoidance::State {
    mnavAvoidance* set = nullptr;
    std::size_t maximumAgents = 0;
    std::vector<mnavAgent> agents;
    std::vector<mnavPos2> velocities;

    ~State() {
        mnavDestroyAvoidance(set);
    }
};

result::Result<std::unique_ptr<Avoidance>> Avoidance::create(const AvoidanceSettings& settings) {
    if (settings.maximumAgents < 1 || settings.maximumAgents > MNAV_MAX_AVOIDANCE_AGENTS || settings.neighbours < 1 ||
        settings.neighbours > MNAV_MAX_AVOIDANCE_NEIGHBORS) {
        return refuse(NavigationError::Invalid, "avoidance holds 1 to 1,048,576 agents, each avoiding 1 to 256");
    }
    mnavAvoidanceDef def = mnavDefaultAvoidanceDef();
    def.limits.agents = static_cast<std::int32_t>(settings.maximumAgents);
    def.limits.neighbors = static_cast<std::int32_t>(settings.neighbours);
    // Agents avoid each other only: the navmesh keeps them off what stands.
    def.limits.obstacleVertices = 0;
    def.neighborDistance = settings.neighbourDistance;
    def.timeHorizon = settings.timeHorizon;
    auto state = std::make_unique<State>();
    RAWFRAME_TRY(checked(mnavCreateAvoidance(&def, &state->set), "avoidance's settings were refused"));
    state->maximumAgents = settings.maximumAgents;
    return std::unique_ptr<Avoidance>{new Avoidance{std::move(state)}};
}

Avoidance::Avoidance(std::unique_ptr<State> state) noexcept : state_(std::move(state)) {
}

Avoidance::~Avoidance() = default;

result::Status Avoidance::avoid(std::span<const AvoidingAgent> agents, double seconds, std::span<Ground> velocities) {
    State& state = *state_;
    if (velocities.size() != agents.size()) {
        return refuse(NavigationError::Invalid, "one velocity for each agent");
    }
    if (agents.size() > state.maximumAgents) {
        return refuse(NavigationError::Limit, "more agents than avoidance holds");
    }
    state.agents.clear();
    for (const AvoidingAgent& agent : agents) {
        state.agents.push_back(mnavAgent{.position = groundOf(agent.position),
                                         .velocity = groundOf(agent.velocity),
                                         .preferred = groundOf(agent.preferred),
                                         .radius = agent.radius,
                                         .maxSpeed = agent.maxSpeed,
                                         .priority = agent.priority,
                                         .id = agent.id});
    }
    state.velocities.resize(agents.size());
    RAWFRAME_TRY(checked(mnavAvoid(state.set,
                                   state.agents.data(),
                                   static_cast<std::int32_t>(state.agents.size()),
                                   nullptr,
                                   0,
                                   seconds,
                                   state.velocities.data()),
                         "an agent or the step was refused"));
    for (std::size_t at = 0; at < velocities.size(); ++at) {
        velocities[at] = Ground{.x = state.velocities[at].x, .y = state.velocities[at].y};
    }
    return {};
}

} // namespace rawframe::navigation
