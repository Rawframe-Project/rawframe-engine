#include "rawframe/world_tooling/server.h"

#include "rawframe/document/json.h"
#include "rawframe/world_tooling/errors.h"

#include <algorithm>
#include <map>
#include <optional>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

namespace rawframe::world_tooling {

namespace {

using document::Value;

/// Whether two texts are equal, in a time that depends on their lengths
/// only, so a token is not guessed a byte at a time.
bool sameSecret(std::string_view given, std::string_view expected) noexcept {
    unsigned difference = given.size() == expected.size() ? 0U : 1U;
    for (std::size_t at = 0; at < given.size(); ++at) {
        const char kExpected = expected.empty() ? '\0' : expected[at % expected.size()];
        difference |= static_cast<unsigned char>(given[at]) ^ static_cast<unsigned char>(kExpected);
    }
    return difference == 0;
}

std::string_view codeName(ToolingError error) noexcept {
    switch (error) {
    case ToolingError::Malformed:
        return "malformed";
    case ToolingError::Unauthenticated:
        return "unauthenticated";
    case ToolingError::NotGranted:
        return "not_granted";
    case ToolingError::Unsupported:
        return "unsupported";
    case ToolingError::LimitExceeded:
        return "limit_exceeded";
    case ToolingError::Configuration:
        return "configuration";
    }
    return "malformed";
}

/// One reply line, its line feed included.
std::string replyLine(const Value& id, std::string_view member, Value value) {
    Value made = Value::object();
    made.add("kind", Value::string("tooling.reply"));
    made.add("id", id);
    made.add(std::string{member}, std::move(value));
    return document::writeCompact(made) + "\n";
}

std::string errorLine(const Value& id, ToolingError error, std::string_view why) {
    Value record = Value::object();
    record.add("code", Value::string(std::string{codeName(error)}));
    record.add("message", Value::string(std::string{why}));
    return replyLine(id, "error", std::move(record));
}

struct Client {
    execution::MonotonicInstant since;
    std::optional<network::StreamId> stream;
    std::string pending;
    bool admitted = false;
    bool closing = false;
    /// When it began closing: it closes once its peer has every byte sent,
    /// or a second later, so a last reply is not cut off.
    std::optional<execution::MonotonicInstant> closingSince;
};

constexpr execution::MonotonicDuration kLastReplyWithin = execution::MonotonicDuration::fromSeconds(1);

} // namespace

struct ToolingServer::State {
    network::Provider* provider = nullptr;
    ToolingSettings settings;
    std::map<std::uint64_t, Client> clients;
    std::vector<network::Event> events;
    Statistics statistics;

    void send(network::ConnectionId connection, Client& client, std::string_view line) {
        if (!client.stream.has_value()) {
            return;
        }
        const std::span<const std::byte> kBytes{reinterpret_cast<const std::byte*>(line.data()), line.size()};
        if (!provider->send(connection, *client.stream, kBytes).has_value()) {
            client.closing = true;
        }
    }

    /// Answers with an error and closes the connection.
    void refuse(
        network::ConnectionId connection, Client& client, const Value& id, ToolingError error, std::string_view why) {
        send(connection, client, errorLine(id, error, why));
        client.closing = true;
        ++statistics.refused;
    }

    Value status(const world::World* world, world::TickIndex tick) const {
        Value made = Value::object();
        made.add("kind", Value::string("tooling.status"));
        made.add("tick", Value::integer(static_cast<std::int64_t>(tick.value)));
        made.add("world", Value::boolean(world != nullptr));
        made.add("entities", Value::integer(static_cast<std::int64_t>(world != nullptr ? world->entityCount() : 0)));
        Value components = Value::array();
        if (world != nullptr) {
            // Each component's holders, by name, in name order.
            std::map<std::string_view, std::size_t> held;
            for (const auto& archetype : world->archetypes()) {
                for (const schema::ComponentRuntimeId kComponent : archetype->components()) {
                    held[world->registry().descriptor(kComponent).name] += archetype->size();
                }
            }
            for (const auto& [name, count] : held) {
                Value each = Value::object();
                each.add("name", Value::string(std::string{name}));
                each.add("entities", Value::integer(static_cast<std::int64_t>(count)));
                components.push(std::move(each));
            }
        }
        made.add("components", std::move(components));
        return made;
    }

    /// One whole record from a client.
    void answer(network::ConnectionId connection,
                Client& client,
                std::string_view line,
                const world::World* world,
                world::TickIndex tick) {
        ++statistics.records;
        auto parsed = document::parse(line, document::ReadLimits{.maximumBytes = settings.maximumRecord});
        const Value kNone;
        if (!parsed.has_value() || parsed->kind() != Value::Kind::Object) {
            refuse(connection, client, kNone, ToolingError::Malformed, "a record is one strict JSON object a line");
            return;
        }
        const Value* kId = parsed->find("id");
        const Value& id = kId != nullptr ? *kId : kNone;
        const Value* kKind = parsed->find("kind");
        const std::string* kind = kKind != nullptr && kKind->kind() == Value::Kind::String ? kKind->text() : nullptr;
        if (!client.admitted) {
            const Value* kVersion = parsed->find("protocolVersion");
            const Value* kToken = parsed->find("token");
            if (kind == nullptr || *kind != "tooling.hello" || kToken == nullptr ||
                kToken->kind() != Value::Kind::String) {
                refuse(connection, client, id, ToolingError::Unauthenticated, "a connection begins with hello");
                return;
            }
            if (kVersion == nullptr || kVersion->integer() != kToolingProtocolVersion) {
                refuse(connection,
                       client,
                       id,
                       ToolingError::Unsupported,
                       "this endpoint speaks tooling protocol version 1 only");
                return;
            }
            if (!sameSecret(*kToken->text(), settings.token)) {
                refuse(connection, client, id, ToolingError::Unauthenticated, "the token is not this endpoint's");
                return;
            }
            client.admitted = true;
            ++statistics.admitted;
            Value grants = Value::array();
            if (settings.grants.inspect) {
                grants.push(Value::string("inspect"));
            }
            Value made = Value::object();
            made.add("kind", Value::string("tooling.welcome"));
            made.add("protocolVersion", Value::integer(kToolingProtocolVersion));
            made.add("grants", std::move(grants));
            send(connection, client, replyLine(id, "answer", std::move(made)));
            return;
        }
        if (kind != nullptr && *kind == "tooling.status") {
            if (!settings.grants.inspect) {
                send(connection, client, errorLine(id, ToolingError::NotGranted, "status needs the inspect grant"));
                return;
            }
            send(connection, client, replyLine(id, "answer", status(world, tick)));
            return;
        }
        if (kind != nullptr && *kind == "tooling.end") {
            Value made = Value::object();
            made.add("kind", Value::string("tooling.ended"));
            send(connection, client, replyLine(id, "answer", std::move(made)));
            client.closing = true;
            return;
        }
        send(connection, client, errorLine(id, ToolingError::Unsupported, "this endpoint has no such verb"));
    }

    void bytes(network::ConnectionId connection,
               Client& client,
               const network::Event& event,
               const world::World* world,
               world::TickIndex tick) {
        // One stream a client: the first it opens carries every record.
        if (!client.stream.has_value()) {
            if (!event.stream.openedByConnector() || event.stream.unidirectional()) {
                client.closing = true;
                ++statistics.refused;
                return;
            }
            client.stream = event.stream;
        } else if (!(*client.stream == event.stream)) {
            refuse(connection, client, Value{}, ToolingError::Malformed, "a client speaks on one stream");
            return;
        }
        client.pending.append(reinterpret_cast<const char*>(event.bytes.data()), event.bytes.size());
        std::size_t start = 0;
        for (std::size_t end = client.pending.find('\n'); end != std::string::npos && !client.closing;
             end = client.pending.find('\n', start)) {
            answer(connection, client, std::string_view{client.pending}.substr(start, end - start), world, tick);
            start = end + 1;
        }
        client.pending.erase(0, start);
        if (!client.closing && client.pending.size() > settings.maximumRecord) {
            refuse(
                connection, client, Value{}, ToolingError::LimitExceeded, "a record is at most the endpoint's limit");
        }
    }
};

network::ProviderProfile toolingProfile(std::size_t clients) noexcept {
    return network::ProviderProfile{.application = network::kToolingApplication,
                                    .maximumConnections = clients,
                                    .maximumStreamsPerConnection = 1,
                                    .maximumStreamSend = std::size_t{4} << 20U,
                                    .maximumDatagram = 1,
                                    .maximumQueuedEvents = 256,
                                    .maximumQueuedBytes = std::size_t{4} << 20U};
}

result::Result<std::unique_ptr<ToolingServer>> ToolingServer::create(network::Provider& provider,
                                                                     ToolingSettings settings) {
    if (settings.token.size() < 32) {
        return result::fail(result::ErrorClass::InvalidArgument,
                            kToolingDomain,
                            code(ToolingError::Configuration),
                            "a tooling token is at least 32 bytes");
    }
    auto state = std::make_unique<State>();
    state->provider = &provider;
    state->settings = std::move(settings);
    return std::unique_ptr<ToolingServer>{new ToolingServer{std::move(state)}};
}

ToolingServer::ToolingServer(std::unique_ptr<State> state) noexcept : state_(std::move(state)) {
}

ToolingServer::~ToolingServer() = default;

void ToolingServer::serve(const world::World* world, world::TickIndex tick, execution::MonotonicInstant now) {
    State& state = *state_;
    state.events.clear();
    state.provider->poll(state.events, 1024);
    for (const network::Event& event : state.events) {
        switch (event.kind) {
        case network::EventKind::Accepted:
            state.clients[event.connection.value] = Client{.since = now};
            ++state.statistics.accepted;
            break;
        case network::EventKind::StreamBytes: {
            const auto kFound = state.clients.find(event.connection.value);
            if (kFound != state.clients.end() && !kFound->second.closing) {
                state.bytes(event.connection, kFound->second, event, world, tick);
            }
            break;
        }
        case network::EventKind::Closed:
            state.clients.erase(event.connection.value);
            break;
        case network::EventKind::Connected:
        case network::EventKind::Datagram:
            break;
        }
    }
    for (auto at = state.clients.begin(); at != state.clients.end();) {
        Client& client = at->second;
        if (!client.admitted && !client.closing && now - client.since > state.settings.helloWithin) {
            state.refuse(network::ConnectionId{at->first},
                         client,
                         Value{},
                         ToolingError::Unauthenticated,
                         "no hello came in time");
        }
        if (client.closing && !client.closingSince.has_value()) {
            client.closingSince = now;
        }
        const bool kDelivered = !client.stream.has_value() ||
                                state.provider->pendingBytes(network::ConnectionId{at->first}, *client.stream) == 0;
        if (client.closing && (kDelivered || now - *client.closingSince > kLastReplyWithin)) {
            state.provider->close(network::ConnectionId{at->first});
            at = state.clients.erase(at);
        } else {
            ++at;
        }
    }
}

ToolingServer::Statistics ToolingServer::statistics() const noexcept {
    return state_->statistics;
}

} // namespace rawframe::world_tooling
