#pragma once

// The MsQuic side of the QUIC provider: one provider's state, which MsQuic's
// threads reach through callback contexts, and the callbacks themselves.
// Split from the provider's own calls (quic.cpp) along that seam.

#include "rawframe/base/threads.h"
#include "rawframe/network/provider.h"
#include "rawframe/network_quic/quic.h"
#include "webtransport.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <map>
#include <memory>
// The global execution configuration (D214) is a preview feature of the
// MsQuic this engine pins; the library takes it whatever the header shows.
#define QUIC_API_ENABLE_PREVIEW_FEATURES 1
#include <msquic.h>
#include <span>
#include <vector>

namespace rawframe::network_quic {

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
    network::CloseReason reason = network::CloseReason::Closed;
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

    void noteReason(network::CloseReason why) noexcept {
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
    std::deque<network::Event> inbound;
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

    void deliver(Connection& connection, network::Event event) {
        connection.queuedEvents += 1;
        connection.queuedBytes += event.bytes.size();
        event.connection = network::ConnectionId{connection.id};
        inbound.push_back(std::move(event));
    }

    /// Starts ending a connection: the peer hears `code`, the owner hears
    /// `why` once MsQuic has finished with it.
    void shutDown(Connection& connection, network::CloseReason why, QUIC_UINT62 code) noexcept {
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

/// Releases bytes MsQuic has finished sending.
void releaseSending(Core& core, Sending* sending) noexcept;

/// Hands bytes this side sends of its own (HTTP/3's, not the owner's) to
/// MsQuic on `stream`. False if MsQuic refused them.
bool sendOwn(Core& core, Connection& connection, HQUIC stream, std::span<const std::byte> bytes);

QUIC_STATUS streamCallback(HQUIC handle, void* context, QUIC_STREAM_EVENT* event) noexcept;
QUIC_STATUS connectionCallback(HQUIC handle, void* context, QUIC_CONNECTION_EVENT* event) noexcept;
QUIC_STATUS listenerCallback(HQUIC handle, void* context, QUIC_LISTENER_EVENT* event) noexcept;

/// Rawframe's ALPN, and HTTP/3's after it where a server accepts browsers.
std::array<QUIC_BUFFER, 2> alpnsFor(bool browsers) noexcept;

/// A configuration: ALPN, the settings a profile implies, and credentials.
HQUIC openConfiguration(const QUIC_API_TABLE& api,
                        HQUIC registration,
                        const QuicSettings& settings,
                        const network::ProviderProfile& profile,
                        const QUIC_CREDENTIAL_CONFIG& credential,
                        bool browsers);

} // namespace rawframe::network_quic
