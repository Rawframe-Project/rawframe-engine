#pragma once

// Logical connections and admission (SPEC-0010 provider lifecycle and
// application admission) over any Provider. A client connects, opens the one
// control stream, and says hello; the server checks exact compatibility and
// its own admission rule and answers once. Nothing reaches the owner as
// gameplay before a connection is admitted, and a peer that breaks the
// protocol is closed, not trusted.

#include "rawframe/execution/time.h"
#include "rawframe/network/admission.h"
#include "rawframe/network/provider.h"
#include "rawframe/network/wire.h"
#include "rawframe/result/result.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <vector>

namespace rawframe::network {

/// Every bound a session side keeps. All are required.
struct SessionProfile {
    /// Connections admitting or admitted at once.
    std::size_t maximumSessions = 0;
    /// Control bytes held before admission, which is less than after.
    std::size_t maximumPreAdmissionBytes = 0;
    /// Control bytes held waiting for the rest of a frame, once admitted.
    std::size_t maximumControlBuffer = 0;
    std::size_t maximumFramePayload = 0;
    std::size_t maximumDatagramPayload = 0;
    /// From connecting to admitted.
    execution::MonotonicDuration admissionTimeout;
};

enum class SessionEventKind : std::uint8_t {
    /// Admission done: `accept` says what both sides now keep.
    Admitted,
    /// The server said no (client side only).
    Rejected,
    /// A control frame after admission.
    Frame,
    /// A datagram record on the lane this side receives.
    Datagram,
    /// The connection is over.
    Ended,
};

enum class EndReason : std::uint8_t {
    Closed,
    PeerGone,
    Refused,
    Rejected,
    TimedOut,
    ProtocolViolation,
    QueueExhausted,
    /// The transport did not trust the peer's identity.
    Untrusted,
};

struct SessionEvent {
    SessionEventKind kind = SessionEventKind::Ended;
    ConnectionId connection;
    /// Admitted.
    Accept accept;
    /// Admitted, server side: the session the client asked for.
    std::vector<std::byte> requestedSession;
    /// Rejected.
    Reject reject;
    /// Frame: its type and payload. Datagram: the record's fields.
    std::uint64_t frameType = 0;
    DatagramLane lane = DatagramLane::Input;
    std::uint64_t laneEpoch = 0;
    std::uint64_t sequence = 0;
    std::uint64_t payloadType = 0;
    std::vector<std::byte> payload;
    /// Ended.
    EndReason reason = EndReason::Closed;
};

/// A server's own admission rule after exact compatibility: tickets,
/// session identity, policy. Answers a rejection, or nothing to admit.
using AdmitFunction = std::optional<Reject> (*)(const Hello& hello, void* context) noexcept;

struct ServerSettings {
    SessionProfile profile;
    Compatibility expected;
    std::uint64_t features = 0;
    AdmitFunction admit = nullptr;
    void* admitContext = nullptr;
    /// Connections admitted at once; a hello past it is refused as
    /// `capacity`, before the admission rule. Zero admits up to
    /// `maximumSessions`. Keep it below that, so a full server still has
    /// room to say why.
    std::size_t maximumAdmitted = 0;
    std::uint64_t tickRateTicks = 60;
    std::uint64_t tickRateSeconds = 1;
    /// Seeds epochs and nonces, for tests and runs that must repeat. Without
    /// a seed they come from the secure source, which is what a peer on a
    /// real network must meet (D27).
    std::optional<std::uint64_t> seed;
};

struct ClientSettings {
    SessionProfile profile;
    std::optional<std::uint64_t> seed;
};

class SessionCore;

/// One side of the application protocol over one provider. Thread-affine:
/// one thread calls it; the provider may be shared with nothing else.
class Sessions {
public:
    /// A server: listens, admits, and receives the input lane.
    [[nodiscard]] static result::Result<std::unique_ptr<Sessions>>
    server(Provider& provider, const execution::MonotonicSource& clock, const ServerSettings& settings);
    /// A client: connects, says hello, and receives the state lane.
    [[nodiscard]] static result::Result<std::unique_ptr<Sessions>>
    client(Provider& provider, const execution::MonotonicSource& clock, const ClientSettings& settings);

    ~Sessions();

    [[nodiscard]] result::Status listen(const Endpoint& endpoint);
    /// Starts a connection that will say `hello` (its nonce is filled in).
    [[nodiscard]] result::Result<ConnectionId> connect(const Endpoint& endpoint, const Hello& hello);

    /// Polls the provider, advances every connection, and appends what the
    /// owner must know to `into`.
    void pump(std::vector<SessionEvent>& into);

    /// A control frame to an admitted peer.
    [[nodiscard]] result::Status
    sendFrame(ConnectionId connection, ControlFrame type, std::span<const std::byte> payload);
    /// A datagram record to an admitted peer, on the lane this side sends.
    [[nodiscard]] result::Status sendDatagram(ConnectionId connection, const DatagramRecord& record);
    /// The tick new admissions start from (server).
    void setTickOrigin(std::uint64_t tick) noexcept;
    void close(ConnectionId connection) noexcept;

    /// Records dropped before reaching the owner: before admission, on the
    /// wrong lane, or malformed.
    [[nodiscard]] std::uint64_t droppedDatagrams() const noexcept;

    explicit Sessions(std::unique_ptr<SessionCore> core) noexcept;

private:
    std::unique_ptr<SessionCore> core_;
};

} // namespace rawframe::network
