#include "rawframe/composition/composition.h"
#include "rawframe/composition/configuration.h"
#include "rawframe/world_animation/animation.h"
#include "rawframe/world_animation/registrar.h"
#include "rawframe/world_replication/client_worlds.h"
#include "rawframe/world_runtime/simulation.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

namespace rawframe::world_animation {

namespace {

using diagnostics::EventIdentity;

constexpr EventIdentity kSummary{"animation", "summary"};
constexpr EventIdentity kPresentedSummary{"animation", "presented_summary"};
constexpr EventIdentity kUnbound{"animation", "presentation_unavailable"};

constexpr std::string_view kNeeds[] = {world_runtime::kSimulation.name};
constexpr std::string_view kMaybe[] = {kAnimationPlan.name};

class WorldParticipant final : public composition::Participant {
public:
    result::Status load(composition::ParticipantContext& context, bool simulationOnly) {
        RAWFRAME_TRY_ASSIGN(world_runtime::Simulation * simulation, context.capability(world_runtime::kSimulation));
        RAWFRAME_TRY_ASSIGN(plan_, context.capability(kAnimationPlan));
        if (!plan_->animation().has_value() || !plan_->simulated()) {
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
                      diagnostics::field("requests", kStatistics.requests),
                      diagnostics::field("requestsDropped", kStatistics.requestsDropped),
                      diagnostics::field("rootMotions", kStatistics.rootMotions),
                      diagnostics::field("stagesRun", kStatistics.stagesRun),
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

constexpr std::string_view kPresentedMaybe[] = {kAnimationPlan.name, world_replication::kClientWorlds.name};
/// The most steps one frame catches up: a frame late by more drops the
/// rest, as a presentation may.
constexpr std::uint64_t kMostStepsPerFrame = 4;

/// Plays every animator of one client's mirrored World where it is drawn
/// (D258): the server played only its own World, and a client's mirror is
/// stepped by no schedule. Once per Host frame, in `run_worlds`, after the
/// mirror took the server's state, it steps as many of the game's ticks as
/// the frame's time holds. Idle without animators or without clients.
class PresentedParticipant final : public composition::Participant {
public:
    result::Status load(composition::ParticipantContext& context) {
        if (!context.has(kAnimationPlan.name) || !context.has(world_replication::kClientWorlds.name)) {
            return {};
        }
        RAWFRAME_TRY_ASSIGN(AnimationPlan * plan, context.capability(kAnimationPlan));
        if (!plan->animation().has_value()) {
            return {};
        }
        settings_ = *plan->animation();
        settings_.simulationOnly = false;
        RAWFRAME_TRY_ASSIGN(const std::uint64_t kRate, context.configuration().unsignedInteger("world.tick_rate", 60));
        RAWFRAME_TRY_ASSIGN(rate_,
                            world::TickRate::of(static_cast<std::uint32_t>(std::min<std::uint64_t>(kRate, 1000))));
        RAWFRAME_TRY_ASSIGN(clients_, context.capability(world_replication::kClientWorlds));
        return {};
    }

    result::Status start(composition::ParticipantContext& context) noexcept override {
        emitter_ = context.emitter();
        return {};
    }

    void runHostPhase(composition::HostPhase phase, const composition::HostFrame& frame) noexcept override {
        if (phase != composition::HostPhase::RunWorlds || clients_ == nullptr) {
            return;
        }
        const world_replication::ClientView kView = clients_->client(clients_->playerClient().value_or(0));
        if (kView.world == nullptr) {
            return;
        }
        if (kView.world != bound_) {
            // A new mirror: its registry bound afresh, its time from now.
            auto made = WorldAnimation::create(settings_);
            const result::Status kBound = made.has_value() ? (*made)->bind(kView.world->registry())
                                                           : result::Status{std::unexpected{made.error().clone()}};
            if (!kBound.has_value()) {
                emitter_.log(diagnostics::Severity::Warning,
                             kUnbound,
                             "a client's World cannot be animated: its animators are not played there",
                             {diagnostics::field("reason", std::string{kBound.error().description()})});
                clients_ = nullptr;
                return;
            }
            animation_ = std::move(*made);
            bound_ = kView.world;
            last_ = frame.now;
            owed_ = {};
        }
        owed_.nanoseconds += (frame.now - last_).nanoseconds;
        last_ = frame.now;
        const std::uint64_t kSteps = rate_.ticksIn(owed_);
        const std::uint64_t kTickNanoseconds = std::uint64_t{rate_.seconds} * 1'000'000'000U / rate_.ticks;
        owed_.nanoseconds -= static_cast<std::int64_t>(kSteps * kTickNanoseconds);
        for (std::uint64_t step = 0; step < std::min(kSteps, kMostStepsPerFrame); ++step) {
            if (!animation_->play(*kView.world, rate_).has_value()) {
                ++failedSteps_;
            }
        }
        dropped_ += kSteps - std::min(kSteps, kMostStepsPerFrame);
    }

    void stop() noexcept override {
        if (animation_ == nullptr) {
            return;
        }
        const AnimationStatistics kStatistics = animation_->statistics();
        emitter_.log(diagnostics::Severity::Info,
                     kPresentedSummary,
                     "what one client's World played",
                     {diagnostics::field("steps", kStatistics.steps),
                      diagnostics::field("stepsDropped", dropped_),
                      diagnostics::field("failedSteps", failedSteps_),
                      diagnostics::field("instancesMade", kStatistics.instancesMade),
                      diagnostics::field("animatorsRefused", kStatistics.animatorsRefused),
                      diagnostics::field("eventsFired", kStatistics.eventsFired)});
    }

private:
    world_replication::ClientWorlds* clients_ = nullptr;
    AnimationSettings settings_;
    world::TickRate rate_;
    std::unique_ptr<WorldAnimation> animation_;
    const world::World* bound_ = nullptr;
    execution::MonotonicInstant last_;
    execution::MonotonicDuration owed_;
    std::uint64_t dropped_ = 0;
    std::uint64_t failedSteps_ = 0;
    diagnostics::Emitter emitter_;
};

result::Result<composition::ParticipantOwner> makePresented(composition::ParticipantContext& context) noexcept {
    auto participant = std::make_unique<PresentedParticipant>();
    RAWFRAME_TRY(participant->load(context));
    return composition::ParticipantOwner{participant.release()};
}

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
    // Never on a dedicated server, which draws nothing.
    registrar.submit(composition::ParticipantDeclaration{
        .identity = "rawframe.animation.presented",
        .factory = &makePresented,
        .scope = composition::LifetimeScope::World,
        .optionalCapabilities = kPresentedMaybe,
        .eligibility = {.roles = ~kServer},
        .lifecycle = {.stopBudget = execution::MonotonicDuration::fromMilliseconds(100)},
        .observabilityIdentity = "animation.presented",
        .budgetOwner = "world",
        .hostPhases = composition::hostPhaseBit(composition::HostPhase::RunWorlds),
    });
}

} // namespace rawframe::world_animation
