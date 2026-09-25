#include "rawframe/physics/errors.h"
#include "rawframe/physics/ground.h"
#include "rawframe/physics3d/components.h"
#include "rawframe/physics3d/errors.h"
#include "rawframe/physics3d/physics.h"
#include "rawframe/test/test.h"
#include "rawframe/world/schedule.h"
#include "rawframe/world/world.h"

#include <cmath>
#include <cstring>
#include <optional>
#include <vector>

using namespace rawframe;
using namespace rawframe::physics3d;
using physics::Motion;

namespace {

std::shared_ptr<const schema::SchemaRegistry> registry() {
    schema::RegistryBuilder builder;
    builder.add<Body3D>()
        .add<Pose3D>()
        .add<Velocity3D>()
        .add<Impulse3D>()
        .add<Contact3D>()
        .add<Character3D>()
        .add<Mesh3D>();
    return *builder.freeze();
}

/// A World with 3D physics, ticked at 60 Hz.
struct Scene {
    std::shared_ptr<const schema::SchemaRegistry> schema = registry();
    world::World world{schema};
    std::unique_ptr<Physics3D> physics;
    std::optional<world::Schedule> schedule;
    world::TickIndex tick;

    explicit Scene(const Physics3DSettings& settings = {}) {
        physics = *Physics3D::create(settings);
        std::vector<world::SystemDeclaration> declarations;
        RAWFRAME_EXPECT(physics->declareSystems(*schema, declarations).has_value());
        schedule.emplace(*world::Schedule::compile(declarations, *schema));
    }

    world::EntityHandle body(const Body3D& body, const Pose3D& pose, const Velocity3D& velocity = {}) {
        const world::EntityHandle kEntity = *world.create();
        RAWFRAME_EXPECT(world.insert(kEntity, *schema->key<Body3D>(), body).has_value());
        RAWFRAME_EXPECT(world.insert(kEntity, *schema->key<Pose3D>(), pose).has_value());
        RAWFRAME_EXPECT(world.insert(kEntity, *schema->key<Velocity3D>(), velocity).has_value());
        RAWFRAME_EXPECT(world.insert(kEntity, *schema->key<Impulse3D>(), Impulse3D{}).has_value());
        RAWFRAME_EXPECT(world.insert(kEntity, *schema->key<Contact3D>(), Contact3D{}).has_value());
        return kEntity;
    }

    world::EntityHandle character(const Pose3D& pose, float snap = 0) {
        const world::EntityHandle kEntity = body(Body3D{.motion = static_cast<std::uint8_t>(Motion::Kinematic),
                                                        .shape = static_cast<std::uint8_t>(Shape::Capsule),
                                                        .fixedRotation = true,
                                                        .width = 0.3F,
                                                        .height = 0.4F},
                                                 pose);
        RAWFRAME_EXPECT(
            world.insert(kEntity, *schema->key<Character3D>(), Character3D{.groundNormal = 0.7F, .snap = snap})
                .has_value());
        return kEntity;
    }

    /// A platformer's gameplay for one tick: run at `run` meters a second
    /// along x, and fall under gravity unless on ground.
    void walk(world::EntityHandle entity, float run) {
        Velocity3D& wanted = velocity(entity);
        const bool kGrounded = characterOf(entity).ground == static_cast<std::uint8_t>(physics::Ground::Grounded);
        wanted = Velocity3D{.x = run, .y = kGrounded ? 0 : wanted.y - (20.0F / 60)};
        this->run(1);
    }

    Character3D& characterOf(world::EntityHandle entity) {
        return *world.get(entity, *schema->key<Character3D>());
    }

    void run(int ticks) {
        for (int index = 0; index < ticks; ++index) {
            RAWFRAME_EXPECT(schedule->runTick(world, tick, *world::TickRate::of(60)).has_value());
        }
    }

    Pose3D& pose(world::EntityHandle entity) {
        return *world.get(entity, *schema->key<Pose3D>());
    }
    Velocity3D& velocity(world::EntityHandle entity) {
        return *world.get(entity, *schema->key<Velocity3D>());
    }
    Impulse3D& impulse(world::EntityHandle entity) {
        return *world.get(entity, *schema->key<Impulse3D>());
    }
    Contact3D& contact(world::EntityHandle entity) {
        return *world.get(entity, *schema->key<Contact3D>());
    }
};

constexpr Body3D kGround{.motion = static_cast<std::uint8_t>(Motion::Static),
                         .shape = static_cast<std::uint8_t>(Shape::Box),
                         .width = 10,
                         .height = 0.5F,
                         .depth = 10,
                         .friction = 0.6F};

constexpr Body3D kCrate{.motion = static_cast<std::uint8_t>(Motion::Dynamic),
                        .shape = static_cast<std::uint8_t>(Shape::Box),
                        .width = 0.5F,
                        .height = 0.5F,
                        .depth = 0.5F,
                        .density = 1,
                        .friction = 0.6F};

constexpr Body3D kBall{.motion = static_cast<std::uint8_t>(Motion::Dynamic),
                       .shape = static_cast<std::uint8_t>(Shape::Sphere),
                       .width = 0.25F,
                       .density = 1,
                       .friction = 0.3F,
                       .restitution = 0.2F};

} // namespace

RAWFRAME_TEST(ACrateFallsAndRestsOnTheGround) {
    Scene scene;
    scene.body(kGround, {});
    const world::EntityHandle kCrateEntity = scene.body(kCrate, {.y = 5});
    scene.run(1);
    RAWFRAME_EXPECT(scene.velocity(kCrateEntity).y < 0 && scene.pose(kCrateEntity).y < 5);
    scene.run(239);
    const Pose3D& kRest = scene.pose(kCrateEntity);
    // A box landing flat on a box settles within a centimeter or so of
    // where it fell.
    RAWFRAME_EXPECT(std::abs(kRest.y - 1.0) < 0.02 && std::abs(kRest.x) < 0.02 && std::abs(kRest.z) < 0.02);
    RAWFRAME_EXPECT(std::abs(scene.velocity(kCrateEntity).y) < 0.01);
    // Unrotated, as it was dropped.
    RAWFRAME_EXPECT(std::abs(kRest.qw - 1.0F) < 0.001F);
    const Physics3DStatistics kStatistics = scene.physics->statistics();
    RAWFRAME_EXPECT(kStatistics.steps == 240 && kStatistics.bodiesMade == 2 && kStatistics.bodiesRefused == 0);
}

RAWFRAME_TEST(TheSameWorldAndWritesGiveTheSameBits) {
    const auto kPlay = [](Scene& scene) {
        scene.body(kGround, {});
        std::vector<world::EntityHandle> balls;
        for (int index = 0; index < 27; ++index) {
            balls.push_back(scene.body(
                kBall, {.x = (index % 3) * 0.3 - 0.3, .y = 2.0 + (index / 9) * 0.6, .z = ((index / 3) % 3) * 0.3}));
        }
        for (int tick = 0; tick < 120; ++tick) {
            if (tick % 30 == 0) {
                scene.impulse(balls[static_cast<std::size_t>(tick / 30)]).x = 0.05F;
            }
            scene.run(1);
        }
        std::vector<Pose3D> poses;
        for (const world::EntityHandle kBallEntity : balls) {
            poses.push_back(scene.pose(kBallEntity));
        }
        return std::pair{poses, scene.physics->digest()};
    };
    Scene first;
    Scene second;
    const auto kFirst = kPlay(first);
    const auto kSecond = kPlay(second);
    RAWFRAME_EXPECT(kFirst.second == kSecond.second);
    RAWFRAME_EXPECT(kFirst.first.size() == kSecond.first.size() &&
                    std::memcmp(kFirst.first.data(), kSecond.first.data(), kFirst.first.size() * sizeof(Pose3D)) == 0);
    // They moved: a pile spread by the pushes, not a still picture.
    RAWFRAME_EXPECT(kFirst.first.front().y < 2.0);
}

RAWFRAME_TEST(GameplayWritesAreCommands) {
    Scene scene{{.gravityY = 0}};
    const world::EntityHandle kCrateEntity = scene.body(kCrate, {});
    scene.run(1);
    // A pose written is a teleport; a velocity written is set; an impulse
    // is applied once and cleared.
    scene.pose(kCrateEntity) = Pose3D{.x = 3, .y = 1, .z = -2, .qw = 1};
    scene.run(1);
    RAWFRAME_EXPECT(std::abs(scene.pose(kCrateEntity).x - 3) < 1e-9 && std::abs(scene.pose(kCrateEntity).z + 2) < 1e-9);
    scene.velocity(kCrateEntity) = Velocity3D{.z = 6};
    scene.run(10);
    RAWFRAME_EXPECT(std::abs(scene.pose(kCrateEntity).z - (-2 + 1.0)) < 0.01);
    scene.impulse(kCrateEntity) = Impulse3D{.y = 1};
    scene.run(1);
    RAWFRAME_EXPECT(scene.velocity(kCrateEntity).y > 0.9F && scene.impulse(kCrateEntity).y == 0);
    const Physics3DStatistics kStatistics = scene.physics->statistics();
    RAWFRAME_EXPECT(kStatistics.teleports == 1 && kStatistics.velocitiesSet == 1 && kStatistics.impulses == 1);
    // A body that cannot be made is refused, and made once it can be.
    const world::EntityHandle kFlat = scene.body(Body3D{.motion = static_cast<std::uint8_t>(Motion::Dynamic),
                                                        .shape = static_cast<std::uint8_t>(Shape::Box),
                                                        .width = 1,
                                                        .height = 1,
                                                        .density = 1},
                                                 {.x = 10});
    scene.run(1);
    RAWFRAME_EXPECT(scene.physics->statistics().bodiesRefused == 1);
    scene.world.get(kFlat, *scene.schema->key<Body3D>())->depth = 1;
    scene.run(1);
    // The crate, made again by its teleport, and now this one.
    RAWFRAME_EXPECT(scene.physics->statistics().bodiesMade == 3);
}

RAWFRAME_TEST(ABallThatLandsIsToldWhatItHit) {
    Scene scene;
    const world::EntityHandle kGroundEntity = scene.body(kGround, {});
    const world::EntityHandle kBallEntity = scene.body(kBall, {.y = 3});
    bool landed = false;
    for (int tick = 0; tick < 120 && !landed; ++tick) {
        scene.run(1);
        landed = scene.contact(kBallEntity).began > 0;
    }
    RAWFRAME_EXPECT(landed);
    const Contact3D kBallContact = scene.contact(kBallEntity);
    const Contact3D kGroundContact = scene.contact(kGroundEntity);
    RAWFRAME_EXPECT(kBallContact.hit == kGroundEntity && kBallContact.hitSpeed > 1);
    // The normal points from each toward the other.
    RAWFRAME_EXPECT(kBallContact.hitNormalY < -0.9F && kGroundContact.hitNormalY > 0.9F);
    RAWFRAME_EXPECT(kGroundContact.hit == kBallEntity && kGroundContact.began == 1);
    scene.run(60);
    const Contact3D kAfter = scene.contact(kBallEntity);
    RAWFRAME_EXPECT(kAfter.began == 0 && kAfter.touching == 1 && kAfter.hit.isNull());
}

RAWFRAME_TEST(ASensorIsEnteredAndLeftOnce) {
    Scene scene{{.gravityY = 0}};
    const world::EntityHandle kZone = scene.body(Body3D{.motion = static_cast<std::uint8_t>(Motion::Static),
                                                        .shape = static_cast<std::uint8_t>(Shape::Box),
                                                        .sensor = true,
                                                        .width = 1,
                                                        .height = 1,
                                                        .depth = 1},
                                                 {});
    const world::EntityHandle kBallEntity = scene.body(kBall, {.x = -3}, {.x = 6});
    int entered = 0;
    int exited = 0;
    int inside = 0;
    world::EntityHandle visitor;
    for (int tick = 0; tick < 90; ++tick) {
        scene.run(1);
        const Contact3D& zone = scene.contact(kZone);
        entered += static_cast<int>(zone.entered);
        exited += static_cast<int>(zone.exited);
        inside += zone.overlapping > 0 ? 1 : 0;
        visitor = zone.visitor.isNull() ? visitor : zone.visitor;
    }
    RAWFRAME_EXPECT(entered == 1 && exited == 1 && visitor == kBallEntity);
    // Through a zone two meters across at 6 m/s: about twenty ticks inside.
    RAWFRAME_EXPECT(inside > 15 && inside < 30);
    // A sensor pushes nothing.
    RAWFRAME_EXPECT(std::abs(scene.velocity(kBallEntity).x - 6) < 0.001F);
}

RAWFRAME_TEST(RaysFindBodiesNowAndThen) {
    constexpr std::uint64_t kTargets = 0x71;
    Scene scene{{.gravityY = 0, .historyTicks = 16, .collision = {.classes = {{kTargets, "targets"}}}}};
    const world::EntityHandle kWall = scene.body(Body3D{.motion = static_cast<std::uint8_t>(Motion::Static),
                                                        .shape = static_cast<std::uint8_t>(Shape::Box),
                                                        .width = 0.5F,
                                                        .height = 2,
                                                        .depth = 2},
                                                 {.x = 2});
    const world::EntityHandle kTarget = scene.body(Body3D{.motion = static_cast<std::uint8_t>(Motion::Kinematic),
                                                          .shape = static_cast<std::uint8_t>(Shape::Sphere),
                                                          .collisionClass = kTargets,
                                                          .width = 0.5F},
                                                   {.x = 5},
                                                   {.z = 6});
    std::vector<double> zAt;
    for (int tick = 0; tick < 12; ++tick) {
        scene.run(1);
        zAt.push_back(scene.pose(kTarget).z);
    }
    // Along x: the wall first, and the target behind it only among targets.
    const RayHit3D kWallHit = scene.physics->castRay(0, 0, 0, 10, 0, 0, physics::kEveryClass);
    RAWFRAME_EXPECT(kWallHit.hit && kWallHit.entity == kWall && std::abs(kWallHit.x - 1.5) < 0.001 &&
                    kWallHit.normalX < -0.99F);
    const double kNow = scene.pose(kTarget).z;
    const RayHit3D kTargetHit = scene.physics->castRay(0, 0, kNow, 10, 0, 0, kTargets);
    RAWFRAME_EXPECT(kTargetHit.entity == kTarget && std::abs(kTargetHit.x - 4.5) < 0.001);
    RAWFRAME_EXPECT(!scene.physics->castRay(0, 0, zAt[2], 10, 0, 0, kTargets).hit);
    RAWFRAME_EXPECT(!scene.physics->castRay(0, 0, kNow, 10, 0, 0, 0x72).hit);
    // Cast back to tick 2, where the target was then, and to its surface
    // there, not here.
    const RayHit3D kBack = scene.physics->castRayAt(0, 0, zAt[2], 10, 0, 0, {.base = 2, .fraction = 0}, kTargets);
    RAWFRAME_EXPECT(kBack.hit && kBack.entity == kTarget && !kBack.discontinuous);
    RAWFRAME_EXPECT(std::abs(kBack.x - 4.5) < 0.001 && std::abs(kBack.z - zAt[2]) < 0.001);
    // Half way to tick 3: a ray just past the target's reach then misses at
    // tick 2 and hits half a tick on.
    const double kEdge = zAt[2] + 0.5 + ((zAt[3] - zAt[2]) / 4);
    RAWFRAME_EXPECT(scene.physics->castRayAt(0, 0, kEdge, 10, 0, 0, {.base = 2, .fraction = 32768}, kTargets).hit);
    RAWFRAME_EXPECT(!scene.physics->castRayAt(0, 0, kEdge, 10, 0, 0, {.base = 2, .fraction = 0}, kTargets).hit);
    // Earlier than the history keeps: clamped, and counted.
    static_cast<void>(scene.physics->castRayAt(0, 0, 0, 10, 0, 0, {.base = 0, .fraction = 0}, kTargets));
    static_cast<void>(scene.physics->castRayAt(0, 0, 0, 10, 0, 0, {.base = 100, .fraction = 0}, kTargets));
    RAWFRAME_EXPECT(scene.physics->statistics().rewindsClamped == 1);
}

RAWFRAME_TEST(SettingsAndDocumentsAreChecked) {
    RAWFRAME_EXPECT(!Physics3D::create({.substeps = 0}).has_value());
    RAWFRAME_EXPECT(!Physics3D::create({.historyTicks = 2048}).has_value());
    const auto kBad = Physics3D::create({.collision = {.classes = {{0x1, "one"}, {0x1, "again"}}}});
    RAWFRAME_EXPECT(!kBad.has_value() && kBad.error().code() == physics::code(physics::PhysicsError::InvalidDocument));
}

namespace {

/// A static box tilted about z by `degrees`, rising toward +x.
Pose3D tilted(double x, float degrees) {
    const float kHalf = degrees * 3.14159265F / 360;
    return Pose3D{.x = x, .qz = std::sin(kHalf), .qw = std::cos(kHalf)};
}

constexpr Body3D kSlab{.motion = static_cast<std::uint8_t>(Motion::Static),
                       .shape = static_cast<std::uint8_t>(Shape::Box),
                       .width = 5,
                       .height = 0.5F,
                       .depth = 5};

} // namespace

RAWFRAME_TEST(ACharacterRunsLandsAndStopsAtAWall) {
    Scene scene;
    scene.body(kGround, {});
    scene.body(Body3D{.motion = static_cast<std::uint8_t>(Motion::Static),
                      .shape = static_cast<std::uint8_t>(Shape::Box),
                      .width = 0.5F,
                      .height = 2,
                      .depth = 5},
               {.x = 3, .y = 2.5});
    const world::EntityHandle kRunner = scene.character({.y = 2.2});
    bool landed = false;
    for (int tick = 0; tick < 120; ++tick) {
        scene.walk(kRunner, 4);
        landed = landed || scene.characterOf(kRunner).ground == static_cast<std::uint8_t>(physics::Ground::Grounded);
        RAWFRAME_EXPECT(scene.pose(kRunner).y > 1.2 - 0.001 && scene.pose(kRunner).x < 2.2 + 0.001);
    }
    RAWFRAME_EXPECT(landed);
    const Pose3D& kAt = scene.pose(kRunner);
    RAWFRAME_EXPECT(std::abs(kAt.x - 2.2) < 0.02 && std::abs(kAt.y - 1.2) < 0.02 && std::abs(kAt.z) < 0.001);
    const Character3D& kOn = scene.characterOf(kRunner);
    RAWFRAME_EXPECT(kOn.ground == static_cast<std::uint8_t>(physics::Ground::Grounded) && kOn.groundNormalY > 0.99F);
    RAWFRAME_EXPECT(std::abs(scene.velocity(kRunner).x) < 0.05F && std::abs(scene.velocity(kRunner).y) < 0.05F);
    RAWFRAME_EXPECT(scene.physics->statistics().characterMoves == 120);
}

RAWFRAME_TEST(ACharacterSlidesDownASteepSlopeAndStandsOnAGentleOne) {
    Scene steep;
    steep.body(kSlab, tilted(0, 60));
    const world::EntityHandle kSlider = steep.character({.x = -0.5, .y = 2.5});
    bool slid = false;
    for (int tick = 0; tick < 60; ++tick) {
        steep.walk(kSlider, 0);
        slid = slid || steep.characterOf(kSlider).ground == static_cast<std::uint8_t>(physics::Ground::Sliding);
        RAWFRAME_EXPECT(steep.characterOf(kSlider).ground != static_cast<std::uint8_t>(physics::Ground::Grounded));
    }
    RAWFRAME_EXPECT(slid && steep.pose(kSlider).x < -1);
    RAWFRAME_EXPECT(steep.characterOf(kSlider).groundNormalX < -0.8F);

    Scene gentle;
    gentle.body(kSlab, tilted(0, 20));
    const world::EntityHandle kStander = gentle.character({.y = 2});
    for (int tick = 0; tick < 60; ++tick) {
        gentle.walk(kStander, 0);
    }
    const Pose3D kStood = gentle.pose(kStander);
    gentle.walk(kStander, 0);
    RAWFRAME_EXPECT(gentle.characterOf(kStander).ground == static_cast<std::uint8_t>(physics::Ground::Grounded));
    RAWFRAME_EXPECT(std::abs(gentle.pose(kStander).x - kStood.x) < 1e-6);
}

RAWFRAME_TEST(ACharacterRunningDownhillSnapsToTheSlope) {
    const auto kRun = [](float snap) {
        Scene scene;
        Body3D slope = kSlab;
        slope.width = 8;
        scene.body(slope, tilted(0, 20));
        const world::EntityHandle kRunner = scene.character({.x = 4, .y = 3.5}, snap);
        for (int tick = 0; tick < 60; ++tick) {
            scene.walk(kRunner, 0);
        }
        int airborne = 0;
        for (int tick = 0; tick < 40; ++tick) {
            scene.walk(kRunner, -6);
            airborne +=
                scene.characterOf(kRunner).ground == static_cast<std::uint8_t>(physics::Ground::Airborne) ? 1 : 0;
        }
        return airborne;
    };
    RAWFRAME_EXPECT(kRun(0.2F) == 0);
    RAWFRAME_EXPECT(kRun(0) > 5);
}

RAWFRAME_TEST(CharactersPassThroughEachOther) {
    Scene scene;
    scene.body(kGround, {});
    const world::EntityHandle kLeft = scene.character({.x = -2, .y = 1.2});
    const world::EntityHandle kRight = scene.character({.x = 2, .y = 1.2});
    for (int tick = 0; tick < 90; ++tick) {
        scene.velocity(kRight) = Velocity3D{.x = -3};
        scene.walk(kLeft, 3);
    }
    RAWFRAME_EXPECT(scene.pose(kLeft).x > 2 && scene.pose(kRight).x < -2);
    RAWFRAME_EXPECT(scene.characterOf(kRight).ground == static_cast<std::uint8_t>(physics::Ground::Grounded));
}

RAWFRAME_TEST(ASphereSweptAndASphereOverlappedFindTheirBodies) {
    constexpr std::uint64_t kMarked = 0x81;
    Scene scene{{.gravityY = 0, .collision = {.classes = {{kMarked, "marked"}}}}};
    const Body3D kBlock{.motion = static_cast<std::uint8_t>(Motion::Static),
                        .shape = static_cast<std::uint8_t>(Shape::Box),
                        .width = 0.5F,
                        .height = 0.5F,
                        .depth = 0.5F};
    Body3D marked = kBlock;
    marked.collisionClass = kMarked;
    const world::EntityHandle kFirst = scene.body(kBlock, {.x = 2});
    const world::EntityHandle kSecond = scene.body(marked, {.x = 5});
    scene.run(1);
    // A sphere of a quarter meter just over the first block grazes its top.
    const RayHit3D kSwept = scene.physics->castSphere(-2, 0.7, 0, 0.25F, 10, 0, 0, physics::kEveryClass);
    RAWFRAME_EXPECT(kSwept.hit && kSwept.entity == kFirst && !kSwept.inside && kSwept.fraction < 0.5F);
    RAWFRAME_EXPECT(scene.physics->castSphere(-2, 0.7, 0, 0.25F, 10, 0, 0, kMarked).entity == kSecond);
    // Started inside: met there, with no normal.
    const RayHit3D kInside = scene.physics->castSphere(2, 0, 0, 0.1F, 1, 0, 0, physics::kEveryClass);
    RAWFRAME_EXPECT(kInside.hit && kInside.inside && kInside.entity == kFirst && kInside.fraction == 0);
    std::vector<world::EntityHandle> found;
    scene.physics->overlapSphere(3.5, 0, 0, 1.2F, physics::kEveryClass, found);
    RAWFRAME_EXPECT(found.size() == 2 && found[0] == kFirst && found[1] == kSecond);
    scene.physics->overlapSphere(3.5, 0, 0, 0.8F, physics::kEveryClass, found);
    RAWFRAME_EXPECT(found.empty());
    scene.physics->overlapSphere(3.5, 0, 0, 1.2F, kMarked, found);
    RAWFRAME_EXPECT(found.size() == 1 && found[0] == kSecond);
    // Many at once, more than the first room the query makes.
    for (int index = 0; index < 40; ++index) {
        scene.body(kBlock, {.x = 20 + (index % 8) * 1.1, .z = (index / 8) * 1.1});
    }
    scene.run(1);
    scene.physics->overlapSphere(24, 0, 2.2, 10, physics::kEveryClass, found);
    RAWFRAME_EXPECT(found.size() == 40);
}

namespace {

/// A flat square `size` meters across on y = 0, of `cells` by `cells`
/// quads, facing up.
std::shared_ptr<const mesh::Mesh> grid(std::uint32_t cells, float size) {
    mesh::Mesh out;
    const float kStep = size / static_cast<float>(cells);
    for (std::uint32_t z = 0; z <= cells; ++z) {
        for (std::uint32_t x = 0; x <= cells; ++x) {
            out.positions.push_back(
                {(-size / 2) + (static_cast<float>(x) * kStep), 0.0F, (-size / 2) + (static_cast<float>(z) * kStep)});
        }
    }
    for (std::uint32_t z = 0; z < cells; ++z) {
        for (std::uint32_t x = 0; x < cells; ++x) {
            const std::uint32_t kA = (z * (cells + 1)) + x;
            const std::uint32_t kC = kA + cells + 1;
            out.indices.insert(out.indices.end(), {kA, kC, kA + 1, kA + 1, kC, kC + 1});
        }
    }
    out.parts.push_back({.firstIndex = 0, .indexCount = static_cast<std::uint32_t>(out.indices.size())});
    return std::make_shared<const mesh::Mesh>(std::move(out));
}

constexpr std::uint64_t kFloorMesh = 0xF1;

constexpr Body3D kMeshBody{.motion = static_cast<std::uint8_t>(Motion::Static),
                           .shape = static_cast<std::uint8_t>(Shape::Mesh),
                           .friction = 0.6F};

world::EntityHandle meshBody(Scene& scene, const Pose3D& pose, std::uint64_t mesh, const Body3D& body = kMeshBody) {
    const world::EntityHandle kEntity = scene.body(body, pose);
    RAWFRAME_EXPECT(scene.world.insert(kEntity, *scene.schema->key<Mesh3D>(), Mesh3D{.mesh = mesh}).has_value());
    return kEntity;
}

} // namespace

RAWFRAME_TEST(AMeshHoldsWhatFallsOnIt) {
    const auto kPlay = [](Scene& scene) {
        const world::EntityHandle kFloor = meshBody(scene, {.y = 1}, kFloorMesh);
        const world::EntityHandle kBallEntity = scene.body(kBall, {.x = 0.3, .y = 4, .z = -0.2});
        scene.run(240);
        return std::tuple{kFloor, kBallEntity, scene.physics->digest()};
    };
    const Physics3DSettings kSettings{.meshes = {{.id = kFloorMesh, .mesh = grid(8, 10)}}};
    Scene scene{kSettings};
    const auto [kFloor, kBallEntity, kDigest] = kPlay(scene);
    // At the body's pose, not the mesh's origin.
    RAWFRAME_EXPECT(std::abs(scene.pose(kBallEntity).y - 1.25) < 0.02);
    const RayHit3D kDown = scene.physics->castRay(2, 3, 2, 0, -5, 0, physics::kEveryClass);
    RAWFRAME_EXPECT(kDown.hit && kDown.entity == kFloor && std::abs(kDown.y - 1) < 0.001 && kDown.normalY > 0.99F);
    // From below, the back of the triangles: nothing.
    RAWFRAME_EXPECT(!scene.physics->castRay(2, 0, 2, 0, 5, 0, physics::kEveryClass).hit);
    RAWFRAME_EXPECT(scene.physics->statistics().bodiesMade == 2);
    Scene again{kSettings};
    RAWFRAME_EXPECT(std::get<2>(kPlay(again)) == kDigest);
}

RAWFRAME_TEST(AMeshPastOneShapeIsMadeOfPieces) {
    // 80,000 triangles: more than Maul3D takes in one shape.
    Scene scene{{.meshes = {{.id = kFloorMesh, .mesh = grid(200, 100)}}}};
    const world::EntityHandle kFloor = meshBody(scene, {}, kFloorMesh);
    const world::EntityHandle kNear = scene.body(kBall, {.x = -45, .y = 2, .z = -45});
    const world::EntityHandle kFar = scene.body(kBall, {.x = 45, .y = 2, .z = 45});
    scene.run(180);
    RAWFRAME_EXPECT(std::abs(scene.pose(kNear).y - 0.25) < 0.02 && std::abs(scene.pose(kFar).y - 0.25) < 0.02);
    // Beside each ball, down to the piece under it.
    for (const double kAt : {-44.0, 44.0}) {
        const RayHit3D kHit = scene.physics->castRayAt(kAt, 3, kAt, 0, -5, 0, {.base = 179}, physics::kEveryClass);
        RAWFRAME_EXPECT(kHit.hit && kHit.entity == kFloor);
    }
}

RAWFRAME_TEST(AMeshBodyIsStaticAndNamesAKnownMesh) {
    Scene scene{{.meshes = {{.id = kFloorMesh, .mesh = grid(2, 4)}}}};
    Body3D moving = kMeshBody;
    moving.motion = static_cast<std::uint8_t>(Motion::Dynamic);
    static_cast<void>(meshBody(scene, {}, kFloorMesh, moving));
    const world::EntityHandle kUnknown = meshBody(scene, {}, 0xF2);
    static_cast<void>(scene.body(kMeshBody, {}));
    scene.run(1);
    RAWFRAME_EXPECT(scene.physics->statistics().bodiesRefused == 3 && scene.physics->statistics().bodiesMade == 0);
    // Named again, a mesh that is known: made.
    scene.world.get(kUnknown, *scene.schema->key<Mesh3D>())->mesh = kFloorMesh;
    scene.run(1);
    RAWFRAME_EXPECT(scene.physics->statistics().bodiesMade == 1);

    const auto kFlat = grid(2, 4);
    mesh::Mesh broken = *kFlat;
    broken.indices[0] = 99;
    for (const std::vector<BodyMesh>& kMeshes :
         {std::vector<BodyMesh>{{.id = 1, .mesh = kFlat}, {.id = 1, .mesh = kFlat}},
          std::vector<BodyMesh>{{.id = 0, .mesh = kFlat}},
          std::vector<BodyMesh>{{.id = 1, .mesh = nullptr}},
          std::vector<BodyMesh>{{.id = 1, .mesh = std::make_shared<const mesh::Mesh>(broken)}}}) {
        const auto kRefused = Physics3D::create({.meshes = kMeshes});
        RAWFRAME_EXPECT(!kRefused.has_value() && kRefused.error().code() == code(Physics3DError::InvalidSettings));
    }
}
