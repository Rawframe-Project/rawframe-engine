#pragma once

// Logical connections and admission (SPEC-0010 provider lifecycle and
// application admission) over any Provider. A client connects, opens the one
// control stream, and says hello; the server checks exact compatibility and
// its own admission rule and answers once. Nothing reaches the owner as
// gameplay before a connection is admitted, and a peer that breaks the
// protocol is closed, not trusted. Once admitted, guaranteed events travel
// on declared event lanes (D265), each a one-way stream of its own.

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

/// SPEC-0013's malformed and security strikes: the eighth within ten
/// seconds ends an admitted connection as a protocol violation (D223).
inline constexpr std::size_t kMaximumStrikes = 8;
inline constexpr execution::MonotonicDuration kStrikeWindow = execution::MonotonicDuration::fromSeconds(10);

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

/// SPEC-0013's hard ceilings on guaranteed events: lanes, and one record.
inline constexpr std::size_t kMaximumEventLanes = 16;
inline constexpr std::size_t kMaximumEventRecord = std::size_t{64} * 1024;
/// SPEC-0013's queued guaranteed events per connection: records sent that
/// the peer has not yet received, their bytes, and how long one may wait
/// (D276). Past any of them the lane cannot keep its promise, and the
/// connection ends as exhausted: no promised event is dropped.
inline constexpr std::size_t kMaximumQueuedEventRecords = 1024;
inline constexpr std::size_t kMaximumQueuedEventBytes = std::size_t{128} * 1024;
inline constexpr execution::MonotonicDuration kMaximumEventAge = execution::MonotonicDuration::fromSeconds(2);

/// An event lane (SPEC-0010 guaranteed events): reliable and ordered within
/// itself, independent of every other lane. Both sides declare the same
/// lanes, which exact compatibility guarantees; admission authorizes them
/// under the connection's epoch, and a stream for any other is a protocol
/// violation.
struct EventLaneDeclaration {
    std::uint64_t id = 0;
    /// Whether the server sends on it; the client sends on the others.
    bool fromServer = true;
    /// Bytes one record's body may hold, 1 to kMaximumEventRecord.
    std::size_t maximumRecord = 0;
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
    /// A record on an event lane this side receives.
    Event,
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
    /// Frame: its type and payload. Datagram: the record's fields. Event:
    /// its lane, and its message type and body as `payloadType` and
    /// `payload`.
    std::uint64_t frameType = 0;
    std::uint64_t eventLane = 0;
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
    std::vector<EventLaneDeclaration> lanes;
    /// Seeds epochs and nonces, for tests and runs that must repeat. Without
    /// a seed they come from the secure source, which is what a peer on a
    /// real network must meet (D27).
    std::optional<std::uint64_t> seed;
};

struct ClientSettings {
    SessionProfile profile;
    std::vector<EventLaneDeclaration> lanes;
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
    /// One record on an event lane this side sends to an admitted peer: a
    /// message type the lane's protocol gives meaning, and a body within the
    /// lane's bound. Success means the transport owns it; one it cannot
    /// keep ends the connection (`QueueExhausted`), never drops the record.
    [[nodiscard]] result::Status
    sendEvent(ConnectionId connection, std::uint64_t lane, std::uint64_t messageType, std::span<const std::byte> body);
    /// The tick new admissions start from (server).
    void setTickOrigin(std::uint64_t tick) noexcept;
    /// Ends a connection the owner is done with. No `Ended` follows: the
    /// owner forgets the connection itself.
    void close(ConnectionId connection) noexcept;
    /// As `close`, but what was sent last is given its chance to arrive: the
    /// connection is held, as a rejected one is, until the peer closes or
    /// the admission timeout passes (D267).
    void closeAfterSending(ConnectionId connection) noexcept;
    /// A strike against an admitted peer whose payload the owner found
    /// malformed. The strike that makes kMaximumStrikes within
    /// kStrikeWindow closes the connection as `close` does and answers
    /// false, and the owner forgets it; true while it stays open.
    /// Datagram records this side cannot read, or on the wrong lane, are
    /// strikes too; those end the connection with an `Ended` event.
    [[nodiscard]] bool strike(ConnectionId connection) noexcept;

    /// Records dropped before reaching the owner: before admission, on the
    /// wrong lane, or malformed.
    [[nodiscard]] std::uint64_t droppedDatagrams() const noexcept;
    /// Connections ended for their strikes.
    [[nodiscard]] std::uint64_t struckOut() const noexcept;

    explicit Sessions(std::unique_ptr<SessionCore> core) noexcept;

private:
    std::unique_ptr<SessionCore> core_;
};

} // namespace rawframe::network
