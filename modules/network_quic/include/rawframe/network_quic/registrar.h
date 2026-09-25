#pragma once

#include "rawframe/composition/registrar.h"

#include <cstdint>

namespace rawframe::network_quic {

/// Contributes `rawframe.network.quic`, a Runtime-scoped participant that
/// provides `rawframe.network.transport` over QUIC. A server needs an
/// identity and a client needs a pin; one process may have both.
/// Configuration keys:
///
///   network.quic.certificate_file   PEM certificate a server presents
///   network.quic.private_key_file   its PEM private key
///   network.quic.self_signed        true: make a fresh identity at start
///   network.quic.fingerprint_file   where to write the identity's
///                                   fingerprint, for clients to pin
///   network.quic.pin                the server fingerprint a client accepts
///   network.quic.pin_file           the same, read from a file
///   network.quic.idle_timeout_ms    (10000)
///   network.quic.keep_alive_ms      (2000)
///   network.quic.webtransport       true: a server also accepts browsers
///                                   over WebTransport (D172); a
///                                   self-signed identity then lives
///                                   13 days, as browsers require (false)
void registerParticipants(composition::ParticipantRegistrar& registrar) noexcept;

inline constexpr std::uint8_t kScopes = composition::scopeBit(composition::LifetimeScope::Runtime);

} // namespace rawframe::network_quic
