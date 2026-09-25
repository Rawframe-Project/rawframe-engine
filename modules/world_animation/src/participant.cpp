#include "rawframe/composition/composition.h"
#include "rawframe/world_animation/animation.h"
#include "rawframe/world_animation/registrar.h"
#include "rawframe/world_runtime/simulation.h"

#include <array>
#include <cstdint>
#include <memory>
#include <string_view>
#include <utility>

namespace rawframe::world_animation {

namespace {

using diagnostics::EventIdentity;

constexpr EventIdentity kSummary{"animation", "summary"};

constexpr std::string_view kNeeds[] = {world_runtime::kSimulation.name};
constexpr std::string_view kMaybe[] = {kAnimationPlan.name};

class WorldParticipant final : public composition::Participant {
public:
    result::Status load(composition::ParticipantContext& context, bool simulationOnly) {
        RAWFRAME_TRY_ASSIGN(world_runtime::Simulation * simulation, context.capability(world_runtime::kSimulation));
        RAWFRAME_TRY_ASSIGN(plan_, context.capability(kAnimationPlan));
        if (!plan_->animation().has_value()) {
            plan_ = nullptr;
            return {};
        }
        AnimationSettings settings = *plan_->animation();
        settings.simulationOnly = settings.simulationOnly || simulationOnly;
        RAWFRAME_TRY_ASSIGN(animation_, WorldAnimation::create(std::move(settings)));
        RAWFRAME_TRY(simulation->addSystems(*animation_));
        plan_->attach(animation_.get());
        return {};
    }

    WorldParticipant() noexcept = default;
    WorldParticipant(const WorldParticipant&) = delete;
    WorldParticipant& operator=(const WorldParticipant&) = delete;
    ~WorldParticipant() override {
        if (plan_ != nullptr) {
            plan_->attach(nullptr);
        }
    }

    result::Status start(composition::ParticipantContext& context) noexcept override {
        emitter_ = context.emitter();
        return {};
    }

    void stop() noexcept override {
        if (animation_ == nullptr) {
            return;
        }
        const AnimationStatistics kStatistics = animation_->statistics();
        // As sixteen hex digits: every bit, never a negative number.
        std::array<char, 16> digest{};
        const std::uint64_t kDigest = animation_->digest();
        for (std::size_t index = 0; index < digest.size(); ++index) {
            digest[index] = "0123456789abcdef"[(kDigest >> (4U * (15U - index))) & 0xFU];
        }
        emitter_.log(diagnostics::Severity::Info,
                     kSummary,
                     "animation totals",
                     {diagnostics::field("steps", kStatistics.steps),
                      diagnostics::field("instancesMade", kStatistics.instancesMade),
                      diagnostics::field("instancesRemoved", kStatistics.instancesRemoved),
                      diagnostics::field("animatorsRefused", kStatistics.animatorsRefused),
                      diagnostics::field("parametersRefused", kStatistics.parametersRefused),
                      diagnostics::field("eventsFired", kStatistics.eventsFired),
                      diagnostics::field("eventsOverflowed", kStatistics.eventsOverflowed),
                      diagnostics::field("digest", std::string_view{digest.data(), digest.size()})});
    }

private:
    AnimationPlan* plan_ = nullptr;
    std::unique_ptr<WorldAnimation> animation_;
    diagnostics::Emitter emitter_;
};

result::Result<composition::ParticipantOwner> make(composition::ParticipantContext& context,
                                                   bool simulationOnly) noexcept {
    auto participant = std::make_unique<WorldParticipant>();
    if (context.has(kAnimationPlan.name)) {
        RAWFRAME_TRY(participant->load(context, simulationOnly));
    }
    return composition::ParticipantOwner{participant.release()};
}

result::Result<composition::ParticipantOwner> makeWorld(composition::ParticipantContext& context) noexcept {
    return make(context, false);
}

result::Result<composition::ParticipantOwner> makeServerWorld(composition::ParticipantContext& context) noexcept {
    return make(context, true);
}

constexpr std::uint32_t kServer = composition::only(composition::TargetRole::DedicatedServer);

} // namespace

void registerParticipants(composition::ParticipantRegistrar& registrar) noexcept {
    // One or the other, by the role the composition is for: a dedicated
    // server plays only the simulation's animators.
    registrar.submit(composition::ParticipantDeclaration{
        .identity = "rawframe.animation.world",
        .factory = &makeWorld,
        .scope = composition::LifetimeScope::World,
        .requiredCapabilities = kNeeds,
        .optionalCapabilities = kMaybe,
        .eligibility = {.roles = ~kServer},
        .lifecycle = {.stopBudget = execution::MonotonicDuration::fromMilliseconds(100)},
        .observabilityIdentity = "animation.world",
        .budgetOwner = "world",
    });
    registrar.submit(composition::ParticipantDeclaration{
        .identity = "rawframe.animation.server_world",
        .factory = &makeServerWorld,
        .scope = composition::LifetimeScope::World,
        .requiredCapabilities = kNeeds,
        .optionalCapabilities = kMaybe,
        .eligibility = {.roles = kServer},
        .lifecycle = {.stopBudget = execution::MonotonicDuration::fromMilliseconds(100)},
        .observabilityIdentity = "animation.world",
        .budgetOwner = "world",
    });
}

} // namespace rawframe::world_animation
