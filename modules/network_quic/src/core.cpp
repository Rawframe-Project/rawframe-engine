#include "core.h"

#include "rawframe/base/assert.h"

#include <mutex>
#include <string_view>
#include <utility>

namespace rawframe::network_quic {

using network::CloseReason;
using network::ConnectionId;
using network::Event;
using network::EventKind;
using network::StreamId;

namespace {

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

} // namespace

void releaseSending(Core& core, Sending* sending) noexcept {
    if (Connection* connection = core.find(sending->connection)) {
        connection->sendingBytes -= sending->bytes.size();
    }
    delete sending;
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

} // namespace rawframe::network_quic
