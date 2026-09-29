#include "rawframe/network/session.h"

#include "rawframe/base/assert.h"
#include "rawframe/base/secure_random.h"
#include "rawframe/network/errors.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <deque>
#include <map>

namespace rawframe::network {

namespace {

std::unexpected<result::Error> refuse(result::ErrorClass errorClass, NetworkError error, std::string_view why) {
    return result::fail(errorClass, kNetworkDomain, code(error), why);
}

enum class Phase : std::uint8_t {
    /// Client: waiting for the transport.
    Connecting,
    /// Server: waiting for the hello.
    AwaitingHello,
    /// Client: hello sent, waiting for accept or reject.
    AwaitingVerdict,
    Active,
};

/// An event lane's stream on one connection, either way.
/// An event record sent and not yet received: where it ends in its
/// stream's bytes, its size, and when it was sent.
struct QueuedRecord {
    std::uint64_t end = 0;
    std::size_t bytes = 0;
    execution::MonotonicInstant sent;
};

struct LaneStream {
    std::uint64_t lane = 0;
    StreamId stream;
    /// Incoming: its preface read. Outgoing: written.
    bool prefaced = false;
    std::vector<std::byte> buffer;
    /// Outgoing: every byte written to it, and its records not yet received
    /// (D276).
    std::uint64_t written = 0;
    std::deque<QueuedRecord> queued;
};

struct Connection {
    Phase phase = Phase::Connecting;
    /// The connection's epoch, which its event lanes carry.
    std::uint64_t epoch = 0;
    std::vector<LaneStream> incoming;
    std::vector<LaneStream> outgoing;
    execution::MonotonicInstant deadline;
    std::optional<StreamId> control;
    /// The control stream's preface: sent, on the client; read, on the server.
    bool prefaced = false;
    std::vector<std::byte> buffer;
    Hello hello;
    /// When its last strikes were, a ring of kMaximumStrikes, and how many
    /// there have been.
    std::array<execution::MonotonicInstant, kMaximumStrikes> strikes{};
    std::uint64_t struck = 0;
    /// Its event lanes could not keep their promise: it ends at the next
    /// pump (D276).
    bool exhausted = false;
};

EndReason endReasonOf(CloseReason reason) noexcept {
    switch (reason) {
    case CloseReason::Closed:
        return EndReason::Closed;
    case CloseReason::PeerGone:
        return EndReason::PeerGone;
    case CloseReason::QueueExhausted:
        return EndReason::QueueExhausted;
    case CloseReason::Refused:
        return EndReason::Refused;
    case CloseReason::Untrusted:
        return EndReason::Untrusted;
    }
    return EndReason::Closed;
}

bool lanesValid(std::span<const EventLaneDeclaration> lanes) noexcept {
    if (lanes.size() > kMaximumEventLanes) {
        return false;
    }
    for (std::size_t index = 0; index < lanes.size(); ++index) {
        if (lanes[index].maximumRecord == 0 || lanes[index].maximumRecord > kMaximumEventRecord ||
            lanes[index].id > kMaximumVarint ||
            std::ranges::count(lanes.subspan(index + 1), lanes[index].id, &EventLaneDeclaration::id) != 0) {
            return false;
        }
    }
    return true;
}

bool profileComplete(const SessionProfile& profile) noexcept {
    return profile.maximumSessions != 0 && profile.maximumPreAdmissionBytes != 0 && profile.maximumControlBuffer != 0 &&
           profile.maximumFramePayload != 0 && profile.maximumDatagramPayload != 0 &&
           profile.admissionTimeout.nanoseconds > 0;
}

} // namespace

class SessionCore {
public:
    SessionCore(Provider& provider, const execution::MonotonicSource& clock, bool server) noexcept
        : provider_(&provider), clock_(&clock), server_(server) {
    }

    Provider* provider_;
    const execution::MonotonicSource* clock_;
    bool server_;
    SessionProfile profile_;
    ServerSettings serverSettings_;
    std::vector<EventLaneDeclaration> lanes_;
    bool seeded_ = false;
    std::uint64_t random_ = 0;
    std::uint64_t tickOrigin_ = 0;
    std::uint64_t dropped_ = 0;
    std::uint64_t struckOut_ = 0;
    std::map<std::uint64_t, Connection> connections_;
    /// Rejected and terminated connections, held open until the peer,
    /// having read why, closes, or until their deadline: a transport may
    /// drop what is still queued when this side closes, and a rejection
    /// must arrive (SPEC-0010). To the owner they have already ended.
    std::map<std::uint64_t, execution::MonotonicInstant> rejected_;
    std::vector<Event> events_;
    std::vector<std::byte> scratch_;

    std::uint64_t draw() noexcept {
        if (!seeded_) {
            std::array<std::byte, 8> bytes{};
            const bool kFilled = base::fillSecureRandom(bytes);
            RAWFRAME_CHECK(kFilled, "the secure random source is unavailable");
            std::uint64_t value = 0;
            for (const std::byte kByte : bytes) {
                value = (value << 8U) | std::to_integer<std::uint64_t>(kByte);
            }
            return value;
        }
        random_ += 0x9e3779b97f4a7c15ULL;
        std::uint64_t mixed = random_;
        mixed = (mixed ^ (mixed >> 30U)) * 0xbf58476d1ce4e5b9ULL;
        mixed = (mixed ^ (mixed >> 27U)) * 0x94d049bb133111ebULL;
        return mixed ^ (mixed >> 31U);
    }

    /// A nonzero value within a varint.
    std::uint64_t epoch() noexcept {
        return (draw() & kMaximumVarint) | 1U;
    }

    Nonce nonce() noexcept {
        Nonce made{};
        for (std::size_t half = 0; half < 2; ++half) {
            const std::uint64_t kWord = draw();
            for (std::size_t index = 0; index < 8; ++index) {
                made[(half * 8) + index] = static_cast<std::byte>((kWord >> (8U * index)) & 0xFFU);
            }
        }
        return made;
    }

    std::size_t sessions() const noexcept {
        return connections_.size() + rejected_.size();
    }

    std::size_t admitted() const noexcept {
        std::size_t count = 0;
        for (const auto& [id, state] : connections_) {
            count += state.phase == Phase::Active ? 1 : 0;
        }
        return count;
    }

    /// Counts a strike against an admitted connection; true when it is
    /// the one that makes kMaximumStrikes within kStrikeWindow.
    bool struckOut(Connection& state) noexcept {
        const execution::MonotonicInstant kNow = clock_->now();
        state.strikes[state.struck % kMaximumStrikes] = kNow;
        ++state.struck;
        // The oldest of the last kMaximumStrikes is where the next goes.
        const bool kOut =
            state.struck >= kMaximumStrikes && (kNow - state.strikes[state.struck % kMaximumStrikes]) < kStrikeWindow;
        struckOut_ += kOut ? 1 : 0;
        return kOut;
    }

    void end(ConnectionId connection, EndReason reason, std::vector<SessionEvent>& into) {
        if (connections_.erase(connection.value) != 0) {
            provider_->close(connection);
            into.push_back(SessionEvent{.kind = SessionEventKind::Ended, .connection = connection, .reason = reason});
        }
    }

    result::Status
    sendControl(ConnectionId connection, Connection& state, ControlFrame type, std::span<const std::byte> payload) {
        if (!state.control) {
            return refuse(result::ErrorClass::FailedPrecondition, NetworkError::WrongStream, "no control stream yet");
        }
        scratch_.resize(payload.size() + 32);
        Writer writer{scratch_};
        if (!state.prefaced && !server_) {
            // The client's first bytes on its control stream are the preface.
            RAWFRAME_TRY(writePreface(writer, StreamPreface{.kind = StreamKind::Control}));
        }
        RAWFRAME_TRY(writeFrame(writer, static_cast<std::uint64_t>(type), payload));
        // A frame may be larger than one stream send: it goes in order.
        std::span<const std::byte> rest = writer.written();
        while (!rest.empty()) {
            const std::size_t kPart = std::min<std::size_t>(rest.size(), 1024);
            RAWFRAME_TRY(provider_->send(connection, *state.control, rest.first(kPart)));
            rest = rest.subspan(kPart);
        }
        return {};
    }

    void onTransport(const Event& event, std::vector<SessionEvent>& into) {
        const execution::MonotonicInstant kNow = clock_->now();
        switch (event.kind) {
        case EventKind::Accepted:
            if (!server_ || sessions() >= profile_.maximumSessions) {
                provider_->close(event.connection);
                return;
            }
            connections_[event.connection.value] =
                Connection{.phase = Phase::AwaitingHello, .deadline = kNow + profile_.admissionTimeout};
            return;
        case EventKind::Connected: {
            const auto kFound = connections_.find(event.connection.value);
            if (kFound == connections_.end()) {
                return;
            }
            Connection& state = kFound->second;
            auto stream = provider_->openStream(event.connection, false);
            if (!stream.has_value()) {
                end(event.connection, EndReason::QueueExhausted, into);
                return;
            }
            state.control = *stream;
            std::vector<std::byte> hello(kMaximumTicketBytes + 512);
            Writer writer{hello};
            if (!encodeHello(writer, state.hello).has_value() ||
                !sendControl(event.connection, state, ControlFrame::ProtocolHello, writer.written()).has_value()) {
                end(event.connection, EndReason::ProtocolViolation, into);
                return;
            }
            state.prefaced = true;
            state.phase = Phase::AwaitingVerdict;
            return;
        }
        case EventKind::StreamBytes:
            onStreamBytes(event, into);
            return;
        case EventKind::Datagram:
            onDatagram(event, into);
            return;
        case EventKind::Closed:
            rejected_.erase(event.connection.value);
            if (connections_.erase(event.connection.value) != 0) {
                into.push_back(SessionEvent{.kind = SessionEventKind::Ended,
                                            .connection = event.connection,
                                            .reason = endReasonOf(event.reason)});
            }
            return;
        }
    }

    /// The lane `id` if this side receives it.
    const EventLaneDeclaration* laneFrom(std::uint64_t id) const noexcept {
        const auto kFound = std::ranges::find(lanes_, id, &EventLaneDeclaration::id);
        return kFound != lanes_.end() && kFound->fromServer != server_ ? &*kFound : nullptr;
    }

    /// Bytes on one of the peer's event-lane streams: its preface names a
    /// lane this side receives, under the connection's epoch, on no other
    /// stream; then event records, each at most the lane's bound.
    void onLaneBytes(const Event& event, Connection& state, std::vector<SessionEvent>& into) {
        auto lane = std::ranges::find(state.incoming, event.stream, &LaneStream::stream);
        if (lane == state.incoming.end()) {
            if (state.incoming.size() >= kMaximumEventLanes) {
                end(event.connection, EndReason::ProtocolViolation, into);
                return;
            }
            state.incoming.push_back(LaneStream{.stream = event.stream});
            lane = state.incoming.end() - 1;
        }
        const EventLaneDeclaration* declared = lane->prefaced ? laneFrom(lane->lane) : nullptr;
        // A preface and one whole record with its frame and message type:
        // the lane's record once its preface is read, the largest before.
        std::size_t largest = 0;
        for (const EventLaneDeclaration& each : lanes_) {
            largest = std::max(largest, each.fromServer != server_ ? each.maximumRecord : 0);
        }
        const std::size_t kBound = (declared != nullptr ? declared->maximumRecord : largest) + 64;
        if (lane->buffer.size() + event.bytes.size() > kBound) {
            end(event.connection, EndReason::ProtocolViolation, into);
            return;
        }
        lane->buffer.insert(lane->buffer.end(), event.bytes.begin(), event.bytes.end());
        Reader reader{lane->buffer};
        if (!lane->prefaced) {
            auto preface = readPreface(reader);
            if (!preface.has_value()) {
                if (preface.error().code() != code(NetworkError::Truncated)) {
                    end(event.connection, EndReason::ProtocolViolation, into);
                }
                return;
            }
            declared = laneFrom(preface->eventLaneId);
            if (preface->kind != StreamKind::EventLane || declared == nullptr ||
                preface->eventLaneEpoch != state.epoch ||
                std::ranges::any_of(state.incoming, [&](const LaneStream& other) {
                    return other.prefaced && other.lane == preface->eventLaneId;
                })) {
                end(event.connection, EndReason::ProtocolViolation, into);
                return;
            }
            lane->lane = preface->eventLaneId;
            lane->prefaced = true;
        }
        while (true) {
            auto frame = readFrame(reader, declared->maximumRecord + varintSize(kMaximumVarint));
            if (!frame.has_value()) {
                if (frame.error().code() != code(NetworkError::Truncated)) {
                    end(event.connection, EndReason::ProtocolViolation, into);
                    return;
                }
                break;
            }
            if (!criticalFrame(frame->type)) {
                continue; // an extension this side does not use
            }
            Reader record{frame->payload};
            const auto kType = record.varint();
            if (frame->type != kEventRecordFrame || !kType.has_value() ||
                record.remaining() > declared->maximumRecord) {
                end(event.connection, EndReason::ProtocolViolation, into);
                return;
            }
            into.push_back(SessionEvent{.kind = SessionEventKind::Event,
                                        .connection = event.connection,
                                        .eventLane = lane->lane,
                                        .payloadType = *kType,
                                        .payload = {record.rest().begin(), record.rest().end()}});
        }
        lane->buffer.erase(lane->buffer.begin(), lane->buffer.begin() + static_cast<std::ptrdiff_t>(reader.consumed()));
    }

    void onStreamBytes(const Event& event, std::vector<SessionEvent>& into) {
        const auto kFound = connections_.find(event.connection.value);
        if (kFound == connections_.end()) {
            return;
        }
        Connection& state = kFound->second;
        // The peer's one-way streams are its event lanes, once admitted.
        const bool kPeers = event.stream.openedByConnector() == server_;
        if (state.phase == Phase::Active && event.stream.unidirectional() && kPeers) {
            onLaneBytes(event, state, into);
            return;
        }
        // Otherwise only the control stream: the client's first two-way
        // stream, and nothing else may arrive.
        if (state.control && !(*state.control == event.stream)) {
            end(event.connection, EndReason::ProtocolViolation, into);
            return;
        }
        if (!state.control) {
            if (!server_ || !event.stream.openedByConnector() || event.stream.unidirectional()) {
                end(event.connection, EndReason::ProtocolViolation, into);
                return;
            }
            state.control = event.stream;
        }
        const std::size_t kBound =
            state.phase == Phase::Active ? profile_.maximumControlBuffer : profile_.maximumPreAdmissionBytes;
        if (state.buffer.size() + event.bytes.size() > kBound) {
            end(event.connection, EndReason::ProtocolViolation, into);
            return;
        }
        state.buffer.insert(state.buffer.end(), event.bytes.begin(), event.bytes.end());

        Reader reader{state.buffer};
        if (server_ && !state.prefaced) {
            auto preface = readPreface(reader);
            if (!preface.has_value()) {
                if (preface.error().code() != code(NetworkError::Truncated)) {
                    end(event.connection, EndReason::ProtocolViolation, into);
                }
                return;
            }
            if (preface->kind != StreamKind::Control) {
                end(event.connection, EndReason::ProtocolViolation, into);
                return;
            }
            state.prefaced = true;
        }
        while (true) {
            auto frame = readFrame(reader, profile_.maximumFramePayload);
            if (!frame.has_value()) {
                if (frame.error().code() != code(NetworkError::Truncated)) {
                    end(event.connection, EndReason::ProtocolViolation, into);
                    return;
                }
                break;
            }
            if (!onFrame(event.connection, state, *frame, into)) {
                return;
            }
        }
        state.buffer.erase(state.buffer.begin(), state.buffer.begin() + static_cast<std::ptrdiff_t>(reader.consumed()));
    }

    /// False once the connection has been ended.
    bool onFrame(ConnectionId connection, Connection& state, const Frame& frame, std::vector<SessionEvent>& into) {
        if (!criticalFrame(frame.type)) {
            return true; // an extension this side does not use
        }
        const auto kType = static_cast<ControlFrame>(frame.type);
        switch (state.phase) {
        case Phase::AwaitingHello:
            if (kType != ControlFrame::ProtocolHello) {
                end(connection, EndReason::ProtocolViolation, into);
                return false;
            }
            return admit(connection, state, frame.payload, into);
        case Phase::AwaitingVerdict:
            return verdict(connection, state, kType, frame.payload, into);
        case Phase::Active:
            if (kType == ControlFrame::ProtocolHello || kType == ControlFrame::ProtocolAccept ||
                kType == ControlFrame::ProtocolReject ||
                frame.type > static_cast<std::uint64_t>(ControlFrame::GracefulClose)) {
                end(connection, EndReason::ProtocolViolation, into);
                return false;
            }
            into.push_back(SessionEvent{.kind = SessionEventKind::Frame,
                                        .connection = connection,
                                        .frameType = frame.type,
                                        .payload = {frame.payload.begin(), frame.payload.end()}});
            return true;
        case Phase::Connecting:
            end(connection, EndReason::ProtocolViolation, into);
            return false;
        }
        return false;
    }

    bool admit(ConnectionId connection,
               Connection& state,
               std::span<const std::byte> payload,
               std::vector<SessionEvent>& into) {
        auto hello = decodeHello(payload);
        std::optional<Reject> refusal;
        if (!hello.has_value()) {
            refusal = Reject{.reason = RejectReason::Malformed, .message = "the hello does not decode"};
        } else if (const auto kReason = compare(*hello, serverSettings_.expected, serverSettings_.features)) {
            refusal = Reject{.reason = *kReason, .message = {}};
        } else if (serverSettings_.maximumAdmitted != 0 && admitted() >= serverSettings_.maximumAdmitted) {
            refusal = Reject{.reason = RejectReason::Capacity, .message = "the server is full"};
        } else if (serverSettings_.admit != nullptr) {
            refusal = serverSettings_.admit(*hello, serverSettings_.admitContext);
        }
        std::vector<std::byte> answer(512);
        Writer writer{answer};
        if (refusal) {
            if (encodeReject(writer, *refusal).has_value() &&
                sendControl(connection, state, ControlFrame::ProtocolReject, writer.written()).has_value()) {
                // The peer closes once it has read the rejection; the
                // connection is held until then, within the admission bound.
                connections_.erase(connection.value);
                rejected_[connection.value] = clock_->now() + profile_.admissionTimeout;
                into.push_back(SessionEvent{
                    .kind = SessionEventKind::Ended, .connection = connection, .reason = EndReason::Rejected});
                return false;
            }
            end(connection, EndReason::Rejected, into);
            return false;
        }
        Accept accept{.compatibility = serverSettings_.expected,
                      .features = serverSettings_.features,
                      .session = hello->requestedSession,
                      .connection = connection.value,
                      .connectionEpoch = epoch(),
                      .inputEpoch = epoch(),
                      .replicationEpoch = epoch(),
                      .tickRateTicks = serverSettings_.tickRateTicks,
                      .tickRateSeconds = serverSettings_.tickRateSeconds,
                      .tickOrigin = tickOrigin_,
                      .maximumDatagram =
                          std::min<std::uint64_t>(hello->maximumDatagram, profile_.maximumDatagramPayload),
                      .maximumFrame = std::min<std::uint64_t>(hello->maximumFrame, profile_.maximumFramePayload),
                      .nonce = nonce()};
        if (!encodeAccept(writer, accept).has_value() ||
            !sendControl(connection, state, ControlFrame::ProtocolAccept, writer.written()).has_value()) {
            end(connection, EndReason::QueueExhausted, into);
            return false;
        }
        state.phase = Phase::Active;
        state.epoch = accept.connectionEpoch;
        into.push_back(SessionEvent{.kind = SessionEventKind::Admitted,
                                    .connection = connection,
                                    .accept = accept,
                                    .requestedSession = hello->requestedSession});
        return true;
    }

    bool verdict(ConnectionId connection,
                 Connection& state,
                 ControlFrame type,
                 std::span<const std::byte> payload,
                 std::vector<SessionEvent>& into) {
        if (type == ControlFrame::ProtocolReject) {
            auto reject = decodeReject(payload);
            if (reject.has_value()) {
                into.push_back(SessionEvent{
                    .kind = SessionEventKind::Rejected, .connection = connection, .reject = std::move(*reject)});
            }
            end(connection, reject.has_value() ? EndReason::Rejected : EndReason::ProtocolViolation, into);
            return false;
        }
        auto accept = type == ControlFrame::ProtocolAccept ? decodeAccept(payload) : result::Result<Accept>{};
        // The server must grant exactly what was offered, within what the
        // client said it takes.
        if (type != ControlFrame::ProtocolAccept || !accept.has_value() ||
            !(accept->compatibility == state.hello.compatibility) ||
            accept->maximumDatagram > state.hello.maximumDatagram || accept->maximumFrame > state.hello.maximumFrame) {
            end(connection, EndReason::ProtocolViolation, into);
            return false;
        }
        state.phase = Phase::Active;
        state.epoch = accept->connectionEpoch;
        into.push_back(
            SessionEvent{.kind = SessionEventKind::Admitted, .connection = connection, .accept = std::move(*accept)});
        return true;
    }

    void onDatagram(const Event& event, std::vector<SessionEvent>& into) {
        const auto kFound = connections_.find(event.connection.value);
        if (kFound == connections_.end() || kFound->second.phase != Phase::Active) {
            ++dropped_;
            return;
        }
        auto record = readDatagram(event.bytes, profile_.maximumDatagramPayload);
        // A server hears input and a client hears state; nothing else.
        const DatagramLane kExpected = server_ ? DatagramLane::Input : DatagramLane::State;
        if (!record.has_value() || record->lane != kExpected) {
            ++dropped_;
            if (struckOut(kFound->second)) {
                end(event.connection, EndReason::ProtocolViolation, into);
            }
            return;
        }
        into.push_back(SessionEvent{.kind = SessionEventKind::Datagram,
                                    .connection = event.connection,
                                    .lane = record->lane,
                                    .laneEpoch = record->laneEpoch,
                                    .sequence = record->sequence,
                                    .payloadType = record->payloadType,
                                    .payload = {record->payload.begin(), record->payload.end()}});
    }

    /// Lets go of a connection's event records its peer has received, and
    /// says how many are left and their bytes.
    std::pair<std::size_t, std::size_t> settle(ConnectionId connection, Connection& state) {
        std::size_t records = 0;
        std::size_t bytes = 0;
        for (LaneStream& lane : state.outgoing) {
            const std::uint64_t kReceived = lane.written - provider_->pendingBytes(connection, lane.stream);
            while (!lane.queued.empty() && lane.queued.front().end <= kReceived) {
                lane.queued.pop_front();
            }
            records += lane.queued.size();
            for (const QueuedRecord& record : lane.queued) {
                bytes += record.bytes;
            }
        }
        return {records, bytes};
    }

    /// Whether an event record has waited past SPEC-0013's age for its peer.
    bool overdue(ConnectionId connection, Connection& state, execution::MonotonicInstant now) {
        static_cast<void>(settle(connection, state));
        return std::ranges::any_of(state.outgoing, [now](const LaneStream& lane) {
            return !lane.queued.empty() && kMaximumEventAge < now - lane.queued.front().sent;
        });
    }

    void expire(std::vector<SessionEvent>& into) {
        const execution::MonotonicInstant kNow = clock_->now();
        std::vector<std::uint64_t> late;
        std::vector<std::uint64_t> exhausted;
        for (auto& [id, state] : connections_) {
            if (state.phase != Phase::Active && !(kNow < state.deadline)) {
                late.push_back(id);
            } else if (state.phase == Phase::Active && (state.exhausted || overdue(ConnectionId{id}, state, kNow))) {
                exhausted.push_back(id);
            }
        }
        for (const std::uint64_t kId : late) {
            end(ConnectionId{kId}, EndReason::TimedOut, into);
        }
        for (const std::uint64_t kId : exhausted) {
            end(ConnectionId{kId}, EndReason::QueueExhausted, into);
        }
        for (auto held = rejected_.begin(); held != rejected_.end();) {
            if (kNow < held->second) {
                ++held;
            } else {
                provider_->close(ConnectionId{held->first});
                held = rejected_.erase(held);
            }
        }
    }
};

Sessions::Sessions(std::unique_ptr<SessionCore> core) noexcept : core_(std::move(core)) {
}

Sessions::~Sessions() {
    for (const auto& [id, state] : core_->connections_) {
        core_->provider_->close(ConnectionId{id});
    }
    for (const auto& [id, deadline] : core_->rejected_) {
        core_->provider_->close(ConnectionId{id});
    }
}

result::Result<std::unique_ptr<Sessions>>
Sessions::server(Provider& provider, const execution::MonotonicSource& clock, const ServerSettings& settings) {
    if (!profileComplete(settings.profile) || settings.tickRateTicks == 0 || settings.tickRateSeconds == 0 ||
        !lanesValid(settings.lanes)) {
        return refuse(result::ErrorClass::InvalidArgument,
                      NetworkError::InvalidProfile,
                      "every session bound and the tick rate are required, and at most 16 distinct lanes");
    }
    auto core = std::make_unique<SessionCore>(provider, clock, true);
    core->profile_ = settings.profile;
    core->lanes_ = settings.lanes;
    core->serverSettings_ = settings;
    core->seeded_ = settings.seed.has_value();
    core->random_ = settings.seed.value_or(0);
    return std::make_unique<Sessions>(std::move(core));
}

result::Result<std::unique_ptr<Sessions>>
Sessions::client(Provider& provider, const execution::MonotonicSource& clock, const ClientSettings& settings) {
    if (!profileComplete(settings.profile) || !lanesValid(settings.lanes)) {
        return refuse(result::ErrorClass::InvalidArgument,
                      NetworkError::InvalidProfile,
                      "every session bound is required, and at most 16 distinct lanes");
    }
    auto core = std::make_unique<SessionCore>(provider, clock, false);
    core->profile_ = settings.profile;
    core->lanes_ = settings.lanes;
    core->seeded_ = settings.seed.has_value();
    core->random_ = settings.seed.value_or(0);
    return std::make_unique<Sessions>(std::move(core));
}

result::Status Sessions::listen(const Endpoint& endpoint) {
    if (!core_->server_) {
        return refuse(result::ErrorClass::FailedPrecondition, NetworkError::WrongStream, "a client does not listen");
    }
    return core_->provider_->listen(endpoint);
}

result::Result<ConnectionId> Sessions::connect(const Endpoint& endpoint, const Hello& hello) {
    if (core_->server_ || core_->sessions() >= core_->profile_.maximumSessions) {
        return refuse(result::ErrorClass::ResourceExhausted,
                      NetworkError::Exhausted,
                      "a server does not connect, and a client connects within its bound");
    }
    RAWFRAME_TRY_ASSIGN(const ConnectionId kConnection, core_->provider_->connect(endpoint));
    Connection state{.phase = Phase::Connecting,
                     .deadline = core_->clock_->now() + core_->profile_.admissionTimeout,
                     .hello = hello};
    state.hello.nonce = core_->nonce();
    core_->connections_[kConnection.value] = std::move(state);
    return kConnection;
}

void Sessions::pump(std::vector<SessionEvent>& into) {
    core_->events_.clear();
    core_->provider_->poll(core_->events_, 4096);
    for (const Event& event : core_->events_) {
        core_->onTransport(event, into);
    }
    core_->expire(into);
}

result::Status Sessions::sendFrame(ConnectionId connection, ControlFrame type, std::span<const std::byte> payload) {
    const auto kFound = core_->connections_.find(connection.value);
    if (kFound == core_->connections_.end() || kFound->second.phase != Phase::Active) {
        return refuse(
            result::ErrorClass::FailedPrecondition, NetworkError::StaleConnection, "not an admitted connection");
    }
    if (payload.size() > core_->profile_.maximumFramePayload || type == ControlFrame::ProtocolHello ||
        type == ControlFrame::ProtocolAccept || type == ControlFrame::ProtocolReject) {
        return refuse(result::ErrorClass::InvalidArgument, NetworkError::TooLarge, "not a frame to send once admitted");
    }
    return core_->sendControl(connection, kFound->second, type, payload);
}

result::Status Sessions::sendDatagram(ConnectionId connection, const DatagramRecord& record) {
    const auto kFound = core_->connections_.find(connection.value);
    if (kFound == core_->connections_.end() || kFound->second.phase != Phase::Active) {
        return refuse(
            result::ErrorClass::FailedPrecondition, NetworkError::StaleConnection, "not an admitted connection");
    }
    const DatagramLane kMine = core_->server_ ? DatagramLane::State : DatagramLane::Input;
    if (record.lane != kMine || record.payload.size() > core_->profile_.maximumDatagramPayload) {
        return refuse(
            result::ErrorClass::InvalidArgument, NetworkError::WrongStream, "not this side's lane, or too large");
    }
    core_->scratch_.resize(record.payload.size() + 48);
    Writer writer{core_->scratch_};
    RAWFRAME_TRY(writeDatagram(writer, record));
    return core_->provider_->sendDatagram(connection, writer.written());
}

result::Status Sessions::sendEvent(ConnectionId connection,
                                   std::uint64_t lane,
                                   std::uint64_t messageType,
                                   std::span<const std::byte> body) {
    const auto kFound = core_->connections_.find(connection.value);
    if (kFound == core_->connections_.end() || kFound->second.phase != Phase::Active) {
        return refuse(
            result::ErrorClass::FailedPrecondition, NetworkError::StaleConnection, "not an admitted connection");
    }
    const auto kDeclared = std::ranges::find(core_->lanes_, lane, &EventLaneDeclaration::id);
    if (kDeclared == core_->lanes_.end() || kDeclared->fromServer != core_->server_ ||
        body.size() > kDeclared->maximumRecord || messageType > kMaximumVarint) {
        return refuse(result::ErrorClass::InvalidArgument,
                      NetworkError::WrongStream,
                      "not a lane this side sends on, or a record past its bound");
    }
    Connection& state = kFound->second;
    if (state.exhausted) {
        return refuse(result::ErrorClass::ResourceExhausted,
                      NetworkError::Exhausted,
                      "the connection's event lanes could not keep their promise; it is ending");
    }
    auto stream = std::ranges::find(state.outgoing, lane, &LaneStream::lane);
    if (stream == state.outgoing.end()) {
        RAWFRAME_TRY_ASSIGN(const StreamId kOpened, core_->provider_->openStream(connection, true));
        state.outgoing.push_back(LaneStream{.lane = lane, .stream = kOpened});
        stream = state.outgoing.end() - 1;
    }
    core_->scratch_.resize(body.size() + 64);
    Writer writer{core_->scratch_};
    if (!stream->prefaced) {
        RAWFRAME_TRY(writePreface(
            writer, StreamPreface{.kind = StreamKind::EventLane, .eventLaneId = lane, .eventLaneEpoch = state.epoch}));
    }
    std::vector<std::byte> record(varintSize(messageType) + body.size());
    Writer recordWriter{record};
    RAWFRAME_TRY(recordWriter.varint(messageType));
    RAWFRAME_TRY(recordWriter.bytes(body));
    RAWFRAME_TRY(writeFrame(writer, kEventRecordFrame, record));
    // SPEC-0013's queued events: one more than the peer can hold unread
    // ends the connection rather than wait or drop it.
    const auto [kRecords, kBytes] = core_->settle(connection, state);
    if (kRecords + 1 > kMaximumQueuedEventRecords || kBytes + writer.written().size() > kMaximumQueuedEventBytes) {
        state.exhausted = true;
        return refuse(result::ErrorClass::ResourceExhausted,
                      NetworkError::Exhausted,
                      "the peer has not received what its event lanes hold; the connection is ending");
    }
    // A record may be larger than one stream send: it goes in order.
    std::span<const std::byte> rest = writer.written();
    while (!rest.empty()) {
        const std::size_t kPart = std::min<std::size_t>(rest.size(), 1024);
        RAWFRAME_TRY(core_->provider_->send(connection, stream->stream, rest.first(kPart)));
        stream->written += kPart;
        rest = rest.subspan(kPart);
    }
    stream->queued.push_back(
        QueuedRecord{.end = stream->written, .bytes = writer.written().size(), .sent = core_->clock_->now()});
    stream->prefaced = true;
    return {};
}

void Sessions::setTickOrigin(std::uint64_t tick) noexcept {
    core_->tickOrigin_ = tick;
}

void Sessions::close(ConnectionId connection) noexcept {
    if (core_->connections_.erase(connection.value) != 0) {
        core_->provider_->close(connection);
    }
}

void Sessions::closeAfterSending(ConnectionId connection) noexcept {
    if (core_->connections_.erase(connection.value) != 0) {
        core_->rejected_[connection.value] = core_->clock_->now() + core_->profile_.admissionTimeout;
    }
}

bool Sessions::strike(ConnectionId connection) noexcept {
    const auto kFound = core_->connections_.find(connection.value);
    if (kFound == core_->connections_.end() || kFound->second.phase != Phase::Active) {
        return false;
    }
    if (core_->struckOut(kFound->second)) {
        close(connection);
        return false;
    }
    return true;
}

std::uint64_t Sessions::droppedDatagrams() const noexcept {
    return core_->dropped_;
}

std::uint64_t Sessions::struckOut() const noexcept {
    return core_->struckOut_;
}

} // namespace rawframe::network
