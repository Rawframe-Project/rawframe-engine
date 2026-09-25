#include "rawframe/physics/errors.h"
#include "rawframe/physics2d/components.h"
#include "rawframe/physics2d/errors.h"
#include "rawframe/physics2d/physics.h"
#include "rawframe/test/test.h"
#include "rawframe/world/schedule.h"
#include "rawframe/world/world.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <optional>
#include <string>
#include <vector>

using namespace rawframe;
using namespace rawframe::physics2d;
using namespace rawframe::physics;

namespace {

std::shared_ptr<const schema::SchemaRegistry> registry() {
    schema::RegistryBuilder builder;
    builder.add<Body2D>().add<Pose2D>().add<Velocity2D>().add<Impulse2D>().add<Contact2D>().add<Character2D>();
    return *builder.freeze();
}

/// A World with physics, ticked at 60 Hz.
struct Scene {
    std::shared_ptr<const schema::SchemaRegistry> schema = registry();
    world::World world{schema};
    std::unique_ptr<Physics2D> physics;
    std::optional<world::Schedule> schedule;
    world::TickIndex tick;

    explicit Scene(const Physics2DSettings& settings = {}) {
        physics = *Physics2D::create(settings);
        std::vector<world::SystemDeclaration> declarations;
        RAWFRAME_EXPECT(physics->declareSystems(*schema, declarations).has_value());
        schedule.emplace(*world::Schedule::compile(declarations, *schema));
    }

    world::EntityHandle body(const Body2D& body, const Pose2D& pose, const Velocity2D& velocity = {}) {
        const world::EntityHandle kEntity = *world.create();
        RAWFRAME_EXPECT(world.insert(kEntity, *schema->key<Body2D>(), body).has_value());
        RAWFRAME_EXPECT(world.insert(kEntity, *schema->key<Pose2D>(), pose).has_value());
        RAWFRAME_EXPECT(world.insert(kEntity, *schema->key<Velocity2D>(), velocity).has_value());
        RAWFRAME_EXPECT(world.insert(kEntity, *schema->key<Impulse2D>(), Impulse2D{}).has_value());
        RAWFRAME_EXPECT(world.insert(kEntity, *schema->key<Contact2D>(), Contact2D{}).has_value());
        return kEntity;
    }

    world::EntityHandle character(const Pose2D& pose, float snap = 0) {
        const world::EntityHandle kEntity = body(Body2D{.motion = static_cast<std::uint8_t>(physics::Motion::Kinematic),
                                                        .shape = static_cast<std::uint8_t>(Shape::Capsule),
                                                        .fixedRotation = true,
                                                        .width = 0.3F,
                                                        .height = 0.4F},
                                                 pose);
        RAWFRAME_EXPECT(
            world.insert(kEntity, *schema->key<Character2D>(), Character2D{.groundNormal = 0.7F, .snap = snap})
                .has_value());
        return kEntity;
    }

    /// A platformer's gameplay for one tick: run at `run` meters a second,
    /// and fall under gravity unless on ground.
    void walk(world::EntityHandle entity, float run) {
        Velocity2D& wanted = velocity(entity);
        const bool kGrounded = character(entity).ground == static_cast<std::uint8_t>(physics::Ground::Grounded);
        wanted = Velocity2D{.x = run, .y = kGrounded ? 0 : wanted.y - (20.0F / 60)};
        this->run(1);
    }

    Character2D& character(world::EntityHandle entity) {
        return *world.get(entity, *schema->key<Character2D>());
    }

    void run(int ticks) {
        for (int index = 0; index < ticks; ++index) {
            RAWFRAME_EXPECT(schedule->runTick(world, tick, *world::TickRate::of(60)).has_value());
        }
    }

    Pose2D& pose(world::EntityHandle entity) {
        return *world.get(entity, *schema->key<Pose2D>());
    }
    Velocity2D& velocity(world::EntityHandle entity) {
        return *world.get(entity, *schema->key<Velocity2D>());
    }
    Contact2D& contact(world::EntityHandle entity) {
        return *world.get(entity, *schema->key<Contact2D>());
    }
};

constexpr Body2D kGround{.motion = static_cast<std::uint8_t>(physics::Motion::Static),
                         .shape = static_cast<std::uint8_t>(Shape::Box),
                         .width = 10,
                         .height = 0.5F,
                         .friction = 0.6F};

constexpr Body2D kCrate{.motion = static_cast<std::uint8_t>(physics::Motion::Dynamic),
                        .shape = static_cast<std::uint8_t>(Shape::Box),
                        .width = 0.5F,
                        .height = 0.5F,
                        .density = 1,
                        .friction = 0.6F};

constexpr Body2D kBall{.motion = static_cast<std::uint8_t>(physics::Motion::Dynamic),
                       .shape = static_cast<std::uint8_t>(Shape::Circle),
                       .width = 0.25F,
                       .density = 1,
                       .friction = 0.3F,
                       .restitution = 0.5F};

} // namespace

RAWFRAME_TEST(ACrateFallsAndRestsOnTheGround) {
    Scene scene;
    scene.body(kGround, {.x = 0, .y = 0});
    const world::EntityHandle kCrateEntity = scene.body(kCrate, {.x = 0, .y = 5});
    scene.run(1);
    // Falling: gravity has taken hold after one tick.
    RAWFRAME_EXPECT(scene.velocity(kCrateEntity).y < 0 && scene.pose(kCrateEntity).y < 5);
    scene.run(239);
    const Pose2D& kRest = scene.pose(kCrateEntity);
    RAWFRAME_EXPECT(std::abs(kRest.y - 1.0) < 0.02 && std::abs(kRest.x) < 0.001);
    RAWFRAME_EXPECT(std::abs(scene.velocity(kCrateEntity).y) < 0.01);
    // Unrotated, as it was dropped.
    RAWFRAME_EXPECT(std::abs(kRest.c - 1.0F) < 0.001F && std::abs(kRest.s) < 0.001F);
    const Physics2DStatistics kStatistics = scene.physics->statistics();
    RAWFRAME_EXPECT(kStatistics.steps == 240 && kStatistics.bodiesMade == 2 && kStatistics.bodiesRefused == 0);
}

RAWFRAME_TEST(TheSameWorldAndWritesGiveTheSameBits) {
    const auto kPlay = [](Scene& scene) {
        scene.body(kGround, {.x = 0, .y = 0});
        std::vector<world::EntityHandle> balls;
        for (int index = 0; index < 20; ++index) {
            balls.push_back(scene.body(kBall, {.x = (index % 5) * 0.4 - 1.0, .y = 2.0 + (index / 5) * 0.6}));
        }
        for (int tick = 0; tick < 120; ++tick) {
            if (tick % 30 == 0) {
                scene.world.get(balls[static_cast<std::size_t>(tick / 30)], *scene.schema->key<Impulse2D>())->x = 0.2F;
            }
            scene.run(1);
        }
        std::vector<Pose2D> poses;
        for (const world::EntityHandle kEach : balls) {
            poses.push_back(scene.pose(kEach));
        }
        return poses;
    };
    Scene first;
    Scene second;
    const std::vector<Pose2D> kFirst = kPlay(first);
    const std::vector<Pose2D> kSecond = kPlay(second);
    RAWFRAME_EXPECT(first.physics->digest() == second.physics->digest());
    RAWFRAME_EXPECT(kFirst.size() == kSecond.size() &&
                    std::memcmp(kFirst.data(), kSecond.data(), kFirst.size() * sizeof(Pose2D)) == 0);
    // And something did happen: the balls moved and bounced apart.
    RAWFRAME_EXPECT(first.physics->statistics().impulses == 4);
    RAWFRAME_EXPECT(!(std::memcmp(&kFirst[0], &kFirst[1], sizeof(Pose2D)) == 0));
}

RAWFRAME_TEST(GameplayWritesTeleportPushAndSetVelocity) {
    Scene scene{{.gravityY = 0}};
    const world::EntityHandle kCrateEntity = scene.body(kCrate, {.x = 0, .y = 0});
    scene.run(1);
    // A teleport: the pose written is where the body is.
    scene.pose(kCrateEntity) = Pose2D{.x = 3, .y = -2, .c = 1, .s = 0};
    scene.run(1);
    RAWFRAME_EXPECT(scene.pose(kCrateEntity).x == 3 && scene.pose(kCrateEntity).y == -2);
    // A velocity written is kept, with nothing to slow it.
    scene.velocity(kCrateEntity) = Velocity2D{.x = 1, .y = 0, .angular = 0};
    scene.run(60);
    RAWFRAME_EXPECT(std::abs(scene.pose(kCrateEntity).x - 4.0) < 0.001);
    // An impulse is applied once and cleared: one kilogram (a one meter
    // square of density one) pushed by one newton second gains one meter a
    // second.
    Impulse2D& impulse = *scene.world.get(kCrateEntity, *scene.schema->key<Impulse2D>());
    impulse = Impulse2D{.x = 0, .y = 1, .angular = 0};
    scene.run(1);
    RAWFRAME_EXPECT(std::abs(scene.velocity(kCrateEntity).y - 1.0F) < 0.001F);
    RAWFRAME_EXPECT(impulse.y == 0);
    scene.run(1);
    RAWFRAME_EXPECT(std::abs(scene.velocity(kCrateEntity).y - 1.0F) < 0.001F);
    const Physics2DStatistics kStatistics = scene.physics->statistics();
    RAWFRAME_EXPECT(kStatistics.teleports == 1 && kStatistics.velocitiesSet == 1 && kStatistics.impulses == 1);
}

RAWFRAME_TEST(BodiesFollowTheirEntities) {
    Scene scene{{.gravityY = 0}};
    const world::EntityHandle kFirst = scene.body(kCrate, {.x = 0, .y = 0});
    // Not makeable: no size. Refused once, not every tick.
    Body2D broken = kBall;
    broken.width = 0;
    const world::EntityHandle kBroken = scene.body(broken, {.x = 5, .y = 0}, {.x = 1});
    scene.run(10);
    RAWFRAME_EXPECT(scene.physics->statistics().bodiesRefused == 1);
    RAWFRAME_EXPECT(scene.pose(kBroken).x == 5);
    // Mended, it is made and moves.
    scene.world.get(kBroken, *scene.schema->key<Body2D>())->width = 0.25F;
    scene.run(1);
    RAWFRAME_EXPECT(scene.physics->statistics().bodiesMade == 2 && scene.pose(kBroken).x > 5);
    // A changed Body2D makes the body again where it is.
    scene.world.get(kFirst, *scene.schema->key<Body2D>())->shape = static_cast<std::uint8_t>(Shape::Circle);
    scene.run(1);
    RAWFRAME_EXPECT(scene.physics->statistics().bodiesMade == 3 && scene.physics->statistics().bodiesRemoved == 1);
    // An entity that is gone takes its body with it.
    RAWFRAME_EXPECT(scene.world.destroy(kFirst).has_value());
    scene.run(1);
    RAWFRAME_EXPECT(scene.physics->statistics().bodiesRemoved == 2);
}

RAWFRAME_TEST(ABodyIsToldWhatItTouches) {
    Scene scene;
    const world::EntityHandle kGroundEntity = scene.body(kGround, {.x = 0, .y = 0});
    const world::EntityHandle kBallEntity = scene.body(kBall, {.x = 0, .y = 2});
    int landed = -1;
    for (int tick = 0; tick < 120 && landed < 0; ++tick) {
        scene.run(1);
        landed = scene.contact(kBallEntity).began == 1 ? tick : -1;
    }
    RAWFRAME_EXPECT(landed > 0);
    // Both sides are told, each of the other, with the normal from itself
    // toward the other: the ball's points down, the ground's up.
    const Contact2D kBallContact = scene.contact(kBallEntity);
    const Contact2D kGroundContact = scene.contact(kGroundEntity);
    RAWFRAME_EXPECT(kBallContact.hit == kGroundEntity && kGroundContact.hit == kBallEntity);
    RAWFRAME_EXPECT(kBallContact.hitSpeed > 1 && kBallContact.hitNormalY < -0.99F && kGroundContact.hitNormalY > 0.99F);
    RAWFRAME_EXPECT(kBallContact.touching == 1 && kGroundContact.touching == 1);
    // A step later nothing new began, and they still touch (or the ball has
    // bounced off and the end is counted).
    scene.run(1);
    const Contact2D kAfter = scene.contact(kBallEntity);
    RAWFRAME_EXPECT(kAfter.began == 0 && kAfter.hit.isNull() && kAfter.touching + kAfter.ended == 1);
    RAWFRAME_EXPECT(scene.physics->statistics().contactsBegun >= 1);
}

RAWFRAME_TEST(ASensorIsToldWhatPassesThroughIt) {
    Scene scene;
    Body2D zone = kGround;
    zone.sensor = true;
    zone.height = 1;
    const world::EntityHandle kZone = scene.body(zone, {.x = 0, .y = 5});
    const world::EntityHandle kBallEntity = scene.body(kBall, {.x = 0, .y = 8});
    std::uint32_t entered = 0;
    std::uint32_t exited = 0;
    std::uint32_t mostInside = 0;
    world::EntityHandle visitor;
    for (int tick = 0; tick < 120; ++tick) {
        scene.run(1);
        const Contact2D& zoneContact = scene.contact(kZone);
        entered += zoneContact.entered;
        exited += zoneContact.exited;
        mostInside = std::max(mostInside, zoneContact.overlapping);
        visitor = zoneContact.visitor.isNull() ? visitor : zoneContact.visitor;
    }
    // The ball fell through untouched: one entry, one exit, told to both.
    RAWFRAME_EXPECT(entered == 1 && exited == 1 && mostInside == 1 && visitor == kBallEntity);
    RAWFRAME_EXPECT(scene.pose(kBallEntity).y < 0);
    RAWFRAME_EXPECT(scene.physics->statistics().overlapsBegun == 1 && scene.physics->statistics().contactsBegun == 0);
}

RAWFRAME_TEST(ARayFindsTheClosestBody) {
    Scene scene;
    const world::EntityHandle kGroundEntity = scene.body(kGround, {.x = 0, .y = 0});
    const world::EntityHandle kBallEntity = scene.body(kBall, {.x = 3, .y = 5});
    scene.run(1);
    // Straight down onto the ground's top, half a meter up.
    const RayHit2D kDown = scene.physics->castRay(0, 5, 0, -10, kEveryClass);
    RAWFRAME_EXPECT(kDown.hit && !kDown.inside && kDown.entity == kGroundEntity);
    RAWFRAME_EXPECT(std::abs(kDown.y - 0.5) < 0.001 && kDown.normalY > 0.99F &&
                    std::abs(kDown.fraction - 0.45F) < 0.001F);
    // Sideways into the ball, where it was after the last step.
    const RayHit2D kAcross = scene.physics->castRay(0, scene.pose(kBallEntity).y, 10, 0, kEveryClass);
    RAWFRAME_EXPECT(kAcross.hit && kAcross.entity == kBallEntity && std::abs(kAcross.x - 2.75) < 0.001);
    // Starting inside the ground, and missing everything.
    const RayHit2D kInside = scene.physics->castRay(0, 0, 0, -1, kEveryClass);
    RAWFRAME_EXPECT(kInside.hit && kInside.inside && kInside.entity == kGroundEntity);
    const RayHit2D kMiss = scene.physics->castRay(0, 5, 0, 10, kEveryClass);
    RAWFRAME_EXPECT(!kMiss.hit && kMiss.entity.isNull());
}

RAWFRAME_TEST(CollisionClassesDecideWhatMeets) {
    constexpr std::uint64_t kPlayer = 0x7a31c0de00000001;
    constexpr std::uint64_t kGhost = 0x7a31c0de00000002;
    constexpr std::uint64_t kCoin = 0x7a31c0de00000003;
    constexpr std::uint64_t kWall = 0x7a31c0de00000004;
    Scene scene{{.gravityY = 0,
                 .collision = {.classes = {{kPlayer, "player"}, {kGhost, "ghost"}, {kCoin, "coin"}, {kWall, "wall"}},
                               .rules = {{kPlayer, kGhost, CollisionRule::Ignore},
                                         {kCoin, kPlayer, CollisionRule::Trigger},
                                         {kCoin, kCoin, CollisionRule::Trigger}},
                               .fallback = CollisionRule::Collide}}};
    const auto kOf = [](Body2D body, std::uint64_t collisionClass) {
        body.collisionClass = collisionClass;
        return body;
    };
    Body2D still = kCrate;
    still.motion = static_cast<std::uint8_t>(physics::Motion::Static);
    const world::EntityHandle kRunner = scene.body(kOf(kBall, kPlayer), {.x = 0, .y = 0}, {.x = 4});
    const world::EntityHandle kGhostEntity = scene.body(kOf(still, kGhost), {.x = 2, .y = 0});
    const world::EntityHandle kCoinEntity = scene.body(kOf(still, kCoin), {.x = 4, .y = 0});
    const world::EntityHandle kWallEntity = scene.body(kOf(still, kWall), {.x = 7, .y = 0});
    Body2D unknown = kBall;
    unknown.collisionClass = 0x7a31c0de000000ff;
    scene.body(unknown, {.x = 0, .y = 10});
    std::uint32_t ghostTouches = 0;
    std::uint32_t coinEntered = 0;
    std::uint32_t runnerEntered = 0;
    world::EntityHandle wallHit;
    for (int tick = 0; tick < 120; ++tick) {
        scene.run(1);
        ghostTouches += scene.contact(kGhostEntity).began + scene.contact(kGhostEntity).entered;
        coinEntered += scene.contact(kCoinEntity).entered;
        runnerEntered += scene.contact(kRunner).entered;
        wallHit = scene.contact(kWallEntity).hit.isNull() ? wallHit : scene.contact(kWallEntity).hit;
    }
    // Through the ghost unaware, through the coin and told of it once on
    // each side, and stopped by the wall.
    RAWFRAME_EXPECT(ghostTouches == 0);
    RAWFRAME_EXPECT(coinEntered == 1 && runnerEntered == 1);
    RAWFRAME_EXPECT(wallHit == kRunner && scene.pose(kRunner).x < 6.5);
    // A body of a class the document does not declare is not made.
    RAWFRAME_EXPECT(scene.physics->statistics().bodiesRefused == 1 && scene.physics->statistics().bodiesMade == 4);

    // A document that is not well formed is refused, as rawframe.collision
    // says.
    const auto kMade = Physics2D::create({.collision = {.classes = {{kPlayer, "player"}, {kPlayer, "again"}}}});
    RAWFRAME_EXPECT(!kMade.has_value() &&
                    kMade.error().code() == physics::code(physics::PhysicsError::InvalidDocument));
}

RAWFRAME_TEST(ARayCastBackInTimeFindsWhereBodiesWere) {
    Scene scene{{.gravityY = 0, .historyTicks = 16}};
    const world::EntityHandle kTarget = scene.body(kCrate, {.x = 0, .y = 0}, {.x = 6});
    std::vector<double> xAt;
    for (int tick = 0; tick < 30; ++tick) {
        scene.run(1);
        xAt.push_back(scene.pose(kTarget).x);
    }
    const double kThen = xAt[20];
    // Where it was at tick 20 it is no longer: a ray there now misses, and
    // cast back to tick 20 it hits, with the hit where the body was.
    RAWFRAME_EXPECT(!scene.physics->castRay(kThen, 5, 0, -10, kEveryClass).hit);
    const RayHit2D kBack = scene.physics->castRayAt(kThen, 5, 0, -10, {.base = 20, .fraction = 0}, kEveryClass);
    RAWFRAME_EXPECT(kBack.hit && kBack.entity == kTarget && !kBack.discontinuous);
    RAWFRAME_EXPECT(std::abs(kBack.x - kThen) < 1e-9 && std::abs(kBack.y - 0.5) < 1e-6 && kBack.normalY > 0.99F);
    // Between two ticks, as a client shows it: half way from 20 to 21, a
    // ray just past the right edge at 20 hits, and at 20 itself misses.
    const double kEdge = ((xAt[20] + xAt[21]) / 2) + 0.49;
    RAWFRAME_EXPECT(scene.physics->castRayAt(kEdge, 5, 0, -10, {.base = 20, .fraction = 32768}, kEveryClass).hit);
    RAWFRAME_EXPECT(!scene.physics->castRayAt(kEdge, 5, 0, -10, {.base = 20, .fraction = 0}, kEveryClass).hit);
    // Older than the history keeps: clamped to its oldest tick, and counted.
    const RayHit2D kOld = scene.physics->castRayAt(xAt[29 - 15], 5, 0, -10, {.base = 2, .fraction = 0}, kEveryClass);
    RAWFRAME_EXPECT(kOld.hit && kOld.entity == kTarget);
    RAWFRAME_EXPECT(scene.physics->statistics().rewindsClamped == 1 && scene.physics->statistics().raysRewound == 4);
    // A body made since has no trail back: tried where it is, and marked.
    const world::EntityHandle kLate = scene.body(kBall, {.x = -5, .y = 0});
    scene.run(1);
    const RayHit2D kNew = scene.physics->castRayAt(-5, 5, 0, -10, {.base = 20, .fraction = 0}, kEveryClass);
    RAWFRAME_EXPECT(kNew.hit && kNew.entity == kLate && kNew.discontinuous);
}

RAWFRAME_TEST(SettingsAndWorldsOutOfRangeAreRefused) {
    for (const Physics2DSettings& kSettings : {Physics2DSettings{.substeps = 0},
                                               Physics2DSettings{.bodyCapacity = 0},
                                               Physics2DSettings{.gravityY = std::nanf("")}}) {
        const auto kMade = Physics2D::create(kSettings);
        RAWFRAME_EXPECT(!kMade.has_value() && kMade.error().code() == code(Physics2DError::InvalidSettings));
    }
    // A World without the physics components.
    schema::RegistryBuilder builder;
    builder.add<Pose2D>();
    const auto kSchema = *builder.freeze();
    auto physics = *Physics2D::create({});
    std::vector<world::SystemDeclaration> declarations;
    RAWFRAME_EXPECT(!physics->declareSystems(*kSchema, declarations).has_value());
}

RAWFRAME_TEST(ACharacterRunsLandsAndStopsAtAWall) {
    Scene scene;
    const world::EntityHandle kFloor = scene.body(kGround, {.x = 0, .y = 0});
    scene.body(Body2D{.motion = static_cast<std::uint8_t>(physics::Motion::Static),
                      .shape = static_cast<std::uint8_t>(Shape::Box),
                      .width = 0.5F,
                      .height = 2},
               {.x = 3, .y = 2.5});
    // Dropped from a meter up, running right at 4 m/s.
    const world::EntityHandle kRunner = scene.character({.x = 0, .y = 2.2});
    bool landed = false;
    for (int tick = 0; tick < 120; ++tick) {
        scene.walk(kRunner, 4);
        landed = landed || scene.character(kRunner).ground == static_cast<std::uint8_t>(physics::Ground::Grounded);
        // Never into the floor or the wall.
        RAWFRAME_EXPECT(scene.pose(kRunner).y > 1.2 - 0.001 && scene.pose(kRunner).x < 2.2 + 0.001);
    }
    RAWFRAME_EXPECT(landed);
    // Stood on the floor, pressed against the wall, still.
    const Pose2D& kAt = scene.pose(kRunner);
    RAWFRAME_EXPECT(std::abs(kAt.x - 2.2) < 0.02 && std::abs(kAt.y - 1.2) < 0.02);
    const Character2D& kOn = scene.character(kRunner);
    RAWFRAME_EXPECT(kOn.ground == static_cast<std::uint8_t>(physics::Ground::Grounded));
    RAWFRAME_EXPECT(scene.physics->castRay(kAt.x, kAt.y - 0.75, 0, -1, kEveryClass).entity == kFloor);
    RAWFRAME_EXPECT(kOn.groundNormalY > 0.99F);
    RAWFRAME_EXPECT(std::abs(scene.velocity(kRunner).x) < 0.05F && std::abs(scene.velocity(kRunner).y) < 0.05F);
    RAWFRAME_EXPECT(scene.physics->statistics().characterMoves == 120);
}

RAWFRAME_TEST(ACharacterSlidesDownASteepSlopeAndStandsOnAGentleOne) {
    // Two slopes rising to the right, of 60 and of 20 degrees.
    const auto kSlope = [](Scene& scene, double x, float degrees) {
        const float kRadians = degrees * 3.14159265F / 180;
        scene.body(Body2D{.motion = static_cast<std::uint8_t>(physics::Motion::Static),
                          .shape = static_cast<std::uint8_t>(Shape::Box),
                          .width = 5,
                          .height = 0.5F},
                   {.x = x, .y = 0, .c = std::cos(kRadians), .s = std::sin(kRadians)});
    };
    Scene steep;
    kSlope(steep, 0, 60);
    const world::EntityHandle kSlider = steep.character({.x = -0.5, .y = 2.5});
    bool slid = false;
    for (int tick = 0; tick < 60; ++tick) {
        steep.walk(kSlider, 0);
        slid = slid || steep.character(kSlider).ground == static_cast<std::uint8_t>(physics::Ground::Sliding);
        RAWFRAME_EXPECT(steep.character(kSlider).ground != static_cast<std::uint8_t>(physics::Ground::Grounded));
    }
    // Down the slope, which pushes it left and up.
    RAWFRAME_EXPECT(slid && steep.pose(kSlider).x < -1);
    RAWFRAME_EXPECT(steep.character(kSlider).groundNormalX < -0.8F);

    Scene gentle;
    kSlope(gentle, 0, 20);
    const world::EntityHandle kStander = gentle.character({.x = 0, .y = 2});
    for (int tick = 0; tick < 60; ++tick) {
        gentle.walk(kStander, 0);
    }
    const Pose2D kStood = gentle.pose(kStander);
    gentle.walk(kStander, 0);
    RAWFRAME_EXPECT(gentle.character(kStander).ground == static_cast<std::uint8_t>(physics::Ground::Grounded));
    RAWFRAME_EXPECT(std::abs(gentle.pose(kStander).x - kStood.x) < 1e-6);
}

RAWFRAME_TEST(ACharacterRunningDownhillSnapsToTheSlope) {
    // Downhill at 6 m/s, a tenth of a meter a tick, leaves a 20 degree slope
    // a little each tick; a character that snaps stays on it.
    const auto kRun = [](float snap) {
        Scene scene;
        const float kRadians = 20 * 3.14159265F / 180;
        scene.body(Body2D{.motion = static_cast<std::uint8_t>(physics::Motion::Static),
                          .shape = static_cast<std::uint8_t>(Shape::Box),
                          .width = 8,
                          .height = 0.5F},
                   {.c = std::cos(kRadians), .s = std::sin(kRadians)});
        const world::EntityHandle kRunner = scene.character({.x = 4, .y = 3.5}, snap);
        for (int tick = 0; tick < 60; ++tick) {
            scene.walk(kRunner, 0);
        }
        int airborne = 0;
        for (int tick = 0; tick < 40; ++tick) {
            scene.walk(kRunner, -6);
            airborne += scene.character(kRunner).ground == static_cast<std::uint8_t>(physics::Ground::Airborne) ? 1 : 0;
        }
        return airborne;
    };
    RAWFRAME_EXPECT(kRun(0.2F) == 0);
    RAWFRAME_EXPECT(kRun(0) > 5);
}

RAWFRAME_TEST(CharactersPassThroughEachOtherAndBlockNothingOfTheirOwn) {
    Scene scene;
    scene.body(kGround, {.x = 0, .y = 0});
    const world::EntityHandle kLeft = scene.character({.x = -2, .y = 1.2});
    const world::EntityHandle kRight = scene.character({.x = 2, .y = 1.2});
    for (int tick = 0; tick < 90; ++tick) {
        Velocity2D& right = scene.velocity(kRight);
        right = Velocity2D{.x = -3};
        scene.walk(kLeft, 3);
    }
    RAWFRAME_EXPECT(scene.pose(kLeft).x > 2 && scene.pose(kRight).x < -2);
    RAWFRAME_EXPECT(scene.character(kRight).ground == static_cast<std::uint8_t>(physics::Ground::Grounded));
}

RAWFRAME_TEST(ARayAmongOneClassSeesThroughTheRest) {
    constexpr std::uint64_t kPlayer = 0x51;
    constexpr std::uint64_t kUndeclared = 0x52;
    Scene scene({.gravityY = 0, .historyTicks = 8, .collision = {.classes = {{kPlayer, "player"}}}});
    // A wall of no class between the origin and a player moving up.
    const world::EntityHandle kWall = scene.body(Body2D{.motion = static_cast<std::uint8_t>(physics::Motion::Static),
                                                        .shape = static_cast<std::uint8_t>(Shape::Box),
                                                        .width = 0.5F,
                                                        .height = 2},
                                                 {.x = 2});
    const world::EntityHandle kTarget =
        scene.body(Body2D{.motion = static_cast<std::uint8_t>(physics::Motion::Kinematic),
                          .shape = static_cast<std::uint8_t>(Shape::Circle),
                          .collisionClass = kPlayer,
                          .width = 0.5F},
                   {.x = 5},
                   {.y = 3});
    scene.run(10);
    RAWFRAME_EXPECT(scene.physics->castRay(0, 0.5, 10, 0, kEveryClass).entity == kWall);
    // Where the player is now, and not where it was.
    const double kNowY = scene.pose(kTarget).y;
    const RayHit2D kNow = scene.physics->castRay(0, kNowY, 10, 0, kPlayer);
    RAWFRAME_EXPECT(kNow.entity == kTarget && std::abs(kNow.x - 4.5) < 0.001);
    RAWFRAME_EXPECT(!scene.physics->castRay(0, -0.3, 10, 0, kPlayer).hit);
    RAWFRAME_EXPECT(!scene.physics->castRay(0, kNowY, 10, 0, kUndeclared).hit);
    // Back to tick 2, when it was about the origin.
    RAWFRAME_EXPECT(scene.physics->castRayAt(0, -0.3, 10, 0, {.base = 2, .fraction = 0}, kPlayer).entity == kTarget);
    RAWFRAME_EXPECT(scene.physics->castRayAt(0, -0.3, 10, 0, {.base = 2, .fraction = 0}, kEveryClass).entity == kWall);
    RAWFRAME_EXPECT(!scene.physics->castRayAt(0, -0.3, 10, 0, {.base = 2, .fraction = 0}, kUndeclared).hit);
}
