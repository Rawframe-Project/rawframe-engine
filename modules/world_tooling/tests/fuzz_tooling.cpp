// Coverage-guided fuzzing of the tooling endpoint's records (D408, D675):
// the bytes arrive as one stream on a connection that has not said hello,
// and again after a hello that admits it, so both the refusals before
// admission and every verb after it read them, split into lines as they
// come. Whatever they hold, every line the server sends is one JSON object.

#include "rawframe/document/json.h"
#include "rawframe/schema/registry.h"
#include "rawframe/world_tooling/server.h"

#include <cstdlib>
#include <map>
#include <string>
#include <string_view>
#include <vector>

using namespace rawframe;
using namespace rawframe::world_tooling;

namespace {

struct Crate {
    static constexpr schema::ComponentTypeId kComponentTypeId =
        schema::ComponentTypeId::fromText("6d2f84a1-3c7e-4b09-9a51-e8f2c0d47b16");
    static constexpr std::string_view kComponentName = "fuzz.crate";
    float x = 0;
};

/// A provider the fuzzer feeds, keeping what the server sends.
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
    void close(network::ConnectionId) noexcept override {
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
    void say(std::uint64_t connection, std::string_view text) {
        network::Event event{.kind = network::EventKind::StreamBytes, .connection = {connection}};
        for (const char kByte : text) {
            event.bytes.push_back(static_cast<std::byte>(kByte));
        }
        queued.push_back(std::move(event));
    }

    std::vector<network::Event> queued;
    std::map<std::uint64_t, std::string> sent;
};

const std::string kToken(40, 't');

} // namespace

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    const std::string_view kBytes{reinterpret_cast<const char*>(data), size};
    schema::RegistryBuilder builder;
    builder.add<Crate>();
    const auto kRegistry = builder.freeze();
    if (!kRegistry.has_value()) {
        std::abort();
    }
    world::World world{*kRegistry};
    const auto kKey = (*kRegistry)->key<Crate>();
    for (int made = 0; made < 3; ++made) {
        const auto kEntity = world.create();
        if (!kEntity.has_value() || !kKey.has_value() || !world.insert(*kEntity, *kKey, Crate{.x = 1}).has_value()) {
            std::abort();
        }
    }
    FedProvider provider;
    auto server = ToolingServer::create(
        provider, {.token = kToken, .grants = {.inspect = true, .view = true}, .maximumRecord = 4096});
    if (!server.has_value()) {
        std::abort();
    }
    provider.accept(1);
    provider.accept(2);
    provider.say(1, kBytes);
    provider.say(2, R"({"kind":"tooling.hello","id":1,"protocolVersion":1,"token":")" + kToken + "\"}\n");
    provider.say(2, kBytes);
    (*server)->serve(&world, world::TickIndex{1}, {});
    provider.say(2, "\n");
    (*server)->serve(&world, world::TickIndex{2}, execution::MonotonicInstant{});
    (*server)->serve(
        nullptr, {}, execution::MonotonicInstant{execution::MonotonicDuration::fromSeconds(6).nanoseconds});
    for (const auto& [kConnection, kSent] : provider.sent) {
        std::size_t from = 0;
        while (from < kSent.size()) {
            const std::size_t kEnd = kSent.find('\n', from);
            if (kEnd == std::string::npos) {
                std::abort();
            }
            const auto kLine = document::parse(std::string_view{kSent}.substr(from, kEnd - from));
            if (!kLine.has_value() || kLine->kind() != document::Value::Kind::Object) {
                std::abort();
            }
            from = kEnd + 1;
        }
    }
    return 0;
}
