#pragma once

// The Worlds a process's clients mirror, for what presents them (sound now,
// pictures later) to read after replication has applied each tick. A client
// knows which mirrored entity is its own player; nothing else does. A
// predicting client's effects wait for presentation to take them.

#include "rawframe/composition/participant.h"
#include "rawframe/world/world.h"
#include "rawframe/world_replication/prediction.h"

#include <cstddef>
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
    /// Client `index`'s effect events since the last take, oldest first,
    /// into `into`, cleared first. Only so many wait; past them the oldest
    /// are let go. None by default.
    virtual void takeEffects(std::size_t index, std::vector<EffectEvent>& into) noexcept {
        static_cast<void>(index);
        into.clear();
    }
};

inline constexpr composition::Capability<ClientWorlds> kClientWorlds{"rawframe.replication.client_worlds"};

} // namespace rawframe::world_replication
