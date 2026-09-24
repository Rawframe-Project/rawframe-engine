// The World as a participant: another participant adds components and a
// system, and Host iterations run exactly the ticks the pacer owes.

#include "rawframe/composition/composition.h"
#include "rawframe/test/test.h"
#include "rawframe/world/query.h"
#include "rawframe/world_runtime/errors.h"
#include "rawframe/world_runtime/registrar.h"
#include "rawframe/world_runtime/simulation.h"

#include <array>
#include <cstdint>
#include <string_view>
#include <vector>

using namespace rawframe;
using composition::HostFrame;
using composition::HostPhase;
using execution::MonotonicDuration;
using execution::MonotonicInstant;

namespace {

struct Position {
    static constexpr schema::ComponentTypeId kComponentTypeId =
        schema::ComponentTypeId::fromText("1982ffcb-4b4e-4c45-b890-ad86ff986d47");
    static constexpr std::string_view kComponentName = "test.position";
    std::int64_t x = 0;
};

struct Velocity {
    static constexpr schema::ComponentTypeId kComponentTypeId =
        schema::ComponentTypeId::fromText("800cefb0-ad1d-4327-8a00-bc5522cdc39a");
    static constexpr std::string_view kComponentName = "test.velocity";
    std::int64_t dx = 0;
};

/// A gameplay participant: registers its components and one system with the
/// World, and spawns movers when it starts.
class Movement final : public composition::Participant, public world_runtime::SystemContributor, public world::System {
public:
    explicit Movement(world_runtime::Simulation& simulation) noexcept : simulation_(&simulation) {
    }

    result::Status declareSystems(const schema::SchemaRegistry& registry,
                                  std::vector<world::SystemDeclaration>& systems) noexcept override {
        RAWFRAME_TRY_ASSIGN(query_, (world::Query<world::Write<Position>, world::Read<Velocity>>::resolve(registry)));
        reads_ = query_->reads();
        writes_ = query_->writes();
        systems.push_back(world::SystemDeclaration{
            .identity = "movement.integrate", .reads = reads_, .writes = writes_, .system = this});
        return {};
    }

    result::Status start(composition::ParticipantContext&) noexcept override {
        world::World& world = *simulation_->world();
        RAWFRAME_TRY_ASSIGN(const auto kPosition, world.registry().key<Position>());
        RAWFRAME_TRY_ASSIGN(const auto kVelocity, world.registry().key<Velocity>());
        for (std::int64_t speed = 1; speed <= 3; ++speed) {
            RAWFRAME_TRY_ASSIGN(const world::EntityHandle kEntity, world.create());
            RAWFRAME_TRY(world.insert(kEntity, kPosition, Position{0}));
            RAWFRAME_TRY(world.insert(kEntity, kVelocity, Velocity{speed}));
        }
        return {};
    }

    result::Status run(world::SystemContext& context) noexcept override {
        query_->forEach(context.world, [](world::EntityHandle, Position& position, const Velocity& velocity) {
            position.x += velocity.dx;
        });
        return {};
    }

    std::vector<std::int64_t> positions() {
        std::vector<std::int64_t> values;
        auto all = world::Query<world::Read<Position>>::resolve(simulation_->world()->registry());
        all->forEach(*simulation_->world(), [&values](world::EntityHandle, const Position& position) {
            values.push_back(position.x);
        });
        return values;
    }

private:
    world_runtime::Simulation* simulation_;
    std::optional<world::Query<world::Write<Position>, world::Read<Velocity>>> query_;
    std::vector<schema::ComponentRuntimeId> reads_;
    std::vector<schema::ComponentRuntimeId> writes_;
};

Movement* movement = nullptr;

result::Result<composition::ParticipantOwner> makeMovement(composition::ParticipantContext& context) noexcept {
    RAWFRAME_TRY_ASSIGN(world_runtime::Simulation* const kSimulation, context.capability(world_runtime::kSimulation));
    RAWFRAME_TRY(kSimulation->addComponent<Position>());
    RAWFRAME_TRY(kSimulation->addComponent<Velocity>());
    auto* created = new Movement{*kSimulation};
    RAWFRAME_TRY(kSimulation->addSystems(*created));
    movement = created;
    return composition::ParticipantOwner{created};
}

constexpr std::string_view kNeeds[] = {world_runtime::kSimulation.name};

void registerMovement(composition::ParticipantRegistrar& registrar) noexcept {
    registrar.submit(composition::ParticipantDeclaration{
        .identity = "test.movement",
        .factory = &makeMovement,
        .scope = composition::LifetimeScope::World,
        .requiredCapabilities = kNeeds,
        .lifecycle = {.stopBudget = MonotonicDuration::fromMilliseconds(10)},
    });
}

const std::array<composition::RegistrarEntry, 2> kRegistrars = {
    composition::RegistrarEntry{"world_runtime", &world_runtime::registerParticipants, world_runtime::kScopes},
    composition::RegistrarEntry{"movement_test", &registerMovement, world_runtime::kScopes},
};

result::Result<composition::Plan> plan() {
    std::vector<composition::Problem> problems;
    return composition::compose(
        composition::CompositionRequest{.registrars = kRegistrars, .shutdownBudget = MonotonicDuration::fromSeconds(1)},
        problems);
}

void iterate(composition::Composition& composition, std::uint64_t iteration, MonotonicInstant now) {
    for (std::size_t phase = 0; phase < composition::kHostPhaseCount; ++phase) {
        composition.runHostPhase(static_cast<HostPhase>(phase), HostFrame{.iteration = iteration, .now = now});
    }
}

} // namespace

RAWFRAME_TEST(HostIterationsRunTheTicksThePacerOwes) {
    const auto kPlan = plan();
    RAWFRAME_EXPECT(kPlan.has_value());
    const auto kConfiguration =
        composition::Configuration::parse("world.tick_rate = 10\nworld.maximum_ticks_per_iteration = 3\n");
    execution::ManualClock clock;
    execution::CancellationScope root{clock};
    composition::Composition composition{
        *kPlan, composition::HostServices{.clock = &clock, .scope = &root, .configuration = &*kConfiguration}};
    RAWFRAME_EXPECT(composition.start().has_value());
    RAWFRAME_EXPECT(movement != nullptr);
    if (movement == nullptr) {
        return;
    }

    iterate(composition, 0, clock.now());
    RAWFRAME_EXPECT((movement->positions() == std::vector<std::int64_t>{0, 0, 0}));
    // Half a second owes 5 ticks at 10 Hz; 3 run now and 2 wait as debt.
    clock.advance(MonotonicDuration::fromMilliseconds(500));
    iterate(composition, 1, clock.now());
    RAWFRAME_EXPECT((movement->positions() == std::vector<std::int64_t>{3, 6, 9}));
    iterate(composition, 2, clock.now());
    RAWFRAME_EXPECT((movement->positions() == std::vector<std::int64_t>{5, 10, 15}));
    iterate(composition, 3, clock.now());
    RAWFRAME_EXPECT((movement->positions() == std::vector<std::int64_t>{5, 10, 15}));
    composition.stop();
    movement = nullptr;
}

RAWFRAME_TEST(BadWorldSettingsFailTheStart) {
    const auto kPlan = plan();
    execution::ManualClock clock;
    execution::CancellationScope root{clock};
    for (const std::string_view kText :
         {"world.tick_rate = 0", "world.maximum_ticks_per_iteration = 0", "world.root_seed = minus one"}) {
        const auto kConfiguration = composition::Configuration::parse(kText);
        composition::Composition composition{
            *kPlan, composition::HostServices{.clock = &clock, .scope = &root, .configuration = &*kConfiguration}};
        RAWFRAME_EXPECT(!composition.start().has_value());
    }
    movement = nullptr;
}
