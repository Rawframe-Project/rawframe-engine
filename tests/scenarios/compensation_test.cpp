// Lag compensation end to end (SPEC-0041): a client over a slow, jittery
// network aims at where it sees a moving target, which is where the target
// was a while ago. Each command carries the moment the client saw, and the
// server casts the shot back to that moment. Cast now, the same shots miss.
// Only what the client was sent is taken back (the victim gate).

#include "rawframe/network_loopback/loopback.h"
#include "rawframe/physics2d/components.h"
#include "rawframe/physics2d/physics.h"
#include "rawframe/test/test.h"
#include "rawframe/world/query.h"
#include "rawframe/world_replication/client.h"
#include "rawframe/world_replication/perception.h"
#include "rawframe/world_replication/server.h"

#include <optional>
#include <vector>

using namespace rawframe;
using execution::ManualClock;
using execution::MonotonicDuration;
using physics2d::Body2D;
using physics2d::Pose2D;
using world_replication::ComponentCodec;
using world_replication::Perception;
using world_replication::WireKind;

namespace {

/// Where the player aims: straight down at this x.
struct Aim {
    static constexpr schema::ComponentTypeId kComponentTypeId =
        schema::ComponentTypeId::fromText("5c0e9a13-7b42-4d8e-a1f6-3e29b8d74c05");
    static constexpr std::string_view kComponentName = "scenario.aim";
    double x = 0;
};

ComponentCodec poseCodec() {
    return {.component = Pose2D::kComponentTypeId,
            .size = sizeof(Pose2D),
            .fields = {{offsetof(Pose2D, x), WireKind::F64},
                       {offsetof(Pose2D, y), WireKind::F64},
                       {offsetof(Pose2D, c), WireKind::F32},
                       {offsetof(Pose2D, s), WireKind::F32}}};
}

ComponentCodec aimCodec() {
    return {.component = Aim::kComponentTypeId, .size = sizeof(Aim), .fields = {{offsetof(Aim, x), WireKind::F64}}};
}

std::shared_ptr<const schema::SchemaRegistry> registry() {
    schema::RegistryBuilder builder;
    builder.add<Body2D>()
        .add<Pose2D>()
        .add<physics2d::Velocity2D>()
        .add<physics2d::Impulse2D>()
        .add<physics2d::Target2D>()
        .add<physics2d::Contact2D>()
        .add<physics2d::Character2D>()
        .add<physics2d::Joint2D>()
        .add<physics2d::Attach2D>()
        .add<Aim>()
        .add<Perception>();
    return *builder.freeze();
}

constexpr network::ProviderProfile kTransport{.maximumConnections = 4,
                                              .maximumStreamsPerConnection = 4,
                                              .maximumStreamSend = 1024,
                                              .maximumDatagram = 1200,
                                              .maximumQueuedEvents = 4096,
                                              .maximumQueuedBytes = 1 << 20};

constexpr network::SessionProfile kSessions{.maximumSessions = 4,
                                            .maximumPreAdmissionBytes = 4096,
                                            .maximumControlBuffer = 1 << 16,
                                            .maximumFramePayload = 4096,
                                            .maximumDatagramPayload = 1100,
                                            .admissionTimeout = MonotonicDuration{2'000'000'000}};

network::Compatibility compatibility() {
    network::Compatibility made{.protocol = network::protocolFingerprint()};
    made.game.bytes.fill(std::byte{9});
    return made;
}

/// Takes back only what one connection was sent by the moment it saw.
class SentGate final : public physics::RewindGate {
public:
    SentGate(const world_replication::InterestHistory& interest, const Perception& seen) noexcept
        : interest_(&interest), seen_(seen) {
    }
    [[nodiscard]] std::optional<std::uint64_t> since(world::EntityHandle entity) const noexcept override {
        return interest_->sentSince(seen_.viewer, entity, seen_.baseTick);
    }

private:
    const world_replication::InterestHistory* interest_;
    Perception seen_;
};

/// What the shooter counted.
struct Shots {
    int shots = 0;
    /// Straight down at the aim, cast back through the gate: hit the target.
    int compensated = 0;
    /// The same, cast now.
    int present = 0;
    /// Along the target's track, which meets it wherever it is: through the
    /// gate, where it was taken back to without one, and where it is now.
    int gatedAsRewound = 0;
    int gatedAsNow = 0;
    int rewoundNotNow = 0;
};

/// The server's shooter: every player that aims casts back to the moment
/// it saw and now, and counts what each hits.
class Shoot final : public world::System {
public:
    Shoot(const schema::SchemaRegistry& registry,
          const physics2d::Physics2D& physics,
          const world_replication::InterestHistory& interest,
          world::EntityHandle target,
          double trackY)
        : query_(*world::Query<world::Read<Aim>, world::Read<Perception>>::resolve(registry)), physics_(&physics),
          interest_(&interest), target_(target), trackY_(trackY) {
    }
    result::Status run(world::SystemContext& context) noexcept override {
        query_.forEach(context.world, [this](world::EntityHandle, const Aim& aim, const Perception& seen) {
            if (aim.x == 0 || seen.baseTick == 0 || seen.viewer == 0) {
                return;
            }
            ++counted.shots;
            const SentGate kGate{*interest_, seen};
            const physics::Moment kGated{.base = seen.baseTick, .fraction = seen.fraction, .gate = &kGate};
            const physics::Moment kEvery{.base = seen.baseTick, .fraction = seen.fraction};
            counted.compensated +=
                physics_->castRayAt(aim.x, trackY_ + 5, 0, -10, kGated, physics::kEveryClass).entity == target_;
            counted.present += physics_->castRay(aim.x, trackY_ + 5, 0, -10, physics::kEveryClass).entity == target_;
            const double kGatedX = physics_->castRayAt(-60, trackY_, 120, 0, kGated, physics::kEveryClass).x;
            const double kRewoundX = physics_->castRayAt(-60, trackY_, 120, 0, kEvery, physics::kEveryClass).x;
            const double kNowX = physics_->castRay(-60, trackY_, 120, 0, physics::kEveryClass).x;
            counted.gatedAsRewound += kGatedX == kRewoundX;
            counted.gatedAsNow += kGatedX == kNowX;
            counted.rewoundNotNow += kRewoundX != kNowX;
        });
        return {};
    }
    Shots counted;

private:
    world::Query<world::Read<Aim>, world::Read<Perception>> query_;
    const physics2d::Physics2D* physics_;
    const world_replication::InterestHistory* interest_;
    world::EntityHandle target_;
    double trackY_;
};

struct Played {
    Shots shots;
    physics2d::Physics2DStatistics physics;
    world_replication::ServerReplicationStatistics replication;
};

/// Three seconds of play. With `unseen`, each connection is sent only what
/// is within a meter of its player, who stands at the origin, and the target
/// slides by fifty meters above it: the client never sees it and aims
/// blindly; otherwise it is sent everything and aims where it shows the
/// target.
Played play(bool unseen) {
    ManualClock clock;
    network_loopback::LoopbackNetwork network{clock,
                                              {.latency = MonotonicDuration::fromMilliseconds(50),
                                               .jitter = MonotonicDuration::fromMilliseconds(10),
                                               .seed = 21}};
    const auto kSchema = registry();
    const double kTrackY = unseen ? 50 : 0;

    // The server: a target sliding right at 4 m/s, physics, and replication
    // of every pose, with perceived input.
    world::World server{kSchema};
    auto physics = *physics2d::Physics2D::create({.gravityY = 0, .historyTicks = 64});
    const world::EntityHandle kTarget = *server.create();
    RAWFRAME_EXPECT(server
                        .insert(kTarget,
                                *kSchema->key<Body2D>(),
                                Body2D{.motion = static_cast<std::uint8_t>(physics::Motion::Kinematic),
                                       .shape = static_cast<std::uint8_t>(physics2d::Shape::Box),
                                       .width = 0.5F,
                                       .height = 0.5F})
                        .has_value());
    RAWFRAME_EXPECT(server.insert(kTarget, *kSchema->key<Pose2D>(), Pose2D{.x = -6, .y = kTrackY}).has_value());
    RAWFRAME_EXPECT(server.insert(kTarget, *kSchema->key<physics2d::Velocity2D>(), {.x = 4}).has_value());
    RAWFRAME_EXPECT(server.insert(kTarget, *kSchema->key<physics2d::Impulse2D>(), {}).has_value());
    RAWFRAME_EXPECT(server.insert(kTarget, *kSchema->key<physics2d::Contact2D>(), {}).has_value());
    auto serverTransport = *network.provider(kTransport);
    auto serverSessions = *network::Sessions::server(
        *serverTransport, clock, {.profile = kSessions, .expected = compatibility(), .seed = 1});
    RAWFRAME_EXPECT(serverSessions->listen({"server"}).has_value());
    world_replication::ServerReplicationSettings settings{
        .table = {.components = {poseCodec()}},
        .playerComponents = {Aim::kComponentTypeId, Perception::kComponentTypeId},
        .input = aimCodec(),
        .perception = true};
    if (unseen) {
        // The player stands at the origin, and its own pose keeps states
        // coming.
        settings.playerComponents.push_back(Pose2D::kComponentTypeId);
        settings.interest = world_replication::InterestSettings{
            .position = Pose2D::kComponentTypeId,
            .axes = {{offsetof(Pose2D, x), WireKind::F64}, {offsetof(Pose2D, y), WireKind::F64}},
            .radius = 1,
            .leaveRadius = 1.125};
    }
    auto replication = *world_replication::ReplicationServer::create(*serverSessions, std::move(settings));
    std::vector<world::SystemDeclaration> declarations;
    RAWFRAME_EXPECT(replication->declareSystems(*kSchema, declarations).has_value());
    RAWFRAME_EXPECT(physics->declareSystems(*kSchema, declarations).has_value());
    Shoot shoot{*kSchema, *physics, *replication, kTarget, kTrackY};
    constexpr std::array<std::string_view, 1> kBeforeStep = {physics2d::kStepSystem};
    declarations.push_back(
        world::SystemDeclaration{.identity = "scenario.shoot", .before = kBeforeStep, .system = &shoot});
    auto schedule = *world::Schedule::compile(declarations, *kSchema);
    world::TickIndex tick;

    // The client: it shows remote poses between states, and aims at the
    // target where it shows it.
    world::World mirror{kSchema};
    auto clientTransport = *network.provider(kTransport);
    auto clientSessions = *network::Sessions::client(*clientTransport, clock, {.profile = kSessions, .seed = 2});
    auto client = *world_replication::ReplicationClient::create(
        *clientSessions,
        mirror,
        {.table = {.components = {poseCodec()}},
         .input = aimCodec(),
         .perception = true,
         .interpolation =
             world_replication::InterpolationSettings{.interpolated = {Pose2D::kComponentTypeId}, .clock = &clock}});
    RAWFRAME_EXPECT(
        client
            ->connect({"server"},
                      network::Hello{.compatibility = compatibility(), .maximumDatagram = 1100, .maximumFrame = 4096})
            .has_value());
    auto shown = *world::Query<world::Read<Pose2D>>::resolve(*kSchema);

    // Half a tick for the server, half a tick later the client aims: what it
    // shows then lies between two states.
    for (int step = 0; step < 180; ++step) {
        clock.advance(MonotonicDuration{8'333'333});
        replication->pump(server, tick);
        RAWFRAME_EXPECT(schedule.runTick(server, tick, *world::TickRate::of(60)).has_value());
        client->pump();
        clock.advance(MonotonicDuration{8'333'334});
        client->pump();
        if (!client->admitted()) {
            continue;
        }
        Aim aim{.x = 1};
        if (!unseen) {
            shown.forEach(mirror, [&aim](world::EntityHandle, const Pose2D& pose) {
                aim.x = pose.x;
            });
        }
        RAWFRAME_EXPECT(client->submitInput(std::as_bytes(std::span{&aim, 1})).has_value());
    }
    return Played{.shots = shoot.counted, .physics = physics->statistics(), .replication = replication->statistics()};
}

} // namespace

RAWFRAME_TEST(AShotCastBackToWhatThePlayerSawHits) {
    const Played kPlayed = play(false);
    const Shots& kShots = kPlayed.shots;
    // Shot after shot at what the client saw hits, though the target has
    // moved on by more than its half width; cast now, they miss.
    RAWFRAME_EXPECT(kShots.shots > 120);
    RAWFRAME_EXPECT(kShots.compensated == kShots.shots);
    RAWFRAME_EXPECT(kShots.present < kShots.shots / 10);
    RAWFRAME_EXPECT(kPlayed.physics.rewindsClamped == 0);
    // The target was sent, so the gate takes it back, but no further than
    // the first state sent: the first shot, aimed before a second state
    // arrived, is at that state as the client showed it. An honest client's
    // moments are never clamped.
    RAWFRAME_EXPECT(kShots.gatedAsRewound >= kShots.shots - 2 && kShots.rewoundNotNow > kShots.shots * 9 / 10);
    RAWFRAME_EXPECT(kPlayed.replication.perceptionsClamped == 0);
}

RAWFRAME_TEST(WhatAClientWasNeverSentIsNotTakenBack) {
    const Played kPlayed = play(true);
    const Shots& kShots = kPlayed.shots;
    // Cast back without the gate, the target is where it was; through the
    // gate, it is where it is now, as a client that never saw it would have
    // to shoot at.
    RAWFRAME_EXPECT(kShots.shots > 120);
    RAWFRAME_EXPECT(kShots.rewoundNotNow > kShots.shots * 9 / 10);
    RAWFRAME_EXPECT(kShots.gatedAsNow == kShots.shots);
    RAWFRAME_EXPECT(kPlayed.replication.perceptionsClamped == 0);
}
