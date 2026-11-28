#pragma once

// A game's timers: entities of the game's components of
// `rawframe.timer.Lifespan`'s type, destroyed when their ticks run out, and
// of `rawframe.timer.Timer`'s, counting down to the tick they fire. One
// system counts them all before the simulation phase, so a system of the
// simulation phase sees this tick's firing, and a World that does not tick
// (paused, or held at a breakpoint) counts nothing. What they count is
// component data, so a checkpoint keeps it and a restore goes on from it.

#include "rawframe/schema/stable_id.h"
#include "rawframe/world/column_query.h"
#include "rawframe/world_runtime/simulation.h"

#include <cstdint>
#include <memory>
#include <string_view>
#include <vector>

namespace rawframe::world_kest {

/// The system that counts every timer.
inline constexpr std::string_view kTimerCountSystem = "rawframe.timer.count";

/// `rawframe.timer.Lifespan` as C++ reads it.
struct TimerLifespan {
    std::uint32_t ticks = 0;
};

/// `rawframe.timer.Timer` as C++ reads it.
struct TimerCountdown {
    std::uint32_t left = 0;
    std::uint32_t period = 0;
    bool fired = false;
};

/// A game's components of the timer types.
struct GameTimerSettings {
    std::vector<schema::ComponentTypeId> lifespans;
    std::vector<schema::ComponentTypeId> timers;
};

class GameTimers final : public world_runtime::SystemContributor {
public:
    explicit GameTimers(GameTimerSettings settings) noexcept;
    GameTimers(const GameTimers&) = delete;
    GameTimers& operator=(const GameTimers&) = delete;
    ~GameTimers() override;

    [[nodiscard]] result::Status declareSystems(const schema::SchemaRegistry& registry,
                                                std::vector<world::SystemDeclaration>& systems) noexcept override;

    /// Counts every timer down by one tick, recording into `commands` the
    /// destruction of each entity whose lifespan ran out.
    [[nodiscard]] result::Status count(world::World& world, world::CommandBuffer& commands);

private:
    GameTimerSettings settings_;
    std::vector<world::ColumnQuery> lifespanQueries_;
    std::vector<world::ColumnQuery> timerQueries_;
    std::vector<schema::ComponentRuntimeId> writes_;
    std::unique_ptr<world::System> system_;
};

} // namespace rawframe::world_kest
