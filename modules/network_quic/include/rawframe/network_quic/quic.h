#pragma once

// The QUIC provider (SPEC-0010, ADR-0015): the provider contract over MsQuic
// with TLS 1.3 from OpenSSL. One reliable stream per contract stream, QUIC
// datagrams for datagrams, ALPN `rawframe-game-v1`, and no 0-RTT: nothing a
// peer sends is accepted before the handshake completes. MsQuic is private to
// this module; nothing above it sees a QUIC type.
//
// Endpoints are `host:port`. A listener's host is an IP literal, or empty or
// `*` for every address, and its port may be 0 for any free one; a
// connection's host is a name or an IP literal.

#include "rawframe/execution/time.h"
#include "rawframe/network/provider.h"
#include "rawframe/network_quic/certificate.h"
#include "rawframe/result/result.h"

#include <memory>
#include <optional>

namespace rawframe::network_quic {

inline constexpr std::string_view kAlpn = "rawframe-game-v1";

struct QuicSettings {
    /// Required to listen: the identity a server presents.
    std::optional<Certificate> certificate;
    /// Required to connect: the one server certificate a client accepts.
    std::optional<Fingerprint> pin;
    /// A connection that hears nothing for this long ends.
    execution::MonotonicDuration idleTimeout = execution::MonotonicDuration::fromSeconds(10);
    /// How often an otherwise quiet connection says it is alive.
    execution::MonotonicDuration keepAlive = execution::MonotonicDuration::fromSeconds(2);
    /// A server also accepts browsers: HTTP/3 connections (ALPN `h3`) that
    /// open one WebTransport session, each then a connection like any
    /// other, its bytes those inside the session (D172).
    bool webTransport = false;
    /// Processors MsQuic's threads, and the buffers each keeps, spread
    /// across; nought for the process's effective parallelism, which counts
    /// a container's CPU quota as MsQuic does not (D214). Set by the first
    /// network a process opens; while one is open, others share its choice.
    std::uint32_t processors = 0;
    /// Bytes handed to MsQuic and not yet released by it, per connection and
    /// across one provider's connections: SPEC-0013's egress queued ceilings
    /// (D239). A datagram past either is dropped; a stream send past either
    /// closes its connection, since a reliable stream cannot lose bytes.
    std::size_t maximumSendingBytes = std::size_t{512} << 10U;
    std::size_t maximumSendingBytesInAll = std::size_t{32} << 20U;
    /// Bytes of events waiting for the Runtime across one provider's
    /// connections, SPEC-0013's aggregate ingress ceiling (D239); per
    /// connection it is the provider profile's. An event past it is treated
    /// as one past its connection's queue.
    std::size_t maximumQueuedBytesInAll = std::size_t{16} << 20U;
};

class QuicNetwork {
public:
    /// Opens MsQuic and registers with it. Refuses (`Unavailable`) when
    /// MsQuic cannot start, and (`BadCertificate`) a certificate that cannot
    /// be read.
    [[nodiscard]] static result::Result<std::unique_ptr<QuicNetwork>> create(QuicSettings settings);

    QuicNetwork(const QuicNetwork&) = delete;
    QuicNetwork& operator=(const QuicNetwork&) = delete;
    /// Every provider must be gone first.
    ~QuicNetwork();

    /// A provider on this network, with its own bounds. Refuses
    /// (`invalid_argument`) a profile with any bound left at zero. Each
    /// provider is one Runtime's; MsQuic's own threads feed its queue.
    [[nodiscard]] result::Result<std::unique_ptr<network::Provider>> provider(const network::ProviderProfile& profile);

    struct State;

private:
    explicit QuicNetwork(std::unique_ptr<State> state) noexcept;

    std::unique_ptr<State> state_;
};

} // namespace rawframe::network_quic
