#pragma once

// The Worlds a process's clients mirror, for what presents them (sound now,
// pictures later) to read after replication has applied each tick. A client
// knows which mirrored entity is its own player; nothing else does. A
// predicting client's effects wait for presentation to read them.

#include "rawframe/composition/participant.h"
#include "rawframe/world/world.h"
#include "rawframe/world_replication/prediction.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace rawframe::world_replication {

/// A predicted effect as presentation hears of it: delivered, or taken back
/// by a resimulation that no longer emits it (D219).
struct EffectEvent {
    PredictedEffect effect;
    bool cancelled = false;
};

struct ClientView {
    world::World* world = nullptr;
    /// The client's own player, or null before admission.
    world::EntityHandle owned;
};

class ClientWorlds {
public:
    ClientWorlds() = default;
    ClientWorlds(const ClientWorlds&) = delete;
    ClientWorlds& operator=(const ClientWorlds&) = delete;
    virtual ~ClientWorlds() = default;

    [[nodiscard]] virtual std::size_t clientCount() const noexcept = 0;
    /// Client `index`'s World and player now; a null World past the count.
    [[nodiscard]] virtual ClientView client(std::size_t index) const noexcept = 0;
    /// Client `index`'s effect events after the first `seen` of them,
    /// oldest first, into `into`, cleared first; returns how many there
    /// have been, for the next call. Each reader keeps its own count, so
    /// each presentation (sound, haptics) reads every event once. Only so
    /// many are kept: a reader further behind misses the oldest. None by
    /// default.
    virtual std::uint64_t readEffects(std::size_t index, std::uint64_t seen, std::vector<EffectEvent>& into) noexcept {
        static_cast<void>(index);
        into.clear();
        return seen;
    }
    /// The client that is the process's own player, played from the
    /// devices its host lends, if one is.
    [[nodiscard]] virtual std::optional<std::size_t> playerClient() const noexcept {
        return std::nullopt;
    }
};

inline constexpr composition::Capability<ClientWorlds> kClientWorlds{"rawframe.replication.client_worlds"};

} // namespace rawframe::world_replication
