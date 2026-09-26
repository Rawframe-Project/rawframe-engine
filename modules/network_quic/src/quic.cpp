#include "rawframe/network_quic/quic.h"

#include "core.h"
#include "rawframe/base/assert.h"
#include "rawframe/base/threads.h"
#include "rawframe/execution/parallelism.h"
#include "rawframe/network/errors.h"
#include "rawframe/network_quic/errors.h"
#include "tls.h"
#include "webtransport.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <cstring>
#include <deque>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

#if defined(__linux__)
#include <sched.h>
#endif

namespace rawframe::network_quic {

using network::CloseReason;
using network::ConnectionId;
using network::Event;
using network::EventKind;
using network::NetworkError;
using network::StreamId;

namespace {

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

namespace {

/// Keeps MsQuic's threads to `processors` of those this process may run on
/// (D214). MsQuic takes this only before its library is in use, so a process
/// with a network already open keeps the first one's choice.
void spreadAcross(const QUIC_API_TABLE& api, std::uint32_t processors) noexcept {
    const std::size_t kWanted = processors != 0 ? processors : execution::effectiveParallelism();
    std::vector<std::uint16_t> allowed;
#if defined(__linux__)
    cpu_set_t mask;
    CPU_ZERO(&mask);
    if (sched_getaffinity(0, sizeof mask, &mask) == 0) {
        for (int cpu = 0; cpu < CPU_SETSIZE && allowed.size() < kWanted; ++cpu) {
            if (CPU_ISSET(cpu, &mask)) {
                allowed.push_back(static_cast<std::uint16_t>(cpu));
            }
        }
    }
#endif
    for (std::uint16_t cpu = 0; allowed.empty() && cpu < kWanted; ++cpu) {
        allowed.push_back(cpu);
    }
    // The configuration ends in a list of processors, one given in place.
    std::vector<std::byte> config(QUIC_GLOBAL_EXECUTION_CONFIG_MIN_SIZE + (allowed.size() * sizeof(std::uint16_t)));
    QUIC_GLOBAL_EXECUTION_CONFIG header{};
    header.ProcessorCount = static_cast<std::uint32_t>(allowed.size());
    std::memcpy(config.data(), &header, QUIC_GLOBAL_EXECUTION_CONFIG_MIN_SIZE);
    std::memcpy(
        config.data() + QUIC_GLOBAL_EXECUTION_CONFIG_MIN_SIZE, allowed.data(), allowed.size() * sizeof(std::uint16_t));
    static_cast<void>(api.SetParam(
        nullptr, QUIC_PARAM_GLOBAL_EXECUTION_CONFIG, static_cast<std::uint32_t>(config.size()), config.data()));
}

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
    spreadAcross(*state->api, state->settings.processors);
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
