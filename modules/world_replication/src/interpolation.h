#pragma once

#include "rawframe/world/world.h"
#include "rawframe/world_replication/codec.h"
#include "rawframe/world_replication/interpolation.h"

#include <cstddef>
#include <cstdint>
#include <deque>
#include <map>
#include <optional>
#include <span>
#include <utility>
#include <vector>

namespace rawframe::world_replication {

/// A client's interpolation bookkeeping: the server tick estimate, and the
/// states received for each remote entity's interpolated components.
class Interpolation {
public:
    /// `interpolated` is by replication table index.
    Interpolation(const InterpolationSettings& settings,
                  std::span<const ComponentCodec> table,
                  std::vector<bool> interpolated);

    [[nodiscard]] bool interpolates(std::size_t component) const noexcept {
        return interpolated_[component];
    }

    /// Admitted at this tick rate.
    void admitted(std::uint64_t ticks, std::uint64_t seconds) noexcept;
    /// A state datagram for `serverTick` arrived just now.
    void heard(std::uint64_t serverTick);
    /// A received state of one entity's component, in memory layout.
    void sample(std::uint32_t net, std::size_t component, std::uint64_t tick, std::span<const std::byte> value);
    void retire(std::uint32_t net);
    /// The newest state received of one entity's component; empty if none.
    [[nodiscard]] std::span<const std::byte> newest(std::uint32_t net, std::size_t component) const noexcept;
    void reset() noexcept;

    /// The server tick shown now, fractional; none before the first state.
    [[nodiscard]] std::optional<double> perceivedTick() const noexcept;
    /// Writes into `world` what each interpolated value is at the moment
    /// shown. `mirrored` maps IDs to entities and `table` indices to
    /// component IDs.
    void show(world::World& world,
              const std::map<std::uint32_t, world::EntityHandle>& mirrored,
              std::span<const schema::ComponentRuntimeId> table);

    [[nodiscard]] InterpolationStatistics statistics() const noexcept {
        return statistics_;
    }

private:
    struct State {
        std::uint64_t tick = 0;
        std::vector<std::byte> value;
    };

    [[nodiscard]] double nowTicks() const noexcept;
    void blend(const ComponentCodec& codec, const State& earlier, const State& later, double fraction);

    InterpolationSettings settings_;
    std::vector<ComponentCodec> table_;
    std::vector<bool> interpolated_;
    double ticksPerNanosecond_ = 0;
    /// Server tick less the local time in ticks, per state datagram, the
    /// greatest first: its front is the least delayed of the window.
    std::deque<std::pair<std::int64_t, double>> offsets_;
    std::map<std::pair<std::uint32_t, std::size_t>, std::deque<State>> states_;
    std::vector<std::byte> shown_;
    InterpolationStatistics statistics_;
};

} // namespace rawframe::world_replication
