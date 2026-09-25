#pragma once

// Client prediction (SPEC-0041, ADR-0054): a client runs the game's
// predicted systems over its own player from its own input, ahead of the
// server, and compares what it predicted with the server's state for the same
// input tick, bit for bit. Equal, the prediction is confirmed; unequal, the
// player goes back to the server's values and every later input is simulated
// again. There is no correction message: corrections are the ordinary state
// records, and the dedicated server predicts nothing.

#include "rawframe/result/result.h"
#include "rawframe/schema/component.h"
#include "rawframe/world/time.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace rawframe::world_replication {

/// One value of another entity the client mirrors, by its NetEntityId.
struct NeighborValue {
    std::uint32_t entity = 0;
    schema::ComponentTypeId component;
    std::span<const std::byte> value;
};

/// A game's predicted systems over one entity, the connection's own player.
/// Values are in memory layout.
class Predictor {
public:
    Predictor() = default;
    Predictor(const Predictor&) = delete;
    Predictor& operator=(const Predictor&) = delete;
    virtual ~Predictor() = default;

    /// The player's value of `component`; empty if the player has none.
    [[nodiscard]] virtual std::span<const std::byte> get(schema::ComponentTypeId component) const noexcept = 0;
    [[nodiscard]] virtual result::Status set(schema::ComponentTypeId component, std::span<const std::byte> value) = 0;
    /// One tick: `input` into the input component, then every predicted
    /// system in schedule order, as the server runs that tick.
    [[nodiscard]] virtual result::Status step(std::span<const std::byte> input) = 0;
    /// The server's tick rate, told on admission before any step: what a
    /// tick's length is, for systems that integrate over time.
    virtual void rate(world::TickRate rate) noexcept = 0;
    /// The other entities the client mirrors, as last heard: their values of
    /// the neighborhood components, in entity then component order. They
    /// replace what the predictor held of other entities; the player's
    /// steps meet them, and they are never compared.
    [[nodiscard]] virtual result::Status place(std::span<const NeighborValue> values) = 0;
};

struct PredictionSettings {
    /// Owned by the caller; outlives the client.
    Predictor* predictor = nullptr;
    /// The player's components the client predicts, all replicated.
    std::vector<schema::ComponentTypeId> predicted;
    /// Predicted ticks kept for comparison. At the cap the client stops
    /// predicting further ahead until the server catches up; it never
    /// extrapolates.
    std::size_t window = 64;
    /// Ticks the server holds the last command when the next is missing,
    /// before input goes neutral: the server's `inputHoldLast`.
    std::uint32_t holdLast = 4;
    /// Components of the other entities the client mirrors that the
    /// predictor is given, as last heard, when prediction starts and before
    /// every resimulation (D39): what the player's own entity may meet.
    /// Replicated components; none gives the predictor nothing else.
    std::vector<schema::ComponentTypeId> neighborhood;
};

struct PredictionStatistics {
    std::uint64_t predictedTicks = 0;
    /// Server states that matched the prediction for their input tick.
    std::uint64_t confirmed = 0;
    /// Server states that did not, and the ticks simulated again after them.
    std::uint64_t rollbacks = 0;
    std::uint64_t resimulatedTicks = 0;
    /// Commands not predicted because the window was full.
    std::uint64_t stalled = 0;
    /// Steps a predicted system refused; the player then waits for the server.
    std::uint64_t failedSteps = 0;
};

} // namespace rawframe::world_replication
