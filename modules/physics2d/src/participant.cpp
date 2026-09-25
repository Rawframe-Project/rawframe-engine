#include "rawframe/composition/composition.h"
#include "rawframe/physics2d/physics.h"
#include "rawframe/physics2d/registrar.h"
#include "rawframe/world_runtime/simulation.h"

#include <memory>

namespace rawframe::physics2d {

namespace {

using diagnostics::EventIdentity;

constexpr EventIdentity kSummary{"physics2d", "summary"};

constexpr std::string_view kNeeds[] = {world_runtime::kSimulation.name};
constexpr std::string_view kMaybe[] = {kPhysics2DPlan.name};

class WorldParticipant final : public composition::Participant {
public:
    result::Status load(composition::ParticipantContext& context) {
        RAWFRAME_TRY_ASSIGN(world_runtime::Simulation * simulation, context.capability(world_runtime::kSimulation));
        RAWFRAME_TRY_ASSIGN(const Physics2DPlan* plan, context.capability(kPhysics2DPlan));
        if (!plan->physics2d().has_value()) {
            return {};
        }
        RAWFRAME_TRY_ASSIGN(physics_, Physics2D::create(*plan->physics2d()));
        return simulation->addSystems(*physics_);
    }

    result::Status start(composition::ParticipantContext& context) noexcept override {
        emitter_ = context.emitter();
        return {};
    }

    void stop() noexcept override {
        if (physics_ == nullptr) {
            return;
        }
        const Physics2DStatistics kStatistics = physics_->statistics();
        emitter_.log(diagnostics::Severity::Info,
                     kSummary,
                     "2D physics totals",
                     {diagnostics::field("steps", kStatistics.steps),
                      diagnostics::field("bodiesMade", kStatistics.bodiesMade),
                      diagnostics::field("bodiesRemoved", kStatistics.bodiesRemoved),
                      diagnostics::field("bodiesRefused", kStatistics.bodiesRefused),
                      diagnostics::field("teleports", kStatistics.teleports),
                      diagnostics::field("velocitiesSet", kStatistics.velocitiesSet),
                      diagnostics::field("impulses", kStatistics.impulses),
                      diagnostics::field("digest", physics_->digest())});
    }

private:
    std::unique_ptr<Physics2D> physics_;
    diagnostics::Emitter emitter_;
};

result::Result<composition::ParticipantOwner> makeWorld(composition::ParticipantContext& context) noexcept {
    auto participant = std::make_unique<WorldParticipant>();
    if (context.has(kPhysics2DPlan.name)) {
        RAWFRAME_TRY(participant->load(context));
    }
    return composition::ParticipantOwner{participant.release()};
}

} // namespace

void registerParticipants(composition::ParticipantRegistrar& registrar) noexcept {
    registrar.submit(composition::ParticipantDeclaration{
        .identity = "rawframe.physics2d.world",
        .factory = &makeWorld,
        .scope = composition::LifetimeScope::World,
        .requiredCapabilities = kNeeds,
        .optionalCapabilities = kMaybe,
        .lifecycle = {.stopBudget = execution::MonotonicDuration::fromMilliseconds(100)},
        .observabilityIdentity = "physics2d.world",
        .budgetOwner = "world",
    });
}

} // namespace rawframe::physics2d
