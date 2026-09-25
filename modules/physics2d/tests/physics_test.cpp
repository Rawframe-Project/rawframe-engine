#include "rawframe/physics2d/components.h"
#include "rawframe/physics2d/errors.h"
#include "rawframe/physics2d/physics.h"
#include "rawframe/test/test.h"
#include "rawframe/world/schedule.h"
#include "rawframe/world/world.h"

#include <cmath>
#include <cstring>
#include <optional>
#include <vector>

using namespace rawframe;
using namespace rawframe::physics2d;

namespace {

std::shared_ptr<const schema::SchemaRegistry> registry() {
    schema::RegistryBuilder builder;
    builder.add<Body2D>().add<Pose2D>().add<Velocity2D>().add<Impulse2D>();
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
        return kEntity;
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
};

constexpr Body2D kGround{.motion = static_cast<std::uint8_t>(Motion::Static),
                         .shape = static_cast<std::uint8_t>(Shape::Box),
                         .width = 10,
                         .height = 0.5F,
                         .friction = 0.6F};

constexpr Body2D kCrate{.motion = static_cast<std::uint8_t>(Motion::Dynamic),
                        .shape = static_cast<std::uint8_t>(Shape::Box),
                        .width = 0.5F,
                        .height = 0.5F,
                        .density = 1,
                        .friction = 0.6F};

constexpr Body2D kBall{.motion = static_cast<std::uint8_t>(Motion::Dynamic),
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
