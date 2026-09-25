// The Phase 1 shape in one process: a server World running a Kest game,
// replicated over loopback to 64 headless bot clients that each steer their
// own player, with the server's tick time measured against SPEC-0013's
// budget (p50 5 ms, p95 8.33 ms, p99 12.5 ms at 60 Hz).

#include "rawframe/network_loopback/loopback.h"
#include "rawframe/test/test.h"
#include "rawframe/world/column_query.h"
#include "rawframe/world_kest/kest_systems.h"
#include "rawframe/world_kest/replication.h"
#include "rawframe/world_replication/client.h"
#include "rawframe/world_replication/server.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <map>
#include <vector>

using namespace rawframe;
using execution::ManualClock;
using execution::MonotonicDuration;

namespace {

constexpr std::string_view kArena = "module arena\n"
                                    "\n"
                                    "struct Position {\n"
                                    "    x: f32\n"
                                    "    y: f32\n"
                                    "}\n"
                                    "\n"
                                    "struct Steer {\n"
                                    "    dx: f32\n"
                                    "    dy: f32\n"
                                    "}\n"
                                    "\n"
                                    "fn drive(count: i32, positions: [Position], steers: [Steer]) {\n"
                                    "    let i = 0\n"
                                    "    while i < count {\n"
                                    "        positions[i].x = positions[i].x + steers[i].dx\n"
                                    "        positions[i].y = positions[i].y + steers[i].dy\n"
                                    "        i = i + 1\n"
                                    "    }\n"
                                    "}\n";

constexpr auto kPositionId = schema::ComponentTypeId::fromText("4e8c1a27-b6d3-4f90-8a15-c3e72d9b0f46");
constexpr auto kSteerId = schema::ComponentTypeId::fromText("a93f6d05-1c8e-4b72-9e40-5d2b8f7c1a63");

constexpr std::size_t kBots = 64;
constexpr std::size_t kProps = 200;
constexpr int kTicks = 300;

constexpr network::ProviderProfile kTransport{.maximumConnections = kBots + 4,
                                              .maximumStreamsPerConnection = 4,
                                              .maximumStreamSend = 1024,
                                              .maximumDatagram = 1200,
                                              .maximumQueuedEvents = 8192,
                                              .maximumQueuedBytes = 1 << 23};

constexpr network::SessionProfile kSessions{.maximumSessions = kBots + 4,
                                            .maximumPreAdmissionBytes = 4096,
                                            .maximumControlBuffer = 1 << 20,
                                            .maximumFramePayload = 4096,
                                            .maximumDatagramPayload = 1100,
                                            .admissionTimeout = MonotonicDuration{5'000'000'000}};

struct Steer {
    float dx = 0;
    float dy = 0;
};

struct Bot {
    std::unique_ptr<network::Provider> transport;
    std::unique_ptr<network::Sessions> sessions;
    std::unique_ptr<world::World> world;
    std::unique_ptr<world_replication::ReplicationClient> client;
};

double percentile(std::vector<double> samples, double fraction) {
    std::sort(samples.begin(), samples.end());
    return samples[static_cast<std::size_t>(fraction * static_cast<double>(samples.size() - 1))];
}

} // namespace

RAWFRAME_TEST(SixtyFourBotsPlayAKestGame) {
    // The game: its program, and its components as the program lays them out.
    const std::array<kest::SourceFile, 1> kFiles = {
        kest::SourceFile{.path = "arena.kest", .text = std::string{kArena}}};
    const auto kProgram = *kest::Program::compile(kFiles, {});
    const auto kPositionLayout = *kProgram->layout("Position");
    const auto kSteerLayout = *kProgram->layout("Steer");
    schema::RegistryBuilder builder;
    for (const auto& [id, name, layout] : {std::tuple{kPositionId, "arena.position", kPositionLayout},
                                           std::tuple{kSteerId, "arena.steer", kSteerLayout}}) {
        builder.add(schema::ComponentDescriptor{
            .id = id, .name = name, .size = layout.size, .alignment = layout.alignment, .plainData = true});
    }
    const auto kRegistry = *builder.freeze();
    const auto kPositionCodec = *world_kest::codecFor(kPositionId, kPositionLayout, {});
    const auto kSteerCodec = *world_kest::codecFor(kSteerId, kSteerLayout, {});
    const world_replication::ReplicationTable kTable{.components = {kPositionCodec, kSteerCodec}};

    ManualClock clock;
    network_loopback::LoopbackNetwork network{clock, {.latency = MonotonicDuration::fromMilliseconds(15)}};
    network::Compatibility compatibility{.protocol = network::protocolFingerprint()};
    compatibility.game.bytes.fill(std::byte{0xa7});

    // The server: Kest systems, replication, and props that drift.
    world::World server{kRegistry};
    auto serverTransport = *network.provider(kTransport);
    auto serverSessions = *network::Sessions::server(
        *serverTransport, clock, {.profile = kSessions, .expected = compatibility, .seed = 3});
    RAWFRAME_EXPECT(serverSessions->listen({"arena"}).has_value());
    auto replication = *world_replication::ReplicationServer::create(
        *serverSessions, {.table = kTable, .playerComponents = {kPositionId, kSteerId}, .input = kSteerCodec});
    constexpr std::array<world_kest::KestColumn, 2> kDrive = {
        world_kest::KestColumn{.component = kPositionId, .element = "Position", .access = world::Access::Write},
        world_kest::KestColumn{.component = kSteerId, .element = "Steer", .access = world::Access::Read}};
    const std::array<world_kest::KestSystemDeclaration, 1> kSystems = {
        world_kest::KestSystemDeclaration{.identity = "arena.drive", .entry = "drive", .columns = kDrive}};
    auto kestSystems = *world_kest::KestSystems::create(
        {.program = kProgram, .limits = {.heapBytes = 1U << 20U, .fuelPerCall = 10'000'000}, .systems = kSystems});
    std::vector<world::SystemDeclaration> declarations;
    RAWFRAME_EXPECT(kestSystems->declareSystems(*kRegistry, declarations).has_value());
    RAWFRAME_EXPECT(replication->declareSystems(*kRegistry, declarations).has_value());
    auto schedule = *world::Schedule::compile(declarations, *kRegistry);
    const auto kPosition = *kRegistry->find(kPositionId);
    const auto kSteer = *kRegistry->find(kSteerId);
    for (std::size_t index = 0; index < kProps; ++index) {
        const world::EntityHandle kProp = *server.create();
        const std::array<float, 2> kAt = {static_cast<float>(index), 0};
        const std::array<float, 2> kDrift = {0, 0.25F};
        RAWFRAME_EXPECT(server.insertErased(kProp, kPosition, const_cast<float*>(kAt.data())).has_value());
        RAWFRAME_EXPECT(server.insertErased(kProp, kSteer, const_cast<float*>(kDrift.data())).has_value());
    }

    // The bots.
    std::vector<Bot> bots(kBots);
    for (std::size_t index = 0; index < kBots; ++index) {
        Bot& bot = bots[index];
        bot.transport = *network.provider(kTransport);
        bot.sessions = *network::Sessions::client(*bot.transport, clock, {.profile = kSessions, .seed = 100 + index});
        bot.world = std::make_unique<world::World>(kRegistry);
        bot.client = *world_replication::ReplicationClient::create(
            *bot.sessions, *bot.world, {.table = kTable, .input = kSteerCodec});
        RAWFRAME_EXPECT(
            bot.client
                ->connect({"arena"},
                          network::Hello{.compatibility = compatibility, .maximumDatagram = 1100, .maximumFrame = 4096})
                .has_value());
    }

    world::TickIndex tick;
    std::vector<double> tickMilliseconds;
    for (int step = 0; step < kTicks; ++step) {
        clock.advance(MonotonicDuration{16'666'667});
        const auto kStart = std::chrono::steady_clock::now();
        replication->pump(server, tick);
        RAWFRAME_EXPECT(schedule.runTick(server, tick, *world::TickRate::of(60)).has_value());
        tickMilliseconds.push_back(
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - kStart).count());
        for (std::size_t index = 0; index < kBots; ++index) {
            Bot& bot = bots[index];
            bot.client->pump();
            if (bot.client->admitted()) {
                // Each bot steers its own way.
                const Steer kSteerValue{static_cast<float>(index % 5) - 2.0F, 1.0F};
                RAWFRAME_EXPECT(bot.client->submitInput(std::as_bytes(std::span{&kSteerValue, 1})).has_value());
            }
        }
    }

    // Every bot is in, sees every player and prop, and plays its own.
    std::size_t complete = 0;
    for (Bot& bot : bots) {
        std::size_t mirrored = 0;
        const std::array<world::ColumnTerm, 1> kTerms = {world::ColumnTerm{kPosition, world::Access::Read}};
        auto query = world::ColumnQuery::resolve(kTerms, *kRegistry);
        mirrored = query->count(*bot.world);
        const float* own = static_cast<const float*>(bot.world->getErased(bot.client->owned(), kPosition));
        complete += (mirrored == kBots + kProps && own != nullptr && own[1] > 100) ? 1 : 0;
    }
    RAWFRAME_EXPECT(complete == kBots);
    const auto kStatistics = replication->statistics();
    RAWFRAME_EXPECT(kStatistics.inputsConsumed > kStatistics.inputsNeutral * 4);

    const double kP50 = percentile(tickMilliseconds, 0.50);
    const double kP95 = percentile(tickMilliseconds, 0.95);
    const double kP99 = percentile(tickMilliseconds, 0.99);
    std::fprintf(stderr,
                 "server tick with %zu bots and %zu props (%s): p50 %.3f ms, p95 %.3f ms, p99 %.3f ms\n",
                 kBots,
                 kProps,
                 RAWFRAME_CONFIGURATION_NAME,
                 kP50,
                 kP95,
                 kP99);
}
