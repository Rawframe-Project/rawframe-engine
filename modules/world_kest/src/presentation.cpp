#include "presentation.h"

#include "animation_doors.h"
#include "effect_doors.h"
#include "message_doors.h"
#include "mod_services.h"
#include "physics_doors.h"
#include "player_doors.h"
#include "rawframe/composition/composition.h"
#include "rawframe/composition/configuration.h"
#include "rawframe/world/column_query.h"
#include "rawframe/world/schedule.h"
#include "rawframe/world_kest/errors.h"
#include "rawframe/world_kest/kest_systems.h"
#include "rawframe/world_replication/client_worlds.h"

#include <algorithm>
#include <array>
#include <string>
#include <vector>

namespace rawframe::world_kest {

namespace {

constexpr diagnostics::EventIdentity kPresentFailed{"world_kest", "present_failed"};

const GameComponent* componentNamed(const GameDescription& game, std::string_view name) {
    const auto kFound = std::ranges::find(game.components, name, &GameComponent::name);
    return kFound == game.components.end() ? nullptr : &*kFound;
}

} // namespace

struct ClientPresentation::State {
    PresentationSettings settings;
    /// Each present system's columns, and the one before it, beside
    /// `settings.game->presented`.
    std::vector<std::vector<KestColumn>> columns;
    std::vector<std::vector<std::string_view>> after;
    std::vector<KestComponent> components;
    /// What the present systems' doors answer from: the mirror's animation
    /// and the player presented; no physics steps on a mirror, so its doors
    /// refuse, and no mod runs.
    AnimationDoorContext animationDoors;
    PlayerDoorContext playerDoor;
    PhysicsDoorContext physicsDoors;
    std::unique_ptr<ModServices> services;
    std::unique_ptr<EffectDoors> effects;
    std::unique_ptr<MessageDoors> messages;

    /// What is bound to the mirror, made again for another one.
    const world::World* bound = nullptr;
    std::unique_ptr<KestSystems> systems;
    std::optional<world::Schedule> schedule;
    std::unique_ptr<world_animation::WorldAnimation> animation;
    /// Each presentation line's components and what has its `on`, in the
    /// mirror's terms; no query for `on player`.
    struct Attached {
        std::vector<schema::ComponentRuntimeId> components;
        std::optional<world::ColumnQuery> query;
    };
    std::vector<Attached> attached;
    world::TickIndex tick;
    std::vector<world::EntityHandle> missing;
    std::vector<std::byte> zero;
    PresentationStatistics statistics;

    /// The present systems and the animation made for `mirror`'s registry.
    result::Status bind(world::World& mirror) {
        const schema::SchemaRegistry& registry = mirror.registry();
        const GameDescription& game = *settings.game;
        systems.reset();
        schedule.reset();
        animation.reset();
        attached.clear();
        animationDoors.queries = nullptr;
        for (const GamePresentation& presentation : game.presentation) {
            Attached made;
            for (const std::string& name : presentation.components) {
                RAWFRAME_TRY_ASSIGN(const schema::ComponentRuntimeId kComponent,
                                    registry.find(componentNamed(game, name)->id));
                made.components.push_back(kComponent);
            }
            if (presentation.on.has_value()) {
                RAWFRAME_TRY_ASSIGN(const schema::ComponentRuntimeId kOn,
                                    registry.find(componentNamed(game, *presentation.on)->id));
                const std::array<world::ColumnTerm, 1> kTerms = {world::ColumnTerm{kOn, world::Access::Read}};
                RAWFRAME_TRY_ASSIGN(made.query, world::ColumnQuery::resolve(kTerms, registry));
            }
            attached.push_back(std::move(made));
        }
        std::vector<world::SystemDeclaration> scheduled;
        if (!game.presented.empty()) {
            std::vector<KestSystemDeclaration> declarations;
            for (std::size_t index = 0; index < game.presented.size(); ++index) {
                const GameSystem& system = game.presented[index];
                // In line order: each after the one before it.
                declarations.push_back(KestSystemDeclaration{.identity = system.identity,
                                                             .phase = world::Phase::Simulation,
                                                             .entry = system.entry,
                                                             .columns = columns[index],
                                                             .after = after[index],
                                                             .before = {},
                                                             .randomStreams = {}});
            }
            kest::DoorTable doors;
            RAWFRAME_TRY(kest::addStandardMath(doors));
            RAWFRAME_TRY(addAnimationDoors(doors, &animationDoors));
            RAWFRAME_TRY(addPlayerDoor(doors, &playerDoor));
            RAWFRAME_TRY(services->addDoors(doors));
            RAWFRAME_TRY(effects->addDoors(doors));
            RAWFRAME_TRY(messages->addDoors(doors));
            if (game.physics.has_value()) {
                RAWFRAME_TRY(addPhysicsDoors(doors, game.physics->dimensions, &physicsDoors));
            }
            RAWFRAME_TRY_ASSIGN(systems,
                                KestSystems::create(KestSystemsSettings{.program = settings.program,
                                                                        .doors = std::move(doors),
                                                                        .components = components,
                                                                        .limits = settings.limits,
                                                                        .systems = declarations}));
            RAWFRAME_TRY(systems->declareSystems(registry, scheduled));
        }
        RAWFRAME_TRY_ASSIGN(world::Schedule made, world::Schedule::compile(scheduled, registry));
        schedule.emplace(std::move(made));
        if (settings.animation.has_value()) {
            RAWFRAME_TRY_ASSIGN(animation, world_animation::WorldAnimation::create(*settings.animation));
            RAWFRAME_TRY(animation->bind(registry));
            animationDoors.queries = animation.get();
        }
        bound = &mirror;
        tick = {};
        ++statistics.bound;
        return {};
    }

    /// Each presentation component, zeroed, onto every entity that has its
    /// `on` component (or is `player`) and lacks it.
    result::Status attach(world::World& mirror, world::EntityHandle player) {
        for (Attached& each : attached) {
            for (const schema::ComponentRuntimeId kComponent : each.components) {
                missing.clear();
                if (!each.query.has_value()) {
                    if (!player.isNull() && mirror.alive(player) && mirror.getErased(player, kComponent) == nullptr) {
                        missing.push_back(player);
                    }
                } else {
                    each.query->forEachChunk(mirror, [&](const world::ColumnChunk& chunk) {
                        for (const world::EntityHandle kEntity : chunk.entities) {
                            if (mirror.getErased(kEntity, kComponent) == nullptr) {
                                missing.push_back(kEntity);
                            }
                        }
                    });
                }
                zero.assign(mirror.registry().descriptor(kComponent).size, std::byte{});
                for (const world::EntityHandle kEntity : missing) {
                    RAWFRAME_TRY(mirror.insertErased(kEntity, kComponent, zero.data()));
                    ++statistics.attached;
                }
            }
        }
        return {};
    }
};

ClientPresentation::ClientPresentation(std::unique_ptr<State> state) noexcept : state_(std::move(state)) {
}

ClientPresentation::~ClientPresentation() = default;

result::Result<std::unique_ptr<ClientPresentation>> ClientPresentation::create(PresentationSettings settings) {
    const GameDescription& game = *settings.game;
    if (game.presentation.empty() && game.presented.empty() && !settings.animation.has_value()) {
        return std::unique_ptr<ClientPresentation>{};
    }
    auto state = std::make_unique<State>();
    for (const GameSystem& system : game.presented) {
        std::vector<std::string_view>& after = state->after.emplace_back();
        if (state->after.size() > 1) {
            after.emplace_back(game.presented[state->after.size() - 2].identity);
        }
        std::vector<KestColumn>& columns = state->columns.emplace_back();
        for (const GameColumn& column : system.columns) {
            if (column.entities) {
                columns.push_back(KestColumn{.component = {}, .element = {}, .entities = true});
                continue;
            }
            const GameComponent& component = *componentNamed(game, column.component);
            columns.push_back(
                KestColumn{.component = component.id, .element = component.kestType, .access = column.access});
        }
    }
    for (const GameComponent& component : game.components) {
        if (settings.program->layout(component.kestType).has_value()) {
            state->components.push_back(KestComponent{.component = component.id, .kestType = component.kestType});
        }
    }
    std::vector<std::size_t> sizes;
    for (const schema::ComponentDescriptor& descriptor : settings.descriptors) {
        sizes.push_back(descriptor.size);
    }
    // A mod's service answers the value it is given, and effects are the
    // simulation's to emit: a present system emits none it keeps.
    state->services = std::make_unique<ModServices>(game, sizes);
    state->effects = std::make_unique<EffectDoors>(game, false);
    RAWFRAME_TRY_ASSIGN(state->messages, MessageDoors::create(game, *settings.program, MessageDoors::Role::Read));
    state->settings = std::move(settings);
    return std::unique_ptr<ClientPresentation>{new ClientPresentation{std::move(state)}};
}

result::Status ClientPresentation::present(world::World& mirror,
                                           world::EntityHandle player,
                                           std::span<const world_replication::ReceivedMessage> arrived,
                                           world::TickRate rate,
                                           diagnostics::Emitter emitter) {
    State& state = *state_;
    if (state.bound != &mirror) {
        RAWFRAME_TRY(state.bind(mirror));
    }
    ++state.statistics.ticks;
    RAWFRAME_TRY(state.attach(mirror, player));
    state.playerDoor.player = player;
    state.messages->arrived(arrived);
    RAWFRAME_TRY_ASSIGN(const world::TickReport kReport, state.schedule->runTick(mirror, state.tick, rate, emitter));
    for (const world::TickReport::Failure& failure : kReport.failures) {
        ++state.statistics.systemsFailed;
        emitter.log(diagnostics::Severity::Warning,
                    kPresentFailed,
                    "a present system returned an error; its commands were discarded",
                    {diagnostics::field("system", std::string_view{failure.system}),
                     diagnostics::field("tick", kReport.tick.value),
                     diagnostics::field("error", failure.error.description())});
    }
    if (state.animation != nullptr) {
        RAWFRAME_TRY(state.animation->play(mirror, rate));
    }
    return {};
}

PresentationStatistics ClientPresentation::statistics() const noexcept {
    return state_->statistics;
}

world_animation::AnimationStatistics ClientPresentation::animationStatistics() const noexcept {
    return state_->animation != nullptr ? state_->animation->statistics() : world_animation::AnimationStatistics{};
}

namespace {

constexpr diagnostics::EventIdentity kPresentedSummary{"world_kest", "presented_summary"};
constexpr diagnostics::EventIdentity kUnpresented{"world_kest", "presentation_unavailable"};
constexpr std::string_view kPresentedMaybe[] = {kPresentationPlan.name, world_replication::kClientWorlds.name};
constexpr std::uint64_t kMostTicksPerFrame = 4;
constexpr std::uint32_t kServer = composition::only(composition::TargetRole::DedicatedServer);

/// Presents the Worlds of the process's local players (D362), each a frame.
class PresentedParticipant final : public composition::Participant {
public:
    result::Status load(composition::ParticipantContext& context) {
        if (!context.has(kPresentationPlan.name) || !context.has(world_replication::kClientWorlds.name)) {
            return {};
        }
        RAWFRAME_TRY_ASSIGN(const PresentationPlan* plan, context.capability(kPresentationPlan));
        RAWFRAME_TRY_ASSIGN(world_replication::ClientWorlds * clients,
                            context.capability(world_replication::kClientWorlds));
        for (std::size_t player = 0; player < std::max<std::size_t>(clients->localPlayers(), 1); ++player) {
            Presented presented;
            RAWFRAME_TRY_ASSIGN(presented.presentation, plan->presentation());
            if (presented.presentation == nullptr) {
                return {};
            }
            presented_.push_back(std::move(presented));
        }
        RAWFRAME_TRY_ASSIGN(const std::uint64_t kRate, context.configuration().unsignedInteger("world.tick_rate", 60));
        RAWFRAME_TRY_ASSIGN(rate_,
                            world::TickRate::of(static_cast<std::uint32_t>(std::clamp<std::uint64_t>(kRate, 1, 1000))));
        clients_ = clients;
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
        // The frames are counted on the first player's World; the others
        // are presented as many ticks.
        const world_replication::ClientView kFirst = clients_->client(clients_->playerClient().value_or(0));
        if (kFirst.world == nullptr) {
            last_.reset();
            return;
        }
        if (!last_.has_value()) {
            last_ = frame.now;
        }
        owed_.nanoseconds += (frame.now - *last_).nanoseconds;
        last_ = frame.now;
        const std::uint64_t kTicks = rate_.ticksIn(owed_);
        owed_.nanoseconds -=
            static_cast<std::int64_t>(kTicks * std::uint64_t{rate_.seconds} * 1'000'000'000U / rate_.ticks);
        for (std::size_t player = 0; player < presented_.size(); ++player) {
            Presented& each = presented_[player];
            const std::size_t kClient = player == 0 ? clients_->playerClient().value_or(0) : player;
            const world_replication::ClientView kView = clients_->client(kClient);
            if (kView.world == nullptr) {
                continue;
            }
            // What arrived since the last frame is read by its first tick; a
            // frame with no tick leaves it for the next.
            if (kTicks != 0) {
                each.seenMessages = clients_->readMessages(kClient, each.seenMessages, each.arrived);
            }
            for (std::uint64_t tick = 0; tick < std::min(kTicks, kMostTicksPerFrame); ++tick) {
                const std::span<const world_replication::ReceivedMessage> kArrived =
                    tick == 0 ? std::span{each.arrived} : std::span<const world_replication::ReceivedMessage>{};
                const result::Status kPresented =
                    each.presentation->present(*kView.world, kView.owned, kArrived, rate_, emitter_);
                if (!kPresented.has_value()) {
                    emitter_.log(diagnostics::Severity::Warning,
                                 kUnpresented,
                                 "a client's World cannot be presented: it is drawn as it arrives",
                                 {diagnostics::field("reason", std::string{kPresented.error().description()})});
                    clients_ = nullptr;
                    return;
                }
            }
        }
        dropped_ += kTicks - std::min(kTicks, kMostTicksPerFrame);
    }

    void stop() noexcept override {
        if (presented_.empty() || presented_.front().presentation->statistics().ticks == 0) {
            return;
        }
        // The first player's; the others' are presented alike.
        const PresentationStatistics kPresented = presented_.front().presentation->statistics();
        const world_animation::AnimationStatistics kAnimated = presented_.front().presentation->animationStatistics();
        emitter_.log(diagnostics::Severity::Info,
                     kPresentedSummary,
                     "what one client's World was presented",
                     {diagnostics::field("ticks", kPresented.ticks),
                      diagnostics::field("ticksDropped", dropped_),
                      diagnostics::field("attached", kPresented.attached),
                      diagnostics::field("bound", kPresented.bound),
                      diagnostics::field("systemsFailed", kPresented.systemsFailed),
                      diagnostics::field("animationSteps", kAnimated.steps),
                      diagnostics::field("instancesMade", kAnimated.instancesMade),
                      diagnostics::field("animatorsRefused", kAnimated.animatorsRefused),
                      diagnostics::field("players", static_cast<std::uint64_t>(presented_.size()))});
    }

private:
    world_replication::ClientWorlds* clients_ = nullptr;
    /// Each local player's presentation, and the game messages it has read.
    struct Presented {
        std::unique_ptr<ClientPresentation> presentation;
        std::uint64_t seenMessages = 0;
        std::vector<world_replication::ReceivedMessage> arrived;
    };
    std::vector<Presented> presented_;
    world::TickRate rate_;
    std::optional<execution::MonotonicInstant> last_;
    execution::MonotonicDuration owed_;
    std::uint64_t dropped_ = 0;
    diagnostics::Emitter emitter_;
};

result::Result<composition::ParticipantOwner> makePresented(composition::ParticipantContext& context) noexcept {
    auto participant = std::make_unique<PresentedParticipant>();
    RAWFRAME_TRY(participant->load(context));
    return composition::ParticipantOwner{participant.release()};
}

} // namespace

void registerPresented(composition::ParticipantRegistrar& registrar) noexcept {
    registrar.submit(composition::ParticipantDeclaration{
        .identity = "rawframe.world_kest.presented",
        .factory = &makePresented,
        .scope = composition::LifetimeScope::World,
        .optionalCapabilities = kPresentedMaybe,
        .eligibility = {.roles = ~kServer},
        // Stopping only reports: nothing it holds is waited for.
        .lifecycle = {.stopBudget = execution::MonotonicDuration::fromMilliseconds(10)},
        .observabilityIdentity = "world_kest.presented",
        .budgetOwner = "world",
        .hostPhases = composition::hostPhaseBit(composition::HostPhase::RunWorlds),
    });
}

} // namespace rawframe::world_kest
