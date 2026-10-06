// The tooling protocol's runtime end (D408), over a provider that hands
// the server what a test gives it: a hello admits only the endpoint's
// token and version, status reads the World between ticks, and a client out
// of form, past the limit, or silent past the deadline is refused and
// closed after its last reply.

#include "rawframe/schema/registry.h"
#include "rawframe/test/test.h"
#include "rawframe/world_tooling/server.h"

#include <algorithm>
#include <map>
#include <memory>
#include <string>
#include <vector>

using namespace rawframe;
using namespace rawframe::world_tooling;

namespace {

struct Crate {
    static constexpr schema::ComponentTypeId kComponentTypeId =
        schema::ComponentTypeId::fromText("3b8e61d2-5f0a-4c97-8e24-d61a0f7b9c53");
    static constexpr std::string_view kComponentName = "test.crate";
    float x = 0;
};

/// A provider the test feeds: events it queues, and what the server sends
/// and closes, by connection.
class FedProvider final : public network::Provider {
public:
    result::Status listen(const network::Endpoint&) override {
        return {};
    }
    result::Result<network::ConnectionId> connect(const network::Endpoint&) override {
        return network::ConnectionId{};
    }
    result::Result<network::StreamId> openStream(network::ConnectionId, bool) override {
        return network::StreamId{};
    }
    result::Status
    send(network::ConnectionId connection, network::StreamId, std::span<const std::byte> bytes) override {
        sent[connection.value].append(reinterpret_cast<const char*>(bytes.data()), bytes.size());
        return {};
    }
    std::size_t pendingBytes(network::ConnectionId, network::StreamId) const noexcept override {
        return 0;
    }
    result::Status sendDatagram(network::ConnectionId, std::span<const std::byte>) override {
        return {};
    }
    void close(network::ConnectionId connection) noexcept override {
        closed.push_back(connection.value);
    }
    std::size_t poll(std::vector<network::Event>& into, std::size_t) override {
        const std::size_t kCount = queued.size();
        for (network::Event& event : queued) {
            into.push_back(std::move(event));
        }
        queued.clear();
        return kCount;
    }
    network::ProviderStatistics statistics() const noexcept override {
        return {};
    }

    void accept(std::uint64_t connection) {
        queued.push_back(network::Event{.kind = network::EventKind::Accepted, .connection = {connection}});
    }
    void say(std::uint64_t connection, std::string_view text, std::uint64_t stream = 0) {
        network::Event event{.kind = network::EventKind::StreamBytes, .connection = {connection}, .stream = {stream}};
        for (const char kByte : text) {
            event.bytes.push_back(static_cast<std::byte>(kByte));
        }
        queued.push_back(std::move(event));
    }

    std::vector<network::Event> queued;
    std::map<std::uint64_t, std::string> sent;
    std::vector<std::uint64_t> closed;
};

const std::string kToken(40, 't');

std::string hello(std::string_view token = kToken, int version = 1) {
    return R"({"kind":"tooling.hello","id":1,"protocolVersion":)" + std::to_string(version) + R"(,"token":")" +
           std::string{token} + "\"}\n";
}

bool closedOnce(const FedProvider& provider, std::uint64_t connection) {
    return std::ranges::count(provider.closed, connection) == 1;
}

} // namespace

RAWFRAME_TEST(AHelloAdmitsOnlyTheEndpointsTokenAndVersion) {
    FedProvider provider;
    RAWFRAME_EXPECT(!ToolingServer::create(provider, {.token = "short"}).has_value());
    auto made = ToolingServer::create(provider, {.token = kToken, .grants = {.inspect = true}});
    RAWFRAME_EXPECT(made.has_value());
    if (!made.has_value()) {
        return;
    }
    ToolingServer& server = **made;
    for (std::uint64_t connection = 1; connection <= 4; ++connection) {
        provider.accept(connection);
    }
    provider.say(1, hello());
    provider.say(2, hello(std::string(40, 'u')));
    provider.say(3, hello(kToken, 2));
    provider.say(4,
                 R"({"kind":"tooling.status","id":9})"
                 "\n");
    server.serve(nullptr, {}, {});
    RAWFRAME_EXPECT(provider.sent[1] == R"({"kind":"tooling.reply","id":1,"answer":{"kind":"tooling.welcome",)"
                                        R"("protocolVersion":1,"grants":["inspect"]}})"
                                        "\n");
    RAWFRAME_EXPECT(provider.sent[2].find(R"("code":"unauthenticated")") != std::string::npos);
    RAWFRAME_EXPECT(provider.sent[3].find(R"("code":"unsupported")") != std::string::npos);
    RAWFRAME_EXPECT(provider.sent[4].find(R"("id":9,"error":{"code":"unauthenticated")") != std::string::npos);
    RAWFRAME_EXPECT(!closedOnce(provider, 1) && closedOnce(provider, 2) && closedOnce(provider, 3) &&
                    closedOnce(provider, 4));
    const ToolingServer::Statistics kCounted = server.statistics();
    RAWFRAME_EXPECT(kCounted.accepted == 4 && kCounted.admitted == 1 && kCounted.refused == 3);
}

RAWFRAME_TEST(StatusReadsTheWorldBetweenTicks) {
    schema::RegistryBuilder builder;
    builder.add<Crate>();
    const auto kRegistry = builder.freeze();
    RAWFRAME_EXPECT(kRegistry.has_value());
    if (!kRegistry.has_value()) {
        return;
    }
    world::World world{*kRegistry};
    const auto kKey = (*kRegistry)->key<Crate>();
    for (int made = 0; made < 3; ++made) {
        const auto kEntity = world.create();
        RAWFRAME_EXPECT(kEntity.has_value() && kKey.has_value());
        if (made < 2 && kEntity.has_value() && kKey.has_value()) {
            RAWFRAME_EXPECT(world.insert(*kEntity, *kKey, Crate{.x = 1}).has_value());
        }
    }
    FedProvider provider;
    auto server = *ToolingServer::create(provider, {.token = kToken, .grants = {.inspect = true}});
    provider.accept(7);
    // Records split across arrivals are read whole.
    provider.say(7, hello() + R"({"kind":"tooling.sta)");
    server->serve(&world, world::TickIndex{120}, {});
    provider.say(7,
                 R"(tus","id":"s"})"
                 "\n"
                 R"({"kind":"tooling.jump","id":3})"
                 "\n"
                 R"({"kind":"tooling.end","id":4})"
                 "\n");
    server->serve(&world, world::TickIndex{121}, {});
    const std::string& kSent = provider.sent[7];
    RAWFRAME_EXPECT(kSent.find(R"({"kind":"tooling.reply","id":"s","answer":{"kind":"tooling.status","tick":121,)"
                               R"("world":true,"entities":3,"components":[{"name":"test.crate","entities":2}]}})") !=
                    std::string::npos);
    RAWFRAME_EXPECT(kSent.find(R"("id":3,"error":{"code":"unsupported")") != std::string::npos);
    RAWFRAME_EXPECT(kSent.ends_with(R"({"kind":"tooling.reply","id":4,"answer":{"kind":"tooling.ended"}})"
                                    "\n"));
    RAWFRAME_EXPECT(closedOnce(provider, 7));
}

RAWFRAME_TEST(AClientOutOfFormPastTheLimitOrSilentIsRefused) {
    FedProvider provider;
    auto server = *ToolingServer::create(provider,
                                         {.token = kToken,
                                          .grants = {.inspect = true},
                                          .maximumRecord = 256,
                                          .helloWithin = execution::MonotonicDuration::fromSeconds(5)});
    for (std::uint64_t connection = 1; connection <= 4; ++connection) {
        provider.accept(connection);
    }
    provider.say(1, "not json\n");
    provider.say(2, std::string(300, 'x'));
    // A second stream, or one the server would open, is not a client's.
    provider.say(3, hello());
    provider.say(3,
                 R"({"kind":"tooling.status"})"
                 "\n",
                 4);
    server->serve(nullptr, {}, execution::MonotonicInstant{});
    RAWFRAME_EXPECT(provider.sent[1].find(R"("code":"malformed")") != std::string::npos);
    RAWFRAME_EXPECT(provider.sent[2].find(R"("code":"limit_exceeded")") != std::string::npos);
    RAWFRAME_EXPECT(provider.sent[3].find(R"("code":"malformed")") != std::string::npos);
    RAWFRAME_EXPECT(closedOnce(provider, 1) && closedOnce(provider, 2) && closedOnce(provider, 3));
    // Silent past the hello's deadline.
    server->serve(nullptr, {}, execution::MonotonicInstant{} + execution::MonotonicDuration::fromSeconds(4));
    RAWFRAME_EXPECT(!closedOnce(provider, 4));
    server->serve(nullptr, {}, execution::MonotonicInstant{} + execution::MonotonicDuration::fromSeconds(6));
    // It never opened its stream, so it is closed with nothing said.
    RAWFRAME_EXPECT(closedOnce(provider, 4) && provider.sent[4].empty());
    RAWFRAME_EXPECT(server->statistics().refused == 4);
}

RAWFRAME_TEST(AVerbBeyondTheGrantsIsRefusedAndTheClientKept) {
    FedProvider provider;
    auto server = *ToolingServer::create(provider, {.token = kToken});
    provider.accept(1);
    provider.say(1,
                 hello() + R"({"kind":"tooling.status","id":2})"
                           "\n");
    server->serve(nullptr, {}, {});
    RAWFRAME_EXPECT(provider.sent[1].find(R"("grants":[])") != std::string::npos);
    RAWFRAME_EXPECT(provider.sent[1].find(R"("id":2,"error":{"code":"not_granted")") != std::string::npos);
    RAWFRAME_EXPECT(provider.closed.empty());
}
