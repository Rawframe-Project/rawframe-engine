#include "rawframe/composition/composition.h"
#include "rawframe/composition/configuration.h"
#include "rawframe/input_kest/errors.h"
#include "rawframe/input_kest/registrar.h"
#include "rawframe/input_kest/sources.h"
#include "rawframe/world_kest/game_files.h"
#include "rawframe/world_replication/client_worlds.h"
#include "rawframe/world_replication/plan.h"

#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace rawframe::input_kest {

namespace {

constexpr std::string_view kProvides[] = {world_replication::kInputSourcePlan.name, kPlayerHaptics.name};
constexpr std::string_view kNeeds[] = {world_replication::kReplicationPlan.name, world_kest::kGameFiles.name};
constexpr std::string_view kMayUse[] = {kFeed.name};

/// Sources for a game without controls: every one refuses, bots steer at
/// random, and nothing is felt.
class NoSources final : public InputSources {
public:
    [[nodiscard]] bool feelsEffects() const noexcept override {
        return false;
    }
    std::optional<std::size_t> feelEffect(std::uint32_t) override {
        return std::nullopt;
    }

    result::Result<std::unique_ptr<world_replication::InputSource>> botSource(std::uint64_t) override {
        return refuse();
    }
    result::Result<std::unique_ptr<world_replication::InputSource>> playerSource() override {
        return refuse();
    }

private:
    static std::unexpected<result::Error> refuse() {
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
        if (context.has(kFeed.name)) {
            RAWFRAME_TRY_ASSIGN(settings.feed, context.capability(kFeed));
        }
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
        if (capability == kPlayerHaptics.name) {
            return composition::provideAs<PlayerHaptics>(*sources_);
        }
        return {};
    }

private:
    std::unique_ptr<InputSources> sources_;
};

constexpr std::string_view kFeltNeeds[] = {kPlayerHaptics.name};
constexpr std::string_view kFeltMayUse[] = {world_replication::kClientWorlds.name};

/// The process's own player feels its effects (D251): each presentation
/// frame, the effects its client delivered since the last are felt as the
/// game says. An effect taken back after it was felt runs its course, as
/// its sound does.
class Felt final : public composition::Participant {
public:
    result::Status load(composition::ParticipantContext& context) {
        RAWFRAME_TRY_ASSIGN(haptics_, context.capability(kPlayerHaptics));
        if (haptics_->feelsEffects() && context.has(world_replication::kClientWorlds.name)) {
            RAWFRAME_TRY_ASSIGN(clients_, context.capability(world_replication::kClientWorlds));
        }
        return {};
    }

    void runHostPhase(composition::HostPhase phase, const composition::HostFrame& /*frame*/) noexcept override {
        if (phase != composition::HostPhase::PresentationExtract || clients_ == nullptr) {
            return;
        }
        const auto kPlayer = clients_->playerClient();
        if (!kPlayer.has_value()) {
            return;
        }
        seen_ = clients_->readEffects(*kPlayer, seen_, effects_);
        for (const world_replication::EffectEvent& event : effects_) {
            if (!event.cancelled) {
                if (const auto kAsked = haptics_->feelEffect(event.effect.kind); kAsked.has_value()) {
                    ++effectsFelt_;
                    devicesAsked_ += *kAsked;
                }
            }
        }
    }

    result::Status start(composition::ParticipantContext& context) noexcept override {
        emitter_ = context.emitter();
        return {};
    }

    void stop() noexcept override {
        if (clients_ == nullptr) {
            return;
        }
        emitter_.log(
            diagnostics::Severity::Info,
            kFeltSummary,
            "what the player felt",
            {diagnostics::field("effectsFelt", effectsFelt_), diagnostics::field("devicesAsked", devicesAsked_)});
    }

private:
    static constexpr diagnostics::EventIdentity kFeltSummary{"input", "felt_summary"};

    PlayerHaptics* haptics_ = nullptr;
    diagnostics::Emitter emitter_;
    std::uint64_t effectsFelt_ = 0;
    std::uint64_t devicesAsked_ = 0;
    world_replication::ClientWorlds* clients_ = nullptr;
    std::vector<world_replication::EffectEvent> effects_;
    std::uint64_t seen_ = 0;
};

template <typename T>
result::Result<composition::ParticipantOwner> make(composition::ParticipantContext& context) noexcept {
    auto participant = std::make_unique<T>();
    RAWFRAME_TRY(participant->load(context));
    return composition::ParticipantOwner{participant.release()};
}

} // namespace

void registerParticipants(composition::ParticipantRegistrar& registrar) noexcept {
    registrar.submit(composition::ParticipantDeclaration{
        .identity = "rawframe.input_kest.sources",
        .factory = &make<SourcesParticipant>,
        .scope = composition::LifetimeScope::World,
        .providedCapabilities = kProvides,
        .requiredCapabilities = kNeeds,
        .optionalCapabilities = kMayUse,
        .lifecycle = {.stopBudget = execution::MonotonicDuration::fromMilliseconds(100)},
        .observabilityIdentity = "input_kest.sources",
        .budgetOwner = "input",
    });
    registrar.submit(composition::ParticipantDeclaration{
        .identity = "rawframe.input_kest.felt",
        .factory = &make<Felt>,
        .scope = composition::LifetimeScope::World,
        .requiredCapabilities = kFeltNeeds,
        .optionalCapabilities = kFeltMayUse,
        .lifecycle = {.stopBudget = execution::MonotonicDuration::fromMilliseconds(100)},
        .observabilityIdentity = "input_kest.felt",
        .budgetOwner = "input",
        .hostPhases = composition::hostPhaseBit(composition::HostPhase::PresentationExtract),
    });
}

} // namespace rawframe::input_kest
