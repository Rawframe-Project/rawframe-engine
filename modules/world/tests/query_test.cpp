// Queries: matching, filtering, writing through, determinism, and archetypes
// that appear after the first run.

#include "components.h"
#include "rawframe/test/test.h"
#include "rawframe/world/query.h"

#include <vector>

using namespace rawframe::world;
using namespace rawframe::world::testing;

RAWFRAME_TEST(QueriesMatchRequiredAndExcludedTerms) {
    World world{makeRegistry()};
    const Keys kKeys = keysOf(world.registry());
    std::vector<EntityHandle> moving;
    std::vector<EntityHandle> resting;
    for (std::int32_t index = 0; index < 6; ++index) {
        const EntityHandle kEntity = *world.create();
        RAWFRAME_EXPECT(world.insert(kEntity, kKeys.position, Position{index, 0}).has_value());
        if (index % 2 == 0) {
            RAWFRAME_EXPECT(world.insert(kEntity, kKeys.velocity, Velocity{1, 1}).has_value());
            moving.push_back(kEntity);
        } else {
            resting.push_back(kEntity);
        }
    }
    RAWFRAME_EXPECT(world.insert(moving[0], kKeys.frozen, Frozen{}).has_value());

    auto motion = Query<Write<Position>, Read<Velocity>, Without<Frozen>>::resolve(world.registry());
    RAWFRAME_EXPECT(motion.has_value());
    if (!motion.has_value()) {
        return;
    }
    RAWFRAME_EXPECT(motion->count(world) == 2);
    motion->forEach(world, [](EntityHandle, Position& position, const Velocity& velocity) {
        position.x += velocity.dx * 10;
    });
    RAWFRAME_EXPECT(world.get(moving[0], kKeys.position)->x == 0);
    RAWFRAME_EXPECT(world.get(moving[1], kKeys.position)->x == 12);
    RAWFRAME_EXPECT(world.get(moving[2], kKeys.position)->x == 14);
    RAWFRAME_EXPECT(world.get(resting[0], kKeys.position)->x == 1);

    auto frozen = Query<Read<Position>, With<Frozen>>::resolve(world.registry());
    std::vector<EntityHandle> seen;
    frozen->forEach(world, [&seen](EntityHandle entity, const Position&) {
        seen.push_back(entity);
    });
    RAWFRAME_EXPECT(seen == std::vector<EntityHandle>{moving[0]});

    RAWFRAME_EXPECT(motion->reads() == std::vector<rawframe::schema::ComponentRuntimeId>{kKeys.velocity.id});
    RAWFRAME_EXPECT(motion->writes() == std::vector<rawframe::schema::ComponentRuntimeId>{kKeys.position.id});
}

RAWFRAME_TEST(ArchetypesCreatedLaterAreFound) {
    World world{makeRegistry()};
    const Keys kKeys = keysOf(world.registry());
    auto positions = Query<Read<Position>>::resolve(world.registry());
    RAWFRAME_EXPECT(positions->count(world) == 0);
    const EntityHandle kEntity = *world.create();
    RAWFRAME_EXPECT(world.insert(kEntity, kKeys.position, Position{}).has_value());
    RAWFRAME_EXPECT(positions->count(world) == 1);
    RAWFRAME_EXPECT(world.insert(kEntity, kKeys.tracked, Tracked{}).has_value());
    RAWFRAME_EXPECT(positions->count(world) == 1);
    const EntityHandle kOther = *world.create();
    RAWFRAME_EXPECT(world.insert(kOther, kKeys.position, Position{}).has_value());
    RAWFRAME_EXPECT(positions->count(world) == 2);
}

namespace {

/// Builds a World through a fixed history and returns the iteration order.
std::vector<std::int32_t> iterationOrder() {
    World world{makeRegistry()};
    const Keys kKeys = keysOf(world.registry());
    std::vector<EntityHandle> entities;
    for (std::int32_t index = 0; index < 40; ++index) {
        const EntityHandle kEntity = *world.create();
        static_cast<void>(world.insert(kEntity, kKeys.position, Position{index, 0}));
        if (index % 3 == 0) {
            static_cast<void>(world.insert(kEntity, kKeys.velocity, Velocity{}));
        }
        entities.push_back(kEntity);
    }
    for (std::size_t index = 0; index < entities.size(); index += 7) {
        static_cast<void>(world.destroy(entities[index]));
    }
    std::vector<std::int32_t> order;
    auto query = Query<Read<Position>>::resolve(world.registry());
    query->forEach(world, [&order](EntityHandle, const Position& position) {
        order.push_back(position.x);
    });
    return order;
}

} // namespace

RAWFRAME_TEST(TheSameHistoryIteratesTheSameWay) {
    const auto kFirst = iterationOrder();
    RAWFRAME_EXPECT(kFirst.size() == 34);
    RAWFRAME_EXPECT(iterationOrder() == kFirst);
}

RAWFRAME_TEST(ATermOutsideTheRegistryDoesNotResolve) {
    rawframe::schema::RegistryBuilder builder;
    builder.add<Position>();
    const auto kSmall = builder.freeze();
    using Motion = Query<Read<Position>, Read<Velocity>>;
    RAWFRAME_EXPECT(!Motion::resolve(**kSmall).has_value());
}
