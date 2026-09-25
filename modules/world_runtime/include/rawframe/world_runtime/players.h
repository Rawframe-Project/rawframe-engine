#pragma once

// Players as the World hears of them: an entity made for a connection when
// it is admitted and gone when it leaves, and the durable identity it plays
// under. On a server that asks no platform (ADR-0043), that identity is the
// session the client asked for, which the game's admission rule vetted
// against the client's ticket (D103); a client that asks for no session
// plays without one. Whoever owns players (replication) tells a
// PlayerPresence, if the composition has one, between ticks on the Host
// thread, while the World's structure is unlocked.

#include "rawframe/base/bits128.h"
#include "rawframe/composition/participant.h"
#include "rawframe/world/entity.h"
#include "rawframe/world/world.h"

#include <compare>
#include <cstddef>
#include <optional>
#include <span>

namespace rawframe::world_runtime {

struct PlayerIdentity {
    base::Bits128 value;

    friend constexpr bool operator==(const PlayerIdentity&, const PlayerIdentity&) noexcept = default;
    friend constexpr auto operator<=>(const PlayerIdentity&, const PlayerIdentity&) noexcept = default;
};

/// The identity of the player asking for `session`: SHA-256 of a versioned
/// label and the session's bytes, in its first sixteen bytes. None for an
/// empty session.
[[nodiscard]] std::optional<PlayerIdentity> playerIdentity(std::span<const std::byte> session) noexcept;

class PlayerPresence {
public:
    PlayerPresence() = default;
    PlayerPresence(const PlayerPresence&) = delete;
    PlayerPresence& operator=(const PlayerPresence&) = delete;
    virtual ~PlayerPresence() = default;

    /// `player` was just made, with its starting components, for a player
    /// playing as `identity`.
    virtual void joined(world::World& world, world::EntityHandle player, PlayerIdentity identity) noexcept = 0;
    /// `player` is about to go: its player left, or the server is stopping.
    virtual void leaving(world::World& world, world::EntityHandle player, PlayerIdentity identity) noexcept = 0;
};

inline constexpr composition::Capability<PlayerPresence> kPlayerPresence{"rawframe.world.player_presence"};

} // namespace rawframe::world_runtime
