#pragma once

// The bounds replication's sessions and transports run with, on both sides:
// what a host gives the transport it serves replication over, and what the
// mapping budget per publish (records.h) is sized against.

#include "rawframe/network/provider.h"
#include "rawframe/network/session.h"

#include <cstddef>

namespace rawframe::world_replication {

/// The session bounds both sides use: a datagram fits one path MTU.
[[nodiscard]] network::SessionProfile sessionProfile(std::size_t sessions);

/// A transport's room for `connections` connections: 256 KiB a connection
/// holds unread, which a full queue ends.
[[nodiscard]] network::ProviderProfile providerProfile(std::size_t connections);

} // namespace rawframe::world_replication
