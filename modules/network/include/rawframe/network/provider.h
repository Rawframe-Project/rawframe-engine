#pragma once

// The provider-neutral transport contract (SPEC-0010): listeners, logical
// connections, reliable ordered streams, unreliable datagrams, and a bounded
// queue of owned events the Runtime drains. It carries opaque bytes and knows
// no World, entity, or gameplay meaning. QUIC and loopback providers both
// implement it; nothing above it sees which.

#include "rawframe/result/result.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace rawframe::network {

/// Where a listener is reached. For loopback, a name; for QUIC, host and port.
struct Endpoint {
    std::string name;
};

/// One logical connection, local to the provider that made it. Never reused
/// for the provider's life, never zero.
struct ConnectionId {
    std::uint64_t value = 0;

    [[nodiscard]] constexpr bool valid() const noexcept {
        return value != 0;
    }
    friend constexpr bool operator==(ConnectionId, ConnectionId) noexcept = default;
};

/// A stream within one connection, numbered as QUIC numbers them: the low bit
/// says who opened it (0 the connecting side, 1 the accepting side) and the
/// next bit whether only its opener sends on it.
struct StreamId {
    std::uint64_t value = 0;

    [[nodiscard]] constexpr bool openedByConnector() const noexcept {
        return (value & 1U) == 0;
    }
    [[nodiscard]] constexpr bool unidirectional() const noexcept {
        return (value & 2U) != 0;
    }
    friend constexpr bool operator==(StreamId, StreamId) noexcept = default;
};

/// Every bound a provider enforces. All are required: zero is refused, and
/// nothing grows without one (SPEC-0010 finite profile values).
struct ProviderProfile {
    /// Connections open at once, connecting, admitted, or draining.
    std::size_t maximumConnections = 0;
    /// Streams one side may open on one connection.
    std::size_t maximumStreamsPerConnection = 0;
    /// Bytes one stream send may carry.
    std::size_t maximumStreamSend = 0;
    /// Bytes one datagram may carry, whole.
    std::size_t maximumDatagram = 0;
    /// Events waiting for the Runtime per connection, and their bytes.
    std::size_t maximumQueuedEvents = 0;
    std::size_t maximumQueuedBytes = 0;
};

/// Why a connection ended, as its peer and its owner are told.
enum class CloseReason : std::uint8_t {
    /// Either side closed it on purpose.
    Closed,
    /// Its peer's provider went away.
    PeerGone,
    /// A reliable stream could not be queued: the connection cannot keep its
    /// promise, so it ends rather than drop bytes.
    QueueExhausted,
    /// The listener had no room for another connection.
    Refused,
    /// The peer's identity was not the one this side trusts: a server
    /// certificate that does not match the pin.
    Untrusted,
};

enum class EventKind : std::uint8_t {
    /// A connection this side asked for is ready to carry bytes.
    Connected,
    /// A peer connected to one of this side's listeners.
    Accepted,
    /// Bytes on a stream, in order, possibly a part of what was sent.
    StreamBytes,
    /// One whole datagram.
    Datagram,
    /// The connection ended; nothing more arrives on it.
    Closed,
};

/// One thing that happened, with every byte owned.
struct Event {
    EventKind kind = EventKind::Closed;
    ConnectionId connection;
    StreamId stream;
    std::vector<std::byte> bytes;
    CloseReason reason = CloseReason::Closed;
};

/// What a provider counted. Monotonic totals.
struct ProviderStatistics {
    std::uint64_t datagramsSent = 0;
    std::uint64_t datagramsDelivered = 0;
    /// Lost on the way, or dropped because the receiving queue was full.
    std::uint64_t datagramsDropped = 0;
    std::uint64_t streamBytesSent = 0;
    std::uint64_t streamBytesDelivered = 0;
};

class Provider {
public:
    Provider() = default;
    Provider(const Provider&) = delete;
    Provider& operator=(const Provider&) = delete;
    virtual ~Provider() = default;

    /// Accepts connections at `endpoint` until the provider goes away.
    [[nodiscard]] virtual result::Status listen(const Endpoint& endpoint) = 0;
    /// Starts connecting; `Connected` or `Closed` follows through `poll`.
    [[nodiscard]] virtual result::Result<ConnectionId> connect(const Endpoint& endpoint) = 0;
    /// Opens a stream on a connection. Its bytes reach the peer in order.
    [[nodiscard]] virtual result::Result<StreamId> openStream(ConnectionId connection, bool unidirectional) = 0;
    /// Queues bytes on a stream. Success means the provider owns them now,
    /// not that the peer has them.
    [[nodiscard]] virtual result::Status
    send(ConnectionId connection, StreamId stream, std::span<const std::byte> bytes) = 0;
    /// Queues one datagram, which may be lost, duplicated, or reordered.
    [[nodiscard]] virtual result::Status sendDatagram(ConnectionId connection, std::span<const std::byte> bytes) = 0;
    /// Ends a connection. Both sides see `Closed`; bytes already queued may
    /// still arrive before it.
    virtual void close(ConnectionId connection) noexcept = 0;
    /// Moves up to `maximum` events that have arrived into `into`, oldest
    /// first. Never blocks.
    virtual std::size_t poll(std::vector<Event>& into, std::size_t maximum) = 0;

    [[nodiscard]] virtual ProviderStatistics statistics() const noexcept = 0;
};

} // namespace rawframe::network
