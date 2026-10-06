#pragma once

#include "rawframe/composition/registrar.h"

#include <cstdint>

namespace rawframe::world_tooling {

/// Contributes `rawframe.tooling.endpoint`, a World-scoped participant that
/// serves the tooling protocol (server.h, D408) on a provider of its own
/// from the composition's transport. A composition selects it with its
/// endpoint; without one it does nothing.
///
///   tooling.endpoint         where to listen; without it, nothing
///   tooling.token_file       the secret a client's hello names, at least
///                            32 bytes, one line; required with an endpoint
///   tooling.grants           what an admitted client may do: `inspect`
///                            (inspect)
///   tooling.maximum_clients  connected at once, 1 to 16 (4)
void registerParticipants(composition::ParticipantRegistrar& registrar) noexcept;

inline constexpr std::uint8_t kScopes = composition::scopeBit(composition::LifetimeScope::World);

} // namespace rawframe::world_tooling
