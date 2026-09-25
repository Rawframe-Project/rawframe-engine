#pragma once

#include "rawframe/composition/registrar.h"

#include <cstdint>

namespace rawframe::world_replication {

/// Contributes two World-scoped participants. Each needs the World runtime's
/// Simulation or the game's plan, and a transport; without its switch key it
/// does nothing.
///
/// `rawframe.replication.server` listens, admits players, and adds the
/// replication systems to the World:
///
///   replication.endpoint             where to listen; without it, nothing
///   replication.maximum_connections  admitted at once (64)
///   replication.egress_bytes_per_second
///                                    state bytes per connection (65536)
///
/// `rawframe.replication.bots` plays headless clients against a server,
/// each with its own mirror World, steering its player at random:
///
///   bots.count     how many; 0 or none, nothing (0)
///   bots.endpoint  the server's endpoint (arena)
///   bots.seed      (0)
///   bots.predict   predict each bot's player when the game declares
///                  prediction (true)
void registerParticipants(composition::ParticipantRegistrar& registrar) noexcept;

inline constexpr std::uint8_t kScopes = composition::scopeBit(composition::LifetimeScope::World);

} // namespace rawframe::world_replication
