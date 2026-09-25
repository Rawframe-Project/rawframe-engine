#include "rawframe/network_web/web.h"

#include "rawframe/network/errors.h"

#include <algorithm>
#include <cstring>
#include <limits>
#include <unordered_map>

namespace rawframe::network_web {

namespace {

// The page's side of the transport (web.h). Sizes cross as u32: WebAssembly
// here is 32-bit, and nothing the profile allows is larger.
extern "C" {
__attribute__((import_module("rawframe_web_transport"), import_name("open"))) std::uint32_t
pageOpen(std::uint32_t maximumQueuedEvents, std::uint32_t maximumQueuedBytes);
__attribute__((import_module("rawframe_web_transport"), import_name("listen"))) std::uint32_t
pageListen(std::uint32_t provider, const char* name, std::uint32_t length);
__attribute__((import_module("rawframe_web_transport"), import_name("connect"))) std::uint32_t
pageConnect(std::uint32_t provider, const char* name, std::uint32_t length);
__attribute__((import_module("rawframe_web_transport"), import_name("open_stream"))) std::uint64_t
pageOpenStream(std::uint32_t provider, std::uint32_t connection, std::uint32_t unidirectional);
__attribute__((import_module("rawframe_web_transport"), import_name("send"))) std::uint32_t
pageSend(std::uint32_t provider,
         std::uint32_t connection,
         std::uint64_t stream,
         const std::byte* bytes,
         std::uint32_t length);
__attribute__((import_module("rawframe_web_transport"), import_name("send_datagram"))) std::uint32_t
pageSendDatagram(std::uint32_t provider, std::uint32_t connection, const std::byte* bytes, std::uint32_t length);
__attribute__((import_module("rawframe_web_transport"), import_name("close"))) void
pageClose(std::uint32_t provider, std::uint32_t connection, std::uint32_t reason);
__attribute__((import_module("rawframe_web_transport"), import_name("poll"))) std::int32_t
pagePoll(std::uint32_t provider, std::byte* into, std::uint32_t capacity);
__attribute__((import_module("rawframe_web_transport"), import_name("release"))) void
pageRelease(std::uint32_t provider);
}

using network::NetworkError;

std::unexpected<result::Error> refuse(result::ErrorClass errorClass, NetworkError error, std::string_view why) {
    return result::fail(errorClass, network::kNetworkDomain, network::code(error), why);
}

/// A refusal for the code the page answered with.
std::unexpected<result::Error> refusedByPage(std::uint32_t answer) {
    switch (static_cast<NetworkError>(answer)) {
    case NetworkError::Unreachable:
        return refuse(result::ErrorClass::Unavailable, NetworkError::Unreachable, "the page reaches nothing there");
    case NetworkError::Exhausted:
        return refuse(result::ErrorClass::ResourceExhausted, NetworkError::Exhausted, "the page's transport is full");
    case NetworkError::WrongStream:
        return refuse(result::ErrorClass::InvalidArgument, NetworkError::WrongStream, "not a stream to send on");
    default:
        return refuse(
            result::ErrorClass::FailedPrecondition, NetworkError::StaleConnection, "the page's connection is gone");
    }
}

std::uint64_t readU64(const std::byte* from) noexcept {
    std::uint64_t value = 0;
    std::memcpy(&value, from, sizeof value);
    return value;
}

std::uint32_t readU32(const std::byte* from) noexcept {
    std::uint32_t value = 0;
    std::memcpy(&value, from, sizeof value);
    return value;
}

class WebProvider final : public network::Provider {
public:
    WebProvider(std::uint32_t page, const network::ProviderProfile& profile)
        : page_(page), profile_(profile),
          buffer_(kEventHeaderBytes +
                  std::max({profile.maximumQueuedBytes, profile.maximumDatagram, profile.maximumStreamSend})) {
    }
    ~WebProvider() override {
        pageRelease(page_);
    }
    WebProvider(const WebProvider&) = delete;
    WebProvider& operator=(const WebProvider&) = delete;

    result::Status listen(const network::Endpoint& endpoint) override {
        const std::uint32_t kAnswer =
            pageListen(page_, endpoint.name.data(), static_cast<std::uint32_t>(endpoint.name.size()));
        if (kAnswer != 0) {
            return refusedByPage(kAnswer);
        }
        return {};
    }

    result::Result<network::ConnectionId> connect(const network::Endpoint& endpoint) override {
        if (connections_.size() >= profile_.maximumConnections) {
            return refuse(
                result::ErrorClass::ResourceExhausted, NetworkError::Exhausted, "this provider has no room to connect");
        }
        const std::uint32_t kConnection =
            pageConnect(page_, endpoint.name.data(), static_cast<std::uint32_t>(endpoint.name.size()));
        if (kConnection == 0) {
            return refuse(result::ErrorClass::Unavailable, NetworkError::Unreachable, "the page reaches nothing there");
        }
        connections_[kConnection] = Connection{.connector = true};
        return network::ConnectionId{kConnection};
    }

    result::Result<network::StreamId> openStream(network::ConnectionId connection, bool unidirectional) override {
        Connection* known = find(connection);
        if (known == nullptr) {
            return stale();
        }
        if (known->opened >= profile_.maximumStreamsPerConnection) {
            return refuse(result::ErrorClass::ResourceExhausted,
                          NetworkError::Exhausted,
                          "this connection has opened all the streams it may");
        }
        const std::uint64_t kStream =
            pageOpenStream(page_, static_cast<std::uint32_t>(connection.value), unidirectional ? 1U : 0U);
        if (kStream == 0) {
            return stale();
        }
        ++known->opened;
        return network::StreamId{kStream - 1};
    }

    result::Status
    send(network::ConnectionId connection, network::StreamId stream, std::span<const std::byte> bytes) override {
        const Connection* known = find(connection);
        if (known == nullptr) {
            return stale();
        }
        if (bytes.size() > profile_.maximumStreamSend) {
            return refuse(result::ErrorClass::InvalidArgument, NetworkError::TooLarge, "a stream send over the bound");
        }
        // A one-way stream the peer opened is only the peer's to send on.
        if (stream.unidirectional() && stream.openedByConnector() != known->connector) {
            return refuse(result::ErrorClass::InvalidArgument, NetworkError::WrongStream, "not a stream to send on");
        }
        const std::uint32_t kAnswer = pageSend(page_,
                                               static_cast<std::uint32_t>(connection.value),
                                               stream.value,
                                               bytes.data(),
                                               static_cast<std::uint32_t>(bytes.size()));
        if (kAnswer != 0) {
            return refusedByPage(kAnswer);
        }
        statistics_.streamBytesSent += bytes.size();
        return {};
    }

    result::Status sendDatagram(network::ConnectionId connection, std::span<const std::byte> bytes) override {
        if (find(connection) == nullptr) {
            return stale();
        }
        if (bytes.size() > profile_.maximumDatagram) {
            return refuse(result::ErrorClass::InvalidArgument, NetworkError::TooLarge, "a datagram over the bound");
        }
        const std::uint32_t kAnswer = pageSendDatagram(page_,
                                                       static_cast<std::uint32_t>(connection.value),
                                                       bytes.data(),
                                                       static_cast<std::uint32_t>(bytes.size()));
        if (kAnswer != 0) {
            return refusedByPage(kAnswer);
        }
        ++statistics_.datagramsSent;
        return {};
    }

    void close(network::ConnectionId connection) noexcept override {
        if (find(connection) != nullptr) {
            pageClose(page_,
                      static_cast<std::uint32_t>(connection.value),
                      static_cast<std::uint32_t>(network::CloseReason::Closed));
        }
    }

    std::size_t poll(std::vector<network::Event>& into, std::size_t maximum) override {
        std::size_t moved = 0;
        while (moved < maximum) {
            const std::int32_t kSize = pagePoll(page_, buffer_.data(), static_cast<std::uint32_t>(buffer_.size()));
            // Nothing waits, or the page broke the bounds it was given: an
            // event larger than any it may hold stays with the page.
            if (kSize <= 0) {
                break;
            }
            if (auto event = parse(std::span{buffer_.data(), static_cast<std::size_t>(kSize)})) {
                into.push_back(std::move(*event));
                ++moved;
            }
        }
        return moved;
    }

    network::ProviderStatistics statistics() const noexcept override {
        return statistics_;
    }

private:
    struct Connection {
        /// Whether this side connected, which says whose streams are whose.
        bool connector = false;
        std::size_t opened = 0;
    };

    Connection* find(network::ConnectionId connection) noexcept {
        if (connection.value > std::numeric_limits<std::uint32_t>::max()) {
            return nullptr;
        }
        const auto kFound = connections_.find(static_cast<std::uint32_t>(connection.value));
        return kFound == connections_.end() ? nullptr : &kFound->second;
    }

    static std::unexpected<result::Error> stale() {
        return refuse(result::ErrorClass::FailedPrecondition,
                      NetworkError::StaleConnection,
                      "the connection is closed or was never this provider's");
    }

    /// One event as the page wrote it, or nothing for one that is not
    /// well formed, names a connection this side does not have, or is over
    /// a bound.
    std::optional<network::Event> parse(std::span<const std::byte> written) {
        if (written.size() < kEventHeaderBytes) {
            return std::nullopt;
        }
        const auto kKind = std::to_integer<std::uint8_t>(written[0]);
        const auto kReason = std::to_integer<std::uint8_t>(written[1]);
        const std::uint32_t kConnection = readU32(written.data() + 4);
        const std::uint64_t kStream = readU64(written.data() + 8);
        const std::uint32_t kLength = readU32(written.data() + 16);
        if (kKind > static_cast<std::uint8_t>(network::EventKind::Closed) ||
            kReason > static_cast<std::uint8_t>(network::CloseReason::Untrusted) || kConnection == 0 ||
            kLength != written.size() - kEventHeaderBytes) {
            return std::nullopt;
        }
        const auto kEventKind = static_cast<network::EventKind>(kKind);
        network::Event event{.kind = kEventKind,
                             .connection = network::ConnectionId{kConnection},
                             .stream = network::StreamId{kStream},
                             .bytes = {written.begin() + kEventHeaderBytes, written.end()},
                             .reason = static_cast<network::CloseReason>(kReason)};
        if (kEventKind == network::EventKind::Accepted) {
            // A peer beyond this side's room is turned away.
            if (connections_.size() >= profile_.maximumConnections || connections_.contains(kConnection)) {
                pageClose(page_, kConnection, static_cast<std::uint32_t>(network::CloseReason::Refused));
                return std::nullopt;
            }
            connections_[kConnection] = Connection{.connector = false};
            return event;
        }
        if (!connections_.contains(kConnection)) {
            return std::nullopt;
        }
        switch (kEventKind) {
        case network::EventKind::Datagram:
            if (event.bytes.size() > profile_.maximumDatagram) {
                ++statistics_.datagramsDropped;
                return std::nullopt;
            }
            ++statistics_.datagramsDelivered;
            break;
        case network::EventKind::StreamBytes:
            statistics_.streamBytesDelivered += event.bytes.size();
            break;
        case network::EventKind::Closed:
            connections_.erase(kConnection);
            break;
        default:
            break;
        }
        return event;
    }

    std::uint32_t page_;
    network::ProviderProfile profile_;
    std::vector<std::byte> buffer_;
    std::unordered_map<std::uint32_t, Connection> connections_;
    network::ProviderStatistics statistics_;
};

} // namespace

result::Result<std::unique_ptr<network::Provider>> webProvider(const network::ProviderProfile& profile) {
    constexpr std::size_t kLargest = std::numeric_limits<std::uint32_t>::max() / 2;
    if (profile.maximumConnections == 0 || profile.maximumStreamsPerConnection == 0 || profile.maximumStreamSend == 0 ||
        profile.maximumDatagram == 0 || profile.maximumQueuedEvents == 0 || profile.maximumQueuedBytes == 0 ||
        profile.maximumQueuedBytes > kLargest || profile.maximumStreamSend > kLargest ||
        profile.maximumQueuedEvents > kLargest) {
        return refuse(
            result::ErrorClass::InvalidArgument, NetworkError::InvalidProfile, "a profile bound is zero or too large");
    }
    const std::uint32_t kPage = pageOpen(static_cast<std::uint32_t>(profile.maximumQueuedEvents),
                                         static_cast<std::uint32_t>(profile.maximumQueuedBytes));
    if (kPage == 0) {
        return refuse(result::ErrorClass::Unavailable, NetworkError::Unreachable, "the page gave no transport");
    }
    return std::unique_ptr<network::Provider>{new WebProvider{kPage, profile}};
}

} // namespace rawframe::network_web
