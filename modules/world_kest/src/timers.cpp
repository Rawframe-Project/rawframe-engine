#include "timers.h"

#include <array>
#include <cstring>
#include <utility>

namespace rawframe::world_kest {

namespace {

/// The counting as a system of the World.
class CountSystem final : public world::System {
public:
    explicit CountSystem(GameTimers& owner) noexcept : owner_(&owner) {
    }
    result::Status run(world::SystemContext& context) noexcept override {
        return owner_->count(context.world, context.commands);
    }

private:
    GameTimers* owner_;
};

} // namespace

GameTimers::GameTimers(GameTimerSettings settings) noexcept : settings_(std::move(settings)) {
}

GameTimers::~GameTimers() = default;

result::Status GameTimers::declareSystems(const schema::SchemaRegistry& registry,
                                          std::vector<world::SystemDeclaration>& systems) noexcept {
    lifespanQueries_.clear();
    timerQueries_.clear();
    writes_.clear();
    const auto kQueries = [&](std::span<const schema::ComponentTypeId> ids,
                              std::vector<world::ColumnQuery>& into) -> result::Status {
        for (const schema::ComponentTypeId kId : ids) {
            RAWFRAME_TRY_ASSIGN(const schema::ComponentRuntimeId kComponent, registry.find(kId));
            const std::array<world::ColumnTerm, 1> kTerms = {world::ColumnTerm{kComponent, world::Access::Write}};
            RAWFRAME_TRY_ASSIGN(world::ColumnQuery query, world::ColumnQuery::resolve(kTerms, registry));
            into.push_back(std::move(query));
            writes_.push_back(kComponent);
        }
        return {};
    };
    RAWFRAME_TRY(kQueries(settings_.lifespans, lifespanQueries_));
    RAWFRAME_TRY(kQueries(settings_.timers, timerQueries_));
    system_ = std::make_unique<CountSystem>(*this);
    systems.push_back(world::SystemDeclaration{.identity = kTimerCountSystem,
                                               .phase = world::Phase::PreSimulation,
                                               .writes = writes_,
                                               .system = system_.get()});
    return {};
}

result::Status GameTimers::count(world::World& world, world::CommandBuffer& commands) {
    // A lifespan of N lasts N ticks: its entity is destroyed at the end of
    // the tick that finds it at one, or at nought.
    result::Status recorded;
    for (world::ColumnQuery& query : lifespanQueries_) {
        query.forEachChunk(world, [&](const world::ColumnChunk& chunk) {
            for (std::size_t row = 0; row < chunk.entities.size(); ++row) {
                std::byte* const kData = chunk.columns[0] + (row * sizeof(TimerLifespan));
                TimerLifespan lifespan;
                std::memcpy(&lifespan, kData, sizeof(TimerLifespan));
                if (lifespan.ticks <= 1) {
                    lifespan.ticks = 0;
                    if (settings_.destroys && recorded.has_value()) {
                        recorded = commands.destroy(chunk.entities[row]);
                    }
                } else {
                    --lifespan.ticks;
                }
                std::memcpy(kData, &lifespan, sizeof(TimerLifespan));
            }
        });
    }
    RAWFRAME_TRY(recorded);
    // A timer fires on the tick its count reaches nought, and starts again
    // from its period, or stops at nought without one.
    for (world::ColumnQuery& query : timerQueries_) {
        query.forEachChunk(world, [&](const world::ColumnChunk& chunk) {
            for (std::size_t row = 0; row < chunk.entities.size(); ++row) {
                std::byte* const kData = chunk.columns[0] + (row * sizeof(TimerCountdown));
                TimerCountdown timer;
                std::memcpy(&timer, kData, sizeof(TimerCountdown));
                timer.fired = false;
                if (timer.left > 0 && --timer.left == 0) {
                    timer.fired = true;
                    timer.left = timer.period;
                }
                std::memcpy(kData, &timer, sizeof(TimerCountdown));
            }
        });
    }
    return {};
}

} // namespace rawframe::world_kest
