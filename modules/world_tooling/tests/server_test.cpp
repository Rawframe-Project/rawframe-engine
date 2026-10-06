// The tooling protocol's runtime end (D408), over a provider that hands
// the server what a test gives it: a hello admits only the endpoint's
// token and version, status reads the World between ticks, and a client out
// of form, past the limit, or silent past the deadline is refused and
// closed after its last reply.

#include "rawframe/schema/registry.h"
#include "rawframe/test/test.h"
#include "rawframe/world_tooling/server.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <map>
#include <memory>
#include <optional>
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

/// Past the second a refused client has to read why.
constexpr execution::MonotonicInstant kLater{execution::MonotonicDuration::fromMilliseconds(1500).nanoseconds};

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
    // Refused, each is closed once its client has had a second to read why.
    RAWFRAME_EXPECT(provider.closed.empty());
    server.serve(nullptr, {}, kLater);
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
    server->serve(&world, world::TickIndex{122}, kLater);
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
    server->serve(nullptr, {}, kLater);
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

namespace {

/// A preview that keeps what it is told, refusing an eye at its target.
class KeptPreview final : public Previewer {
public:
    bool look(const std::optional<Look>& look) override {
        if (look.has_value() && look->eye == look->target) {
            return false;
        }
        kept = look;
        return true;
    }
    std::optional<Look> kept;
};

} // namespace

RAWFRAME_TEST(LookingMovesThePreviewUnderTheViewGrant) {
    FedProvider provider;
    KeptPreview preview;
    auto server = *ToolingServer::create(provider, {.token = kToken, .grants = {.view = true}, .previewer = &preview});
    provider.accept(1);
    provider.say(1,
                 hello() +
                     R"({"kind":"tooling.look","id":2,"view":{"eye":[0,10,10],"target":[0,0,0],"fieldOfView":90}})"
                     "\n"
                     R"({"kind":"tooling.look","id":3,"view":{"eye":[1,1,1],"target":[1,1,1],"fieldOfView":60}})"
                     "\n"
                     R"({"kind":"tooling.look","id":4,"view":{"eye":[0,1,0],"target":[0,0,0]}})"
                     "\n"
                     R"({"kind":"tooling.status","id":5})"
                     "\n");
    server->serve(nullptr, {}, {});
    RAWFRAME_EXPECT(provider.sent[1].find(R"("grants":["view"])") != std::string::npos);
    RAWFRAME_EXPECT(provider.sent[1].find(R"("id":2,"answer":{"kind":"tooling.looking","previewing":true})") !=
                    std::string::npos);
    RAWFRAME_EXPECT(preview.kept.has_value() && preview.kept->eye == (std::array<double, 3>{0, 10, 10}) &&
                    preview.kept->fieldOfView == 90);
    // An eye at its target, a view out of form, and a verb past the grant
    // refused; the camera kept.
    RAWFRAME_EXPECT(provider.sent[1].find(R"("id":3,"error":{"code":"malformed")") != std::string::npos);
    RAWFRAME_EXPECT(provider.sent[1].find(R"("id":4,"error":{"code":"malformed")") != std::string::npos);
    RAWFRAME_EXPECT(provider.sent[1].find(R"("id":5,"error":{"code":"not_granted")") != std::string::npos);
    RAWFRAME_EXPECT(preview.kept.has_value() && preview.kept->fieldOfView == 90);
    // A null view gives the player's camera back.
    provider.say(1,
                 R"({"kind":"tooling.look","id":6,"view":null})"
                 "\n");
    server->serve(nullptr, {}, {});
    RAWFRAME_EXPECT(provider.sent[1].find(R"("id":6,"answer":{"kind":"tooling.looking","previewing":false})") !=
                        std::string::npos &&
                    !preview.kept.has_value());
    // Granted but with no preview here: not found.
    FedProvider other;
    auto bare = *ToolingServer::create(other, {.token = kToken, .grants = {.view = true}});
    other.accept(1);
    other.say(1,
              hello() + R"({"kind":"tooling.look","id":2,"view":null})"
                        "\n");
    bare->serve(nullptr, {}, {});
    RAWFRAME_EXPECT(other.sent[1].find(R"("id":2,"error":{"code":"not_found")") != std::string::npos);
}

namespace {

struct Door {
    static constexpr schema::ComponentTypeId kComponentTypeId =
        schema::ComponentTypeId::fromText("8f2c0d41-77a3-4b5e-9c10-3e6b2a9d4f71");
    static constexpr std::string_view kComponentName = "test.door";
    world::EntityHandle opens;
    std::uint32_t state = 0;
    std::uint64_t serial = 0;
    float width = 0;
    std::uint8_t locked = 0;
};

/// The door's fields as a game's program would give them.
class DoorFields final : public world_runtime::ComponentFields {
public:
    DoorFields() {
        using K = world_runtime::ComponentFieldKind;
        sets_.push_back(
            {.id = Door::kComponentTypeId,
             .size = sizeof(Door),
             .fields = {{.name = "opens", .offset = offsetof(Door, opens), .kind = K::Entity},
                        {.name = "state", .offset = offsetof(Door, state), .kind = K::Case, .cases = {"shut", "open"}},
                        {.name = "serial", .offset = offsetof(Door, serial), .kind = K::U64},
                        {.name = "width", .offset = offsetof(Door, width), .kind = K::F32},
                        {.name = "locked", .offset = offsetof(Door, locked), .kind = K::Bool}}});
    }
    std::span<const world_runtime::ComponentFieldSet> fieldSets() const noexcept override {
        return sets_;
    }

private:
    std::vector<world_runtime::ComponentFieldSet> sets_;
};

/// The answer of the reply to the record of `id` in what was sent.
std::string replyTo(const std::string& sent, std::string_view id) {
    const std::string kNeedle = "\"id\":" + std::string{id} + ",";
    const std::size_t kAt = sent.find(kNeedle);
    if (kAt == std::string::npos) {
        return {};
    }
    const std::size_t kStart = sent.rfind('\n', kAt);
    const std::size_t kEnd = sent.find('\n', kAt);
    return sent.substr(kStart == std::string::npos ? 0 : kStart + 1,
                       kEnd - (kStart == std::string::npos ? 0 : kStart + 1));
}

} // namespace

RAWFRAME_TEST(EntitiesAreListedInPagesAndReadByTheirFields) {
    schema::RegistryBuilder builder;
    builder.add<Crate>();
    builder.add<Door>();
    const auto kRegistry = builder.freeze();
    RAWFRAME_EXPECT(kRegistry.has_value());
    if (!kRegistry.has_value()) {
        return;
    }
    world::World world{*kRegistry};
    const auto kCrate = *(*kRegistry)->key<Crate>();
    const auto kDoor = *(*kRegistry)->key<Door>();
    std::vector<world::EntityHandle> made;
    for (int at = 0; at < 5; ++at) {
        made.push_back(*world.create());
    }
    RAWFRAME_EXPECT(world.insert(made[1], kCrate, Crate{.x = 1}).has_value());
    RAWFRAME_EXPECT(world.insert(made[3], kCrate, Crate{.x = 2}).has_value());
    RAWFRAME_EXPECT(
        world
            .insert(
                made[3],
                kDoor,
                Door{.opens = made[1], .state = 1, .serial = (std::uint64_t{1} << 60U) + 1, .width = 1.5F, .locked = 1})
            .has_value());
    const DoorFields kFields;
    FedProvider provider;
    auto server = *ToolingServer::create(provider, {.token = kToken, .grants = {.inspect = true}, .fields = &kFields});
    provider.accept(1);
    const std::string kThird = std::to_string(made[3].slot) + ":" + std::to_string(made[3].generation);
    const std::string kSecond = std::to_string(made[1].slot) + ":" + std::to_string(made[1].generation);
    provider.say(1,
                 hello() + R"({"kind":"tooling.entities","id":2,"limit":2})" + "\n" +
                     R"({"kind":"tooling.entities","id":3,"after":")" + kSecond + R"(","component":"test.crate"})" +
                     "\n" + R"({"kind":"tooling.read_entity","id":4,"entity":")" + kThird + "\"}\n" +
                     R"({"kind":"tooling.read_entity","id":5,"entity":"99:1"})" + "\n" +
                     R"({"kind":"tooling.entities","id":6,"limit":0})" + "\n" +
                     R"({"kind":"tooling.entities","id":7,"component":"test.window"})" + "\n");
    server->serve(&world, {}, {});
    const std::string& kSent = provider.sent[1];
    // Slot order, two a page, more to come.
    const std::string kFirstPage = replyTo(kSent, "2");
    RAWFRAME_EXPECT(kFirstPage.find(R"("entities":[{"entity":")" + std::to_string(made[0].slot) + ":") !=
                    std::string::npos);
    RAWFRAME_EXPECT(kFirstPage.find(R"("more":true)") != std::string::npos);
    // After the second, holding a crate: the fourth alone, and no more.
    RAWFRAME_EXPECT(replyTo(kSent, "3")
                        .find(R"("entities":[{"entity":")" + kThird +
                              R"(","components":["test.crate","test.door"]}],"more":false)") != std::string::npos);
    // Fields by name: an entity by its name, an enum by its case, a wide
    // integer as text, a real, a truth; a component not known by name only.
    RAWFRAME_EXPECT(replyTo(kSent, "4")
                        .find(R"({"name":"test.crate"},{"name":"test.door","fields":{"opens":")" + kSecond +
                              R"(","state":"open","serial":"1152921504606846977","width":1.5,)"
                              R"("locked":true}})") != std::string::npos);
    RAWFRAME_EXPECT(replyTo(kSent, "5").find(R"("code":"not_found")") != std::string::npos);
    RAWFRAME_EXPECT(replyTo(kSent, "6").find(R"("code":"malformed")") != std::string::npos);
    RAWFRAME_EXPECT(replyTo(kSent, "7").find(R"("code":"malformed")") != std::string::npos);
    RAWFRAME_EXPECT(provider.closed.empty());
}
