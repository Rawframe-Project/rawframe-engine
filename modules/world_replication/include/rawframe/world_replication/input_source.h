#pragma once

// Where a client's input comes from, tick by tick: a player's devices through
// the game's input mapping, or a bot's hand on the same controls. Replication
// encodes what a source fills and knows nothing of how it was made; the
// mapping layer lives in client modules, never in the dedicated server.

#include "rawframe/composition/participant.h"
#include "rawframe/result/result.h"
#include "rawframe/world_replication/messages.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

namespace rawframe::world_replication {

class InputSource {
public:
    InputSource() = default;
    InputSource(const InputSource&) = delete;
    InputSource& operator=(const InputSource&) = delete;
    virtual ~InputSource() = default;

    /// Fills `input` (the input component's bytes) for the client's tick
    /// `tick`. It holds the input made for the tick before, nought before
    /// the first, which a source may build on (D525).
    [[nodiscard]] virtual result::Status next(std::uint64_t tick, std::span<std::byte> input) = 0;
    /// Appends the commands the last `next` sent (D425), in order; none by
    /// default.
    virtual void takeCommands(std::vector<PostedCommand>& into) {
        static_cast<void>(into);
    }
};

/// Makes input sources for a game's clients.
class InputSourcePlan {
public:
    InputSourcePlan() = default;
    InputSourcePlan(const InputSourcePlan&) = delete;
    InputSourcePlan& operator=(const InputSourcePlan&) = delete;
    virtual ~InputSourcePlan() = default;

    /// A source for a bot, whose hand is seeded by `seed`. Refuses
    /// (`NotFound`) for a game that declares no input mapping.
    [[nodiscard]] virtual result::Result<std::unique_ptr<InputSource>> botSource(std::uint64_t seed) = 0;
    /// The source of the process's local player `player` (D363): the
    /// devices its host lends that are paired to it. Refuses (`NotFound`)
    /// for a game that declares no input mapping or a host that lends no
    /// devices, (`InvalidArgument`) a player past `kMaximumLocalPlayers`,
    /// and (`AlreadyExists`) a player's a second time, since a device plays
    /// one player.
    [[nodiscard]] virtual result::Result<std::unique_ptr<InputSource>> playerSource(std::size_t player) = 0;
};

inline constexpr composition::Capability<InputSourcePlan> kInputSourcePlan{"rawframe.replication.input_sources"};

} // namespace rawframe::world_replication
