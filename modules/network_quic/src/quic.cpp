#include "rawframe/network_quic/quic.h"

#include "rawframe/base/assert.h"
#include "rawframe/base/threads.h"
#include "rawframe/network/errors.h"
#include "rawframe/network_quic/errors.h"
#include "tls.h"
#include "webtransport.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <deque>
#include <limits>
#include <map>
#include <memory>
#include <msquic.h>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace rawframe::network_quic {

using network::CloseReason;
using network::ConnectionId;
using network::Event;
using network::EventKind;
using network::NetworkError;
using network::StreamId;

namespace {

// Application error codes on the wire: why this side ended a connection.
constexpr QUIC_UINT62 kClosedCode = 0;
constexpr QUIC_UINT62 kQueueExhaustedCode = 1;
constexpr QUIC_UINT62 kGoneCode = 2;

// The transport error a server sends when its listener refuses (RFC 9000).
constexpr QUIC_UINT62 kConnectionRefusedError = 0x2;

// A browser's connection: HTTP/3, carrying WebTransport (D172).
constexpr std::string_view kH3Alpn = "h3";
// The streams an HTTP/3 peer opens besides its WebTransport ones: its
// control stream and QPACK's two.
constexpr std::uint16_t kH3PeerStreams = 3;

std::unexpected<result::Error> refuse(result::ErrorClass errorClass, NetworkError error, std::string_view why) {
    return result::fail(errorClass, network::kNetworkDomain, network::code(error), why);
}

std::unexpected<result::Error> fail(result::ErrorClass errorClass, QuicError error, std::string_view why) {
    return result::fail(errorClass, kQuicDomain, code(error), why);
}

struct HostPort {
    std::string host;
    std::uint16_t port = 0;
};

/// `host:port`, `[v6]:port`, or `:port`. The port is 1 to 65535, or 0 for
/// a listener that lets the system choose.
std::optional<HostPort> parseEndpoint(std::string_view name, bool listening) {
    const std::size_t kColon = name.rfind(':');
    if (kColon == std::string_view::npos) {
        return std::nullopt;
    }
    std::string_view host = name.substr(0, kColon);
    const std::string_view kPort = name.substr(kColon + 1);
    unsigned port = 0;
    const auto [end, error] = std::from_chars(kPort.data(), kPort.data() + kPort.size(), port);
    if (error != std::errc{} || end != kPort.data() + kPort.size() || (port == 0 && !listening) || port > 65535) {
        return std::nullopt;
    }
    if (host.size() >= 2 && host.front() == '[' && host.back() == ']') {
        host = host.substr(1, host.size() - 2);
    }
    return HostPort{.host = std::string{host}, .port = static_cast<std::uint16_t>(port)};
}

struct Core;

/// One contract stream: a MsQuic stream and the QUIC number both sides know
/// it by.
struct Stream {
    Core* core = nullptr;
    std::uint64_t connection = 0;
    HQUIC handle = nullptr;
    std::uint64_t id = 0;
};

/// Bytes MsQuic reads until it says it is done with them.
struct Sending {
    QUIC_BUFFER buffer{};
    std::vector<std::uint8_t> bytes;
    std::uint64_t connection = 0;
};

struct Connection {
    Core* core = nullptr;
    std::uint64_t id = 0;
    /// Null once MsQuic has finished with it.
    HQUIC handle = nullptr;
    bool connector = false;
    /// The owner was told it is ready (`Connected` or `Accepted`).
    bool announced = false;
    /// The owner may still use it: no close asked for, no shutdown begun.
    bool open = true;
    CloseReason reason = CloseReason::Closed;
    bool reasonKnown = false;
    std::uint64_t bidirectionalOpened = 0;
    std::uint64_t unidirectionalOpened = 0;
    std::map<std::uint64_t, Stream*> streams;
    std::size_t queuedEvents = 0;
    std::size_t queuedBytes = 0;
    /// Bytes handed to MsQuic and not yet released by it.
    std::size_t sendingBytes = 0;
    bool datagramsEnabled = false;
    std::size_t datagramLimit = 0;
    /// A browser's connection: its HTTP/3 and WebTransport, which it is
    /// announced only once it opens a session through.
    std::unique_ptr<WebTransportServer> web;

    void noteReason(CloseReason why) noexcept {
        if (!reasonKnown) {
            reason = why;
            reasonKnown = true;
        }
    }
};

/// One provider's state, shared with MsQuic's threads through callback
/// contexts. Every field is guarded by `mutex`. Only MsQuic calls that return
/// without waiting for a MsQuic thread are made while holding it; closing a
/// handle from outside a callback waits, so it happens after unlocking.
struct Core {
    const QUIC_API_TABLE* api = nullptr;
    HQUIC registration = nullptr;
    const QuicSettings* settings = nullptr;
    network::ProviderProfile profile;
    HQUIC serverConfiguration = nullptr;
    HQUIC clientConfiguration = nullptr;

    base::Mutex mutex;
    base::Condition finished;
    HQUIC listener = nullptr;
    bool stopping = false;
    std::uint64_t nextConnection = 1;
    std::map<std::uint64_t, std::unique_ptr<Connection>> connections;
    /// Connections MsQuic still holds a handle for.
    std::size_t liveHandles = 0;
    std::deque<Event> inbound;
    network::ProviderStatistics statistics;

    [[nodiscard]] std::size_t openConnections() const noexcept {
        return static_cast<std::size_t>(std::count_if(connections.begin(), connections.end(), [](const auto& entry) {
            return entry.second->handle != nullptr;
        }));
    }

    [[nodiscard]] bool fits(const Connection& connection, std::size_t bytes) const noexcept {
        return connection.queuedEvents + 1 <= profile.maximumQueuedEvents &&
               connection.queuedBytes + bytes <= profile.maximumQueuedBytes;
    }

    void deliver(Connection& connection, Event event) {
        connection.queuedEvents += 1;
        connection.queuedBytes += event.bytes.size();
        event.connection = ConnectionId{connection.id};
        inbound.push_back(std::move(event));
    }

    /// Starts ending a connection: the peer hears `code`, the owner hears
    /// `why` once MsQuic has finished with it.
    void shutDown(Connection& connection, CloseReason why, QUIC_UINT62 code) noexcept {
        // A browser hears HTTP/3's codes; Rawframe's own mean nothing to it.
        if (connection.web != nullptr && code < kH3NoError) {
            code = kH3NoError;
        }
        connection.noteReason(why);
        connection.open = false;
        if (connection.handle != nullptr) {
            api->ConnectionShutdown(connection.handle, QUIC_CONNECTION_SHUTDOWN_FLAG_NONE, code);
        }
    }

    Connection* find(std::uint64_t id) noexcept {
        const auto kFound = connections.find(id);
        return kFound == connections.end() ? nullptr : kFound->second.get();
    }
};

QUIC_STATUS streamCallback(HQUIC handle, void* context, QUIC_STREAM_EVENT* event) noexcept;

void releaseSending(Core& core, Sending* sending) noexcept {
    if (Connection* connection = core.find(sending->connection)) {
        connection->sendingBytes -= sending->bytes.size();
    }
    delete sending;
}

/// Closes a stream MsQuic has finished with. On a MsQuic thread, inside one of
/// its callbacks, so the close does not wait.
void closeStream(Core& core, Stream* stream) noexcept {
    if (Connection* connection = core.find(stream->connection)) {
        connection->streams.erase(stream->id);
    }
    core.api->StreamClose(stream->handle);
    delete stream;
}

void deliverStreamBytes(Core& core, Connection& connection, const Stream& stream, std::vector<std::byte> bytes) {
    if (!core.fits(connection, bytes.size())) {
        // A reliable stream cannot drop bytes, so the connection ends.
        core.shutDown(connection, CloseReason::QueueExhausted, kQueueExhaustedCode);
        return;
    }
    core.deliver(connection,
                 Event{.kind = EventKind::StreamBytes, .stream = StreamId{stream.id}, .bytes = std::move(bytes)});
}

/// Hands bytes this side sends of its own (HTTP/3's, not the owner's) to
/// MsQuic on `stream`. False if MsQuic refused them.
bool sendOwn(Core& core, Connection& connection, HQUIC stream, std::span<const std::byte> bytes) {
    auto* sending = new Sending{.connection = connection.id};
    const auto* kFirst = reinterpret_cast<const std::uint8_t*>(bytes.data());
    sending->bytes.assign(kFirst, kFirst + bytes.size());
    sending->buffer.Length = static_cast<std::uint32_t>(sending->bytes.size());
    sending->buffer.Buffer = sending->bytes.data();
    connection.sendingBytes += bytes.size();
    if (QUIC_FAILED(core.api->StreamSend(stream, &sending->buffer, 1, QUIC_SEND_FLAG_NONE, sending))) {
        releaseSending(core, sending);
        return false;
    }
    return true;
}

/// What a browser's stream carried: HTTP/3 this side answers, the session
/// opening or ending, or the owner's bytes.
void arrivedOnWeb(Core& core, Connection& connection, Stream& stream, std::span<const std::byte> bytes, bool finished) {
    WebTransportServer::Arrived arrived = connection.web->receive(stream.id, bytes, finished);
    if (arrived.failure.has_value()) {
        core.shutDown(connection, CloseReason::PeerGone, *arrived.failure);
        return;
    }
    if (!arrived.reply.empty() && !sendOwn(core, connection, stream.handle, arrived.reply)) {
        core.shutDown(connection, CloseReason::PeerGone, kH3NoError);
        return;
    }
    if (arrived.opened) {
        connection.announced = true;
        core.deliver(connection, Event{.kind = EventKind::Accepted});
    }
    if (!arrived.bytes.empty() && connection.announced) {
        deliverStreamBytes(core, connection, stream, std::move(arrived.bytes));
    }
    if (arrived.closed) {
        core.shutDown(connection, CloseReason::Closed, kH3NoError);
    }
}

/// Opens this side's HTTP/3 control stream and says its SETTINGS. It is a
/// one-way stream of this side's, numbered like the rest.
bool openControlStream(Core& core, Connection& connection) {
    const std::uint64_t kId = (connection.unidirectionalOpened << 2U) | 2U | 1U;
    auto stream = std::make_unique<Stream>(Stream{.core = &core, .connection = connection.id, .id = kId});
    if (QUIC_FAILED(core.api->StreamOpen(
            connection.handle, QUIC_STREAM_OPEN_FLAG_UNIDIRECTIONAL, &streamCallback, stream.get(), &stream->handle))) {
        return false;
    }
    if (QUIC_FAILED(core.api->StreamStart(
            stream->handle, QUIC_STREAM_START_FLAG_IMMEDIATE | QUIC_STREAM_START_FLAG_SHUTDOWN_ON_FAIL))) {
        core.api->StreamClose(stream->handle);
        return false;
    }
    ++connection.unidirectionalOpened;
    Stream* kept = stream.release();
    connection.streams.emplace(kId, kept);
    return sendOwn(core, connection, kept->handle, WebTransportServer::controlStream());
}

QUIC_STATUS streamCallback(HQUIC, void* context, QUIC_STREAM_EVENT* event) noexcept {
    auto* stream = static_cast<Stream*>(context);
    Core& core = *stream->core;
    const std::lock_guard kLock{core.mutex};
    switch (event->Type) {
    case QUIC_STREAM_EVENT_START_COMPLETE:
        // Streams are numbered in the order they start, which is the order
        // openStream started them in.
        RAWFRAME_CHECK(QUIC_FAILED(event->START_COMPLETE.Status) || event->START_COMPLETE.ID == stream->id,
                       "a QUIC stream started with another number than the one handed out");
        break;
    case QUIC_STREAM_EVENT_RECEIVE: {
        Connection* connection = core.find(stream->connection);
        if (connection == nullptr || !connection->open) {
            break;
        }
        std::vector<std::byte> bytes;
        bytes.reserve(static_cast<std::size_t>(event->RECEIVE.TotalBufferLength));
        for (std::uint32_t index = 0; index < event->RECEIVE.BufferCount; ++index) {
            const QUIC_BUFFER& kBuffer = event->RECEIVE.Buffers[index];
            const auto kBytes = std::as_bytes(std::span{kBuffer.Buffer, kBuffer.Length});
            bytes.insert(bytes.end(), kBytes.begin(), kBytes.end());
        }
        if (connection->web != nullptr) {
            arrivedOnWeb(core, *connection, *stream, bytes, false);
            break;
        }
        deliverStreamBytes(core, *connection, *stream, std::move(bytes));
        break;
    }
    case QUIC_STREAM_EVENT_PEER_SEND_SHUTDOWN: {
        Connection* connection = core.find(stream->connection);
        if (connection != nullptr && connection->open && connection->web != nullptr) {
            arrivedOnWeb(core, *connection, *stream, {}, true);
        }
        break;
    }
    case QUIC_STREAM_EVENT_SEND_COMPLETE:
        releaseSending(core, static_cast<Sending*>(event->SEND_COMPLETE.ClientContext));
        break;
    case QUIC_STREAM_EVENT_SHUTDOWN_COMPLETE:
        closeStream(core, stream);
        break;
    default:
        break;
    }
    return QUIC_STATUS_SUCCESS;
}

/// Why a connection the transport ended ended.
CloseReason transportReason(const QUIC_CONNECTION_EVENT& event) noexcept {
    if (event.SHUTDOWN_INITIATED_BY_TRANSPORT.Status == QUIC_STATUS_CONNECTION_REFUSED ||
        event.SHUTDOWN_INITIATED_BY_TRANSPORT.ErrorCode == kConnectionRefusedError) {
        return CloseReason::Refused;
    }
    if (event.SHUTDOWN_INITIATED_BY_TRANSPORT.Status == QUIC_STATUS_BAD_CERTIFICATE) {
        return CloseReason::Untrusted;
    }
    return CloseReason::PeerGone;
}

CloseReason peerReason(QUIC_UINT62 code) noexcept {
    switch (code) {
    case kQueueExhaustedCode:
        return CloseReason::QueueExhausted;
    case kGoneCode:
        return CloseReason::PeerGone;
    default:
        return CloseReason::Closed;
    }
}

/// MsQuic has finished with a connection: every stream is closed, and nothing
/// more arrives on it. The owner hears `Closed` if it ever knew of it.
void finishConnection(Core& core, Connection& connection) noexcept {
    while (!connection.streams.empty()) {
        closeStream(core, connection.streams.begin()->second);
    }
    core.api->ConnectionClose(connection.handle);
    connection.handle = nullptr;
    connection.open = false;
    --core.liveHandles;
    if (connection.announced || connection.connector) {
        core.deliver(connection, Event{.kind = EventKind::Closed, .reason = connection.reason});
    } else {
        core.connections.erase(connection.id);
    }
    core.finished.notify_all();
}

QUIC_STATUS connectionCallback(HQUIC, void* context, QUIC_CONNECTION_EVENT* event) noexcept {
    auto* connection = static_cast<Connection*>(context);
    Core& core = *connection->core;
    const std::lock_guard kLock{core.mutex};
    switch (event->Type) {
    case QUIC_CONNECTION_EVENT_CONNECTED: {
        // MsQuic says whether datagrams may be sent only some packets after
        // the handshake (D172); asked now, a datagram sent at once is not
        // dropped. The path's limit follows with its first state change.
        std::uint8_t sendEnabled = 0;
        std::uint32_t length = sizeof(sendEnabled);
        if (!connection->datagramsEnabled &&
            QUIC_SUCCEEDED(
                core.api->GetParam(connection->handle, QUIC_PARAM_CONN_DATAGRAM_SEND_ENABLED, &length, &sendEnabled)) &&
            sendEnabled != 0) {
            connection->datagramsEnabled = true;
            connection->datagramLimit = core.profile.maximumDatagram;
        }
        if (connection->open && !connection->connector &&
            std::string_view{reinterpret_cast<const char*>(event->CONNECTED.NegotiatedAlpn),
                             event->CONNECTED.NegotiatedAlpnLength} == kH3Alpn) {
            // A browser: announced once its session opens, not before.
            connection->web = std::make_unique<WebTransportServer>();
            if (!openControlStream(core, *connection)) {
                core.shutDown(*connection, CloseReason::PeerGone, kH3NoError);
            }
        } else if (connection->open) {
            connection->announced = true;
            core.deliver(*connection,
                         Event{.kind = connection->connector ? EventKind::Connected : EventKind::Accepted});
        }
        break;
    }
    case QUIC_CONNECTION_EVENT_PEER_CERTIFICATE_RECEIVED: {
        // Portable certificates: the DER bytes, which the pin is the SHA-256 of.
        const auto* certificate = static_cast<const QUIC_BUFFER*>(event->PEER_CERTIFICATE_RECEIVED.Certificate);
        const bool kPinned =
            certificate != nullptr && certificate->Length != 0 && core.settings->pin.has_value() &&
            base::sha256(std::as_bytes(std::span{certificate->Buffer, certificate->Length})) == *core.settings->pin;
        if (!kPinned) {
            connection->noteReason(CloseReason::Untrusted);
            return QUIC_STATUS_BAD_CERTIFICATE;
        }
        break;
    }
    case QUIC_CONNECTION_EVENT_SHUTDOWN_INITIATED_BY_TRANSPORT:
        connection->noteReason(transportReason(*event));
        connection->open = false;
        break;
    case QUIC_CONNECTION_EVENT_SHUTDOWN_INITIATED_BY_PEER:
        connection->noteReason(peerReason(event->SHUTDOWN_INITIATED_BY_PEER.ErrorCode));
        connection->open = false;
        break;
    case QUIC_CONNECTION_EVENT_SHUTDOWN_COMPLETE:
        finishConnection(core, *connection);
        break;
    case QUIC_CONNECTION_EVENT_PEER_STREAM_STARTED: {
        auto* stream =
            new Stream{.core = &core, .connection = connection->id, .handle = event->PEER_STREAM_STARTED.Stream};
        std::uint32_t length = sizeof(stream->id);
        QUIC_UINT62 id = 0;
        if (QUIC_FAILED(core.api->GetParam(stream->handle, QUIC_PARAM_STREAM_ID, &length, &id))) {
            delete stream;
            return QUIC_STATUS_INTERNAL_ERROR;
        }
        stream->id = id;
        connection->streams.emplace(stream->id, stream);
        core.api->SetCallbackHandler(stream->handle, reinterpret_cast<void*>(&streamCallback), stream);
        break;
    }
    case QUIC_CONNECTION_EVENT_DATAGRAM_STATE_CHANGED:
        connection->datagramsEnabled = event->DATAGRAM_STATE_CHANGED.SendEnabled != 0;
        connection->datagramLimit = event->DATAGRAM_STATE_CHANGED.MaxSendLength;
        break;
    case QUIC_CONNECTION_EVENT_DATAGRAM_RECEIVED: {
        const QUIC_BUFFER& kBuffer = *event->DATAGRAM_RECEIVED.Buffer;
        std::optional<std::span<const std::byte>> payload = std::as_bytes(std::span{kBuffer.Buffer, kBuffer.Length});
        if (connection->web != nullptr) {
            // A browser's datagram names its session first.
            payload = connection->web->datagram(*payload);
        }
        if (!connection->open || !connection->announced || !payload || !core.fits(*connection, payload->size())) {
            ++core.statistics.datagramsDropped;
            break;
        }
        core.deliver(*connection, Event{.kind = EventKind::Datagram, .bytes = {payload->begin(), payload->end()}});
        break;
    }
    case QUIC_CONNECTION_EVENT_DATAGRAM_SEND_STATE_CHANGED: {
        const QUIC_DATAGRAM_SEND_STATE kState = event->DATAGRAM_SEND_STATE_CHANGED.State;
        if (QUIC_DATAGRAM_SEND_STATE_IS_FINAL(kState)) {
            if (kState == QUIC_DATAGRAM_SEND_LOST_DISCARDED || kState == QUIC_DATAGRAM_SEND_CANCELED) {
                ++core.statistics.datagramsDropped;
            }
            releaseSending(core, static_cast<Sending*>(event->DATAGRAM_SEND_STATE_CHANGED.ClientContext));
        }
        break;
    }
    default:
        break;
    }
    return QUIC_STATUS_SUCCESS;
}

QUIC_STATUS listenerCallback(HQUIC, void* context, QUIC_LISTENER_EVENT* event) noexcept {
    if (event->Type != QUIC_LISTENER_EVENT_NEW_CONNECTION) {
        return QUIC_STATUS_SUCCESS;
    }
    Core& core = *static_cast<Core*>(context);
    const std::lock_guard kLock{core.mutex};
    // Refusing here sends CONNECTION_REFUSED, which the connector hears as
    // `Refused` before it was ever connected.
    if (core.stopping || core.openConnections() >= core.profile.maximumConnections) {
        return QUIC_STATUS_CONNECTION_REFUSED;
    }
    const HQUIC kHandle = event->NEW_CONNECTION.Connection;
    const QUIC_STATUS kStatus = core.api->ConnectionSetConfiguration(kHandle, core.serverConfiguration);
    if (QUIC_FAILED(kStatus)) {
        // MsQuic closes a connection its listener did not take, and with no
        // handler set, nothing about it reaches this provider.
        return kStatus;
    }
    auto connection = std::make_unique<Connection>();
    connection->core = &core;
    connection->id = core.nextConnection++;
    connection->handle = kHandle;
    core.api->SetCallbackHandler(kHandle, reinterpret_cast<void*>(&connectionCallback), connection.get());
    ++core.liveHandles;
    core.connections.emplace(connection->id, std::move(connection));
    return QUIC_STATUS_SUCCESS;
}

/// Rawframe's ALPN, and HTTP/3's after it where a server accepts browsers.
std::array<QUIC_BUFFER, 2> alpnsFor(bool browsers) noexcept {
    const auto kBuffer = [](std::string_view alpn) {
        return QUIC_BUFFER{static_cast<std::uint32_t>(alpn.size()),
                           reinterpret_cast<std::uint8_t*>(const_cast<char*>(alpn.data()))};
    };
    return {kBuffer(kAlpn), browsers ? kBuffer(kH3Alpn) : QUIC_BUFFER{}};
}

/// A configuration: ALPN, the settings a profile implies, and credentials.
HQUIC openConfiguration(const QUIC_API_TABLE& api,
                        HQUIC registration,
                        const QuicSettings& settings,
                        const network::ProviderProfile& profile,
                        const QUIC_CREDENTIAL_CONFIG& credential,
                        bool browsers) {
    QUIC_SETTINGS quic{};
    quic.IdleTimeoutMs = static_cast<std::uint64_t>(settings.idleTimeout.nanoseconds / 1'000'000);
    quic.IsSet.IdleTimeoutMs = 1;
    quic.KeepAliveIntervalMs = static_cast<std::uint32_t>(settings.keepAlive.nanoseconds / 1'000'000);
    quic.IsSet.KeepAliveIntervalMs = 1;
    const auto kStreams =
        static_cast<std::uint16_t>(std::min<std::size_t>(profile.maximumStreamsPerConnection, 65'535));
    quic.PeerBidiStreamCount = kStreams;
    quic.IsSet.PeerBidiStreamCount = 1;
    quic.PeerUnidiStreamCount =
        static_cast<std::uint16_t>(browsers ? std::min(kStreams + kH3PeerStreams, 65'535) : kStreams);
    quic.IsSet.PeerUnidiStreamCount = 1;
    quic.DatagramReceiveEnabled = 1;
    quic.IsSet.DatagramReceiveEnabled = 1;
    // No resumption means no 0-RTT: nothing is accepted before a handshake.
    quic.ServerResumptionLevel = QUIC_SERVER_NO_RESUME;
    quic.IsSet.ServerResumptionLevel = 1;

    const std::array<QUIC_BUFFER, 2> kAlpns = alpnsFor(browsers);
    HQUIC configuration = nullptr;
    if (QUIC_FAILED(api.ConfigurationOpen(
            registration, kAlpns.data(), browsers ? 2U : 1U, &quic, sizeof(quic), nullptr, &configuration))) {
        return nullptr;
    }
    if (QUIC_FAILED(api.ConfigurationLoadCredential(configuration, &credential))) {
        api.ConfigurationClose(configuration);
        return nullptr;
    }
    return configuration;
}

} // namespace

struct QuicNetwork::State {
    const QUIC_API_TABLE* api = nullptr;
    HQUIC registration = nullptr;
    QuicSettings settings;
    /// The server identity as MsQuic loads it; empty without a certificate.
    std::vector<std::byte> pkcs12;

    State() = default;
    State(const State&) = delete;
    State& operator=(const State&) = delete;
    ~State() {
        if (registration != nullptr) {
            api->RegistrationClose(registration);
        }
        if (api != nullptr) {
            MsQuicClose(api);
        }
    }
};

namespace {

class QuicProvider final : public network::Provider {
public:
    explicit QuicProvider(std::unique_ptr<Core> core) noexcept : core_(std::move(core)) {
    }

    ~QuicProvider() override {
        HQUIC listener = nullptr;
        {
            const std::lock_guard kLock{core_->mutex};
            core_->stopping = true;
            listener = std::exchange(core_->listener, nullptr);
            for (auto& [id, connection] : core_->connections) {
                if (connection->handle != nullptr) {
                    core_->shutDown(*connection, CloseReason::Closed, kGoneCode);
                }
            }
        }
        if (listener != nullptr) {
            // Waits for the listener to stop, so nothing new is accepted.
            core_->api->ListenerClose(listener);
        }
        {
            std::unique_lock lock{core_->mutex};
            core_->finished.wait(lock, [this] {
                return core_->liveHandles == 0;
            });
        }
        if (core_->serverConfiguration != nullptr) {
            core_->api->ConfigurationClose(core_->serverConfiguration);
        }
        if (core_->clientConfiguration != nullptr) {
            core_->api->ConfigurationClose(core_->clientConfiguration);
        }
    }

    result::Status listen(const network::Endpoint& endpoint) override {
        if (core_->serverConfiguration == nullptr) {
            return fail(result::ErrorClass::FailedPrecondition,
                        QuicError::MissingIdentity,
                        "listening needs a certificate in the QUIC settings");
        }
        const std::optional<HostPort> kWhere = parseEndpoint(endpoint.name, true);
        QUIC_ADDR address{};
        if (!kWhere.has_value()) {
            return fail(result::ErrorClass::InvalidArgument, QuicError::BadEndpoint, "an endpoint is host:port");
        }
        if (kWhere->host.empty() || kWhere->host == "*") {
            QuicAddrSetFamily(&address, QUIC_ADDRESS_FAMILY_UNSPEC);
            QuicAddrSetPort(&address, kWhere->port);
        } else if (!QuicAddrFromString(kWhere->host.c_str(), kWhere->port, &address)) {
            return fail(
                result::ErrorClass::InvalidArgument, QuicError::BadEndpoint, "a listen address is an IP literal");
        }
        const std::lock_guard kLock{core_->mutex};
        if (core_->listener != nullptr) {
            return refuse(
                result::ErrorClass::AlreadyExists, NetworkError::Unreachable, "this provider already listens");
        }
        HQUIC listener = nullptr;
        if (QUIC_FAILED(core_->api->ListenerOpen(core_->registration, &listenerCallback, core_.get(), &listener))) {
            return fail(result::ErrorClass::Unavailable, QuicError::Unavailable, "MsQuic could not open a listener");
        }
        const bool kBrowsers = core_->settings->webTransport;
        const std::array<QUIC_BUFFER, 2> kAlpns = alpnsFor(kBrowsers);
        if (QUIC_FAILED(core_->api->ListenerStart(listener, kAlpns.data(), kBrowsers ? 2U : 1U, &address))) {
            // Never started, so closing it waits for nothing.
            core_->api->ListenerClose(listener);
            return refuse(result::ErrorClass::AlreadyExists, NetworkError::Unreachable, "the endpoint is taken");
        }
        core_->listener = listener;
        return {};
    }

    result::Result<ConnectionId> connect(const network::Endpoint& endpoint) override {
        if (core_->clientConfiguration == nullptr) {
            return fail(result::ErrorClass::FailedPrecondition,
                        QuicError::MissingIdentity,
                        "connecting needs a pinned fingerprint in the QUIC settings");
        }
        const std::optional<HostPort> kWhere = parseEndpoint(endpoint.name, false);
        if (!kWhere.has_value() || kWhere->host.empty()) {
            return fail(result::ErrorClass::InvalidArgument, QuicError::BadEndpoint, "an endpoint is host:port");
        }
        std::unique_lock lock{core_->mutex};
        if (core_->openConnections() >= core_->profile.maximumConnections) {
            return refuse(
                result::ErrorClass::ResourceExhausted, NetworkError::Exhausted, "this provider has no room to connect");
        }
        auto connection = std::make_unique<Connection>();
        connection->core = core_.get();
        connection->id = core_->nextConnection++;
        connection->connector = true;
        if (QUIC_FAILED(core_->api->ConnectionOpen(
                core_->registration, &connectionCallback, connection.get(), &connection->handle))) {
            return fail(result::ErrorClass::Unavailable, QuicError::Unavailable, "MsQuic could not open a connection");
        }
        const QUIC_STATUS kStarted = core_->api->ConnectionStart(connection->handle,
                                                                 core_->clientConfiguration,
                                                                 QUIC_ADDRESS_FAMILY_UNSPEC,
                                                                 kWhere->host.c_str(),
                                                                 kWhere->port);
        if (QUIC_FAILED(kStarted)) {
            const HQUIC kHandle = connection->handle;
            lock.unlock();
            core_->api->ConnectionClose(kHandle);
            return refuse(result::ErrorClass::Unavailable, NetworkError::Unreachable, "the connection could not start");
        }
        const ConnectionId kId{connection->id};
        ++core_->liveHandles;
        core_->connections.emplace(kId.value, std::move(connection));
        return kId;
    }

    result::Result<StreamId> openStream(ConnectionId connection, bool unidirectional) override {
        std::unique_lock lock{core_->mutex};
        RAWFRAME_TRY_ASSIGN(Connection * link, openConnection(connection));
        if (link->web != nullptr && !link->web->open()) {
            return refuse(result::ErrorClass::FailedPrecondition,
                          NetworkError::StaleConnection,
                          "a browser's connection has no session to open a stream in");
        }
        std::uint64_t& opened = unidirectional ? link->unidirectionalOpened : link->bidirectionalOpened;
        if (link->bidirectionalOpened + link->unidirectionalOpened >= core_->profile.maximumStreamsPerConnection) {
            return refuse(
                result::ErrorClass::ResourceExhausted, NetworkError::Exhausted, "the connection has no more streams");
        }
        // QUIC numbers a side's streams of one kind in the order they start:
        // the count, then one-way in the second bit and the opener in the low.
        const std::uint64_t kId = (opened << 2U) | (unidirectional ? 2U : 0U) | (link->connector ? 0U : 1U);
        auto stream = std::make_unique<Stream>(Stream{.core = core_.get(), .connection = link->id, .id = kId});
        if (QUIC_FAILED(core_->api->StreamOpen(link->handle,
                                               unidirectional ? QUIC_STREAM_OPEN_FLAG_UNIDIRECTIONAL
                                                              : QUIC_STREAM_OPEN_FLAG_NONE,
                                               &streamCallback,
                                               stream.get(),
                                               &stream->handle))) {
            return refuse(result::ErrorClass::Unavailable, NetworkError::StaleConnection, "MsQuic refused a stream");
        }
        const QUIC_STATUS kStarted = core_->api->StreamStart(
            stream->handle, QUIC_STREAM_START_FLAG_IMMEDIATE | QUIC_STREAM_START_FLAG_SHUTDOWN_ON_FAIL);
        if (QUIC_FAILED(kStarted)) {
            const HQUIC kHandle = stream->handle;
            lock.unlock();
            core_->api->StreamClose(kHandle);
            return refuse(
                result::ErrorClass::FailedPrecondition, NetworkError::StaleConnection, "the stream could not start");
        }
        ++opened;
        Stream* kept = stream.release();
        link->streams.emplace(kId, kept);
        // A browser's stream names the session it belongs to first.
        if (link->web != nullptr && !sendOwn(*core_, *link, kept->handle, link->web->streamPreface(unidirectional))) {
            return refuse(
                result::ErrorClass::FailedPrecondition, NetworkError::StaleConnection, "the stream could not start");
        }
        return StreamId{kId};
    }

    result::Status send(ConnectionId connection, StreamId stream, std::span<const std::byte> bytes) override {
        const std::lock_guard kLock{core_->mutex};
        RAWFRAME_TRY_ASSIGN(Connection * link, openConnection(connection));
        if (bytes.size() > core_->profile.maximumStreamSend) {
            return refuse(result::ErrorClass::InvalidArgument, NetworkError::TooLarge, "a stream send is too large");
        }
        const auto kStream = link->streams.find(stream.value);
        const bool kMine = stream.openedByConnector() == link->connector;
        if (kStream == link->streams.end() || (!kMine && stream.unidirectional())) {
            return refuse(result::ErrorClass::InvalidArgument,
                          NetworkError::WrongStream,
                          "the stream is not open, or is the peer's one-way stream");
        }
        if (link->sendingBytes + bytes.size() > core_->profile.maximumQueuedBytes) {
            // A reliable stream cannot drop bytes, so the connection ends.
            core_->shutDown(*link, CloseReason::QueueExhausted, kQueueExhaustedCode);
            return refuse(result::ErrorClass::ResourceExhausted,
                          NetworkError::Exhausted,
                          "too much is waiting to be sent; the connection was closed");
        }
        if (bytes.empty()) {
            return {};
        }
        auto* sending = copy(*link, bytes);
        if (QUIC_FAILED(
                core_->api->StreamSend(kStream->second->handle, &sending->buffer, 1, QUIC_SEND_FLAG_NONE, sending))) {
            releaseSending(*core_, sending);
            return refuse(
                result::ErrorClass::FailedPrecondition, NetworkError::StaleConnection, "the stream is closing");
        }
        core_->statistics.streamBytesSent += bytes.size();
        return {};
    }

    result::Status sendDatagram(ConnectionId connection, std::span<const std::byte> bytes) override {
        const std::lock_guard kLock{core_->mutex};
        RAWFRAME_TRY_ASSIGN(Connection * link, openConnection(connection));
        // A browser's datagram names its session first.
        std::vector<std::byte> named;
        if (link->web != nullptr) {
            named = link->web->datagramPrefix();
            named.insert(named.end(), bytes.begin(), bytes.end());
        }
        const std::size_t kOnPath = link->web != nullptr ? named.size() : bytes.size();
        if (bytes.size() > core_->profile.maximumDatagram ||
            (link->datagramsEnabled && kOnPath > link->datagramLimit)) {
            return refuse(result::ErrorClass::InvalidArgument, NetworkError::TooLarge, "a datagram is too large");
        }
        if (link->web != nullptr) {
            bytes = named;
        }
        ++core_->statistics.datagramsSent;
        // Unreliable: with no room on the path or in the budget it is lost.
        if (!link->datagramsEnabled || link->sendingBytes + bytes.size() > core_->profile.maximumQueuedBytes) {
            ++core_->statistics.datagramsDropped;
            return {};
        }
        auto* sending = copy(*link, bytes);
        if (QUIC_FAILED(core_->api->DatagramSend(link->handle, &sending->buffer, 1, QUIC_SEND_FLAG_NONE, sending))) {
            releaseSending(*core_, sending);
            ++core_->statistics.datagramsDropped;
        }
        return {};
    }

    void close(ConnectionId connection) noexcept override {
        const std::lock_guard kLock{core_->mutex};
        Connection* link = core_->find(connection.value);
        if (link != nullptr && link->open) {
            core_->shutDown(*link, CloseReason::Closed, kClosedCode);
        }
    }

    std::size_t poll(std::vector<Event>& into, std::size_t maximum) override {
        const std::lock_guard kLock{core_->mutex};
        std::size_t moved = 0;
        while (moved < maximum && !core_->inbound.empty()) {
            Event event = std::move(core_->inbound.front());
            core_->inbound.pop_front();
            if (Connection* link = core_->find(event.connection.value)) {
                link->queuedEvents -= 1;
                link->queuedBytes -= event.bytes.size();
                if (event.kind == EventKind::Closed) {
                    core_->connections.erase(event.connection.value);
                }
            }
            if (event.kind == EventKind::Datagram) {
                ++core_->statistics.datagramsDelivered;
            } else if (event.kind == EventKind::StreamBytes) {
                core_->statistics.streamBytesDelivered += event.bytes.size();
            }
            into.push_back(std::move(event));
            ++moved;
        }
        return moved;
    }

    network::ProviderStatistics statistics() const noexcept override {
        const std::lock_guard kLock{core_->mutex};
        return core_->statistics;
    }

private:
    result::Result<Connection*> openConnection(ConnectionId connection) {
        Connection* link = core_->find(connection.value);
        if (link == nullptr || !link->open || link->handle == nullptr) {
            return refuse(result::ErrorClass::FailedPrecondition,
                          NetworkError::StaleConnection,
                          "the connection is closed or not this provider's");
        }
        return link;
    }

    static Sending* copy(Connection& link, std::span<const std::byte> bytes) {
        auto* sending = new Sending{.connection = link.id};
        const auto* kFirst = reinterpret_cast<const std::uint8_t*>(bytes.data());
        sending->bytes.assign(kFirst, kFirst + bytes.size());
        sending->buffer.Length = static_cast<std::uint32_t>(sending->bytes.size());
        sending->buffer.Buffer = sending->bytes.data();
        link.sendingBytes += bytes.size();
        return sending;
    }

    std::unique_ptr<Core> core_;
};

} // namespace

QuicNetwork::QuicNetwork(std::unique_ptr<State> state) noexcept : state_(std::move(state)) {
}

QuicNetwork::~QuicNetwork() = default;

result::Result<std::unique_ptr<QuicNetwork>> QuicNetwork::create(QuicSettings settings) {
    constexpr std::int64_t kMaximumNanoseconds = std::int64_t{3'600} * 1'000'000'000;
    if (settings.idleTimeout.nanoseconds < 1'000'000 || settings.idleTimeout.nanoseconds > kMaximumNanoseconds ||
        settings.keepAlive.nanoseconds < 0 || settings.keepAlive.nanoseconds >= settings.idleTimeout.nanoseconds) {
        return refuse(result::ErrorClass::InvalidArgument,
                      NetworkError::InvalidProfile,
                      "the idle timeout is 1 ms to an hour, and the keep-alive shorter than it");
    }
    auto state = std::make_unique<State>();
    if (settings.certificate.has_value()) {
        RAWFRAME_TRY_ASSIGN(state->pkcs12, pkcs12Of(*settings.certificate));
    }
    state->settings = std::move(settings);
    if (QUIC_FAILED(MsQuicOpen2(&state->api))) {
        state->api = nullptr;
        return fail(result::ErrorClass::Unavailable, QuicError::Unavailable, "MsQuic could not be opened");
    }
    const QUIC_REGISTRATION_CONFIG kRegistration{"rawframe", QUIC_EXECUTION_PROFILE_LOW_LATENCY};
    if (QUIC_FAILED(state->api->RegistrationOpen(&kRegistration, &state->registration))) {
        state->registration = nullptr;
        return fail(result::ErrorClass::Unavailable, QuicError::Unavailable, "MsQuic refused a registration");
    }
    return std::unique_ptr<QuicNetwork>{new QuicNetwork{std::move(state)}};
}

result::Result<std::unique_ptr<network::Provider>> QuicNetwork::provider(const network::ProviderProfile& profile) {
    if (profile.maximumConnections == 0 || profile.maximumStreamsPerConnection == 0 || profile.maximumStreamSend == 0 ||
        profile.maximumDatagram == 0 || profile.maximumQueuedEvents == 0 || profile.maximumQueuedBytes == 0 ||
        profile.maximumStreamSend > std::numeric_limits<std::uint32_t>::max()) {
        return refuse(result::ErrorClass::InvalidArgument,
                      NetworkError::InvalidProfile,
                      "every provider bound is required and none may be zero");
    }
    auto core = std::make_unique<Core>();
    core->api = state_->api;
    core->registration = state_->registration;
    core->settings = &state_->settings;
    core->profile = profile;
    if (!state_->pkcs12.empty()) {
        QUIC_CERTIFICATE_PKCS12 bundle{reinterpret_cast<const std::uint8_t*>(state_->pkcs12.data()),
                                       static_cast<std::uint32_t>(state_->pkcs12.size()),
                                       nullptr};
        QUIC_CREDENTIAL_CONFIG credential{};
        credential.Type = QUIC_CREDENTIAL_TYPE_CERTIFICATE_PKCS12;
        credential.CertificatePkcs12 = &bundle;
        core->serverConfiguration = openConfiguration(
            *state_->api, state_->registration, state_->settings, profile, credential, state_->settings.webTransport);
        if (core->serverConfiguration == nullptr) {
            return fail(result::ErrorClass::Unavailable, QuicError::BadCertificate, "MsQuic refused the certificate");
        }
    }
    if (state_->settings.pin.has_value()) {
        // Validation is ours: the certificate must be the pinned one, and
        // nothing about who signed it matters (D27).
        QUIC_CREDENTIAL_CONFIG credential{};
        credential.Type = QUIC_CREDENTIAL_TYPE_NONE;
        credential.Flags = QUIC_CREDENTIAL_FLAG_CLIENT | QUIC_CREDENTIAL_FLAG_NO_CERTIFICATE_VALIDATION |
                           QUIC_CREDENTIAL_FLAG_INDICATE_CERTIFICATE_RECEIVED |
                           QUIC_CREDENTIAL_FLAG_USE_PORTABLE_CERTIFICATES;
        core->clientConfiguration =
            openConfiguration(*state_->api, state_->registration, state_->settings, profile, credential, false);
        if (core->clientConfiguration == nullptr) {
            if (core->serverConfiguration != nullptr) {
                state_->api->ConfigurationClose(core->serverConfiguration);
            }
            return fail(
                result::ErrorClass::Unavailable, QuicError::Unavailable, "MsQuic refused a client configuration");
        }
    }
    return std::unique_ptr<network::Provider>{new QuicProvider{std::move(core)}};
}

} // namespace rawframe::network_quic

#if defined(__has_feature)
#if __has_feature(thread_sanitizer)
/// MsQuic is built without the thread sanitizer, so the sanitizer sees only
/// the libc calls MsQuic makes. MsQuic hands a socket's teardown from one of
/// its threads to another through epoll, which the sanitizer cannot see as
/// synchronisation. That is the one report, and no stack of ours is in it.
extern "C" const char* __tsan_default_suppressions() {
    return "race:CxPlatSocketContextUninitializeComplete\n";
}
#endif
#endif
