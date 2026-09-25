#include "rawframe/composition/composition.h"
#include "rawframe/composition/configuration.h"
#include "rawframe/input_kest/errors.h"
#include "rawframe/input_kest/registrar.h"
#include "rawframe/input_kest/sources.h"
#include "rawframe/world_kest/game_files.h"
#include "rawframe/world_replication/plan.h"

#include <string>
#include <utility>

namespace rawframe::input_kest {

namespace {

constexpr std::string_view kProvides[] = {world_replication::kInputSourcePlan.name};
constexpr std::string_view kNeeds[] = {world_replication::kReplicationPlan.name, world_kest::kGameFiles.name};

/// Sources for a game without controls: every one refuses, and bots steer
/// at random.
class NoSources final : public world_replication::InputSourcePlan {
public:
    result::Result<std::unique_ptr<world_replication::InputSource>> botSource(std::uint64_t) override {
        return result::fail(result::ErrorClass::NotFound,
                            kInputKestDomain,
                            code(InputKestError::NoControls),
                            "the game declares no controls");
    }
};

class SourcesParticipant final : public composition::Participant {
public:
    result::Status load(composition::ParticipantContext& context) {
        const composition::Configuration& configuration = context.configuration();
        RAWFRAME_TRY_ASSIGN(const world_kest::GameFiles* game, context.capability(world_kest::kGameFiles));
        RAWFRAME_TRY_ASSIGN(const world_replication::ReplicationPlan* plan,
                            context.capability(world_replication::kReplicationPlan));
        if (!game->named() || !plan->input().has_value()) {
            sources_ = std::make_unique<NoSources>();
            return {};
        }
        SourceSettings settings{.game = game, .inputSize = plan->input()->size};
        RAWFRAME_TRY_ASSIGN(settings.limits.heapBytes,
                            configuration.unsignedInteger("kest.sample_heap_bytes", settings.limits.heapBytes));
        RAWFRAME_TRY_ASSIGN(settings.limits.fuelPerCall,
                            configuration.unsignedInteger("kest.sample_fuel", settings.limits.fuelPerCall));
        auto sources = makeInputSources(settings);
        if (sources.has_value()) {
            sources_ = std::move(*sources);
        } else if (sources.error().code() == code(InputKestError::NoControls)) {
            sources_ = std::make_unique<NoSources>();
        } else {
            return std::unexpected<result::Error>{std::move(sources).error()};
        }
        return {};
    }

    composition::CapabilityObject provide(std::string_view capability) noexcept override {
        if (capability == world_replication::kInputSourcePlan.name) {
            return composition::provideAs<world_replication::InputSourcePlan>(*sources_);
        }
        return {};
    }

private:
    std::unique_ptr<world_replication::InputSourcePlan> sources_;
};

result::Result<composition::ParticipantOwner> makeSources(composition::ParticipantContext& context) noexcept {
    auto participant = std::make_unique<SourcesParticipant>();
    RAWFRAME_TRY(participant->load(context));
    return composition::ParticipantOwner{participant.release()};
}

} // namespace

void registerParticipants(composition::ParticipantRegistrar& registrar) noexcept {
    registrar.submit(composition::ParticipantDeclaration{
        .identity = "rawframe.input_kest.sources",
        .factory = &makeSources,
        .scope = composition::LifetimeScope::World,
        .providedCapabilities = kProvides,
        .requiredCapabilities = kNeeds,
        .lifecycle = {.stopBudget = execution::MonotonicDuration::fromMilliseconds(100)},
        .observabilityIdentity = "input_kest.sources",
        .budgetOwner = "input",
    });
}

} // namespace rawframe::input_kest
