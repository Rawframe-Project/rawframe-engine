#pragma once

// The Worlds a process's clients mirror, for what presents them (sound now,
// pictures later) to read after replication has applied each tick. A client
// knows which mirrored entity is its own player; nothing else does.

#include "rawframe/composition/participant.h"
#include "rawframe/world/world.h"

#include <cstddef>

namespace rawframe::world_replication {

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
};

inline constexpr composition::Capability<ClientWorlds> kClientWorlds{"rawframe.replication.client_worlds"};

} // namespace rawframe::world_replication
