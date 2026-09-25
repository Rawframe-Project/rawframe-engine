#pragma once

// The client's prediction bookkeeping: commands by input tick, predicted
// state by input tick, comparison, rollback, and resimulation.

#include "rawframe/world_replication/prediction.h"

#include <functional>
#include <map>
#include <optional>
#include <span>
#include <vector>

namespace rawframe::world_replication {

class Prediction {
public:
    Prediction(PredictionSettings settings, std::size_t inputSize);

    /// The command for input `tick`: kept for resimulation and, once the
    /// player's state is known, predicted with every tick before it.
    void command(std::uint64_t tick, std::span<const std::byte> value);
    /// The server's values for some predicted components of the player,
    /// after consuming input `consumed`, in `predicted` order with an empty
    /// span for one not in this state.
    void authoritative(std::uint64_t consumed, std::span<const std::span<const std::byte>> values);
    /// The predicted value of the `index`th predicted component, if the
    /// prediction has started.
    [[nodiscard]] std::optional<std::span<const std::byte>> current(std::size_t index) const;
    [[nodiscard]] std::size_t count() const noexcept {
        return settings_.predicted.size();
    }
    void reset();
    /// Called when prediction starts and before every resimulation, to
    /// give the predictor the neighborhood as last heard.
    void neighbors(std::function<void()> place) {
        place_ = std::move(place);
    }

    [[nodiscard]] const PredictionStatistics& statistics() const noexcept {
        return statistics_;
    }

private:
    using State = std::vector<std::vector<std::byte>>;

    /// What the server applies at `tick`: its command, the last command held
    /// for up to `holdLast` ticks, or neutral.
    [[nodiscard]] std::vector<std::byte> commandFor(std::uint64_t tick) const;
    [[nodiscard]] State read() const;
    void write(const State& state);
    /// Predicts every tick after the newest predicted one through `through`.
    void advance(std::uint64_t through);

    PredictionSettings settings_;
    std::size_t inputSize_;
    std::map<std::uint64_t, std::vector<std::byte>> commands_;
    std::map<std::uint64_t, State> history_;
    /// The newest authoritative value of each predicted component, until the
    /// prediction starts.
    State known_;
    bool started_ = false;
    std::uint64_t predictedTick_ = 0;
    std::uint64_t newestCommand_ = 0;
    PredictionStatistics statistics_;
    std::function<void()> place_;
};

} // namespace rawframe::world_replication
