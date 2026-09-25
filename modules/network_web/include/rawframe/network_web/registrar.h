#pragma once

#include "rawframe/composition/registrar.h"

#include <cstdint>

namespace rawframe::network_web {

/// Contributes `rawframe.network.web`, a Runtime-scoped participant that
/// provides `rawframe.network.transport` over the page's WebTransport
/// (web.h). No configuration: the page decides what a name reaches.
void registerParticipants(composition::ParticipantRegistrar& registrar) noexcept;

inline constexpr std::uint8_t kScopes = composition::scopeBit(composition::LifetimeScope::Runtime);

} // namespace rawframe::network_web
