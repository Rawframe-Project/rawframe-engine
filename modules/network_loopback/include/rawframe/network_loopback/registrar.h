#pragma once

#include "rawframe/composition/registrar.h"

#include <cstdint>

namespace rawframe::network_loopback {

/// Contributes `rawframe.network.loopback`, a Runtime-scoped participant that
/// provides `rawframe.network.transport` as one in-process loopback network
/// on the Host's clock, so every participant of the Runtime reaches the
/// others by name. Configuration keys:
///
///   network.loopback.latency_ms       one way (0)
///   network.loopback.jitter_ms        datagrams only (0)
///   network.loopback.loss_ppm         datagrams lost per million (0)
///   network.loopback.duplicate_ppm    datagrams doubled per million (0)
///   network.loopback.seed             (0)
void registerParticipants(composition::ParticipantRegistrar& registrar) noexcept;

inline constexpr std::uint8_t kScopes = composition::scopeBit(composition::LifetimeScope::Runtime);

} // namespace rawframe::network_loopback
