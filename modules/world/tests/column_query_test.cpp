// Column queries: the same matches and order as typed queries, handed out an
// archetype at a time by runtime ID.

#include "components.h"
#include "rawframe/test/test.h"
#include "rawframe/world/column_query.h"
#include "rawframe/world/errors.h"
#include "rawframe/world/query.h"

#include <array>
#include <vector>

using namespace rawframe::world;
using namespace rawframe::world::testing;

RAWFRAME_TEST(ColumnQueriesMatchAsTypedQueriesDo) {
    World world{makeRegistry()};
    const Keys kKeys = keysOf(world.registry());
    std::vector<EntityHandle> entities;
    for (std::int32_t index = 0; index < 6; ++index) {
        const EntityHandle kEntity = *world.create();
        RAWFRAME_EXPECT(world.insert(kEntity, kKeys.position, Position{index, 0}).has_value());
        if (index % 2 == 0) {
            RAWFRAME_EXPECT(world.insert(kEntity, kKeys.velocity, Velocity{1, 2}).has_value());
        }
        if (index == 4) {
            RAWFRAME_EXPECT(world.insert(kEntity, kKeys.frozen, Frozen{}).has_value());
        }
        entities.push_back(kEntity);
    }
    const std::array<ColumnTerm, 3> kTerms = {ColumnTerm{kKeys.velocity.id, Access::Read},
                                              ColumnTerm{kKeys.position.id, Access::Write},
                                              ColumnTerm{kKeys.frozen.id, Access::Without}};
    auto motion = ColumnQuery::resolve(kTerms, world.registry());
    RAWFRAME_EXPECT(motion.has_value());
    if (!motion.has_value()) {
        return;
    }
    RAWFRAME_EXPECT(motion->count(world) == 2);
    std::vector<EntityHandle> seen;
    motion->forEachChunk(world, [&seen](const ColumnChunk& chunk) {
        RAWFRAME_EXPECT(chunk.columns.size() == 2);
        auto* velocities = reinterpret_cast<const Velocity*>(chunk.columns[0]);
        auto* positions = reinterpret_cast<Position*>(chunk.columns[1]);
        for (std::size_t row = 0; row < chunk.entities.size(); ++row) {
            positions[row].x += velocities[row].dx * 10;
            seen.push_back(chunk.entities[row]);
        }
    });
    RAWFRAME_EXPECT((seen == std::vector<EntityHandle>{entities[0], entities[2]}));
    RAWFRAME_EXPECT(world.get(entities[2], kKeys.position)->x == 12);
    RAWFRAME_EXPECT(world.get(entities[4], kKeys.position)->x == 4);

    auto typed = Query<Read<Velocity>, Write<Position>, Without<Frozen>>::resolve(world.registry());
    std::vector<EntityHandle> typedSeen;
    typed->forEach(world, [&typedSeen](EntityHandle entity, const Velocity&, Position&) {
        typedSeen.push_back(entity);
    });
    RAWFRAME_EXPECT(typedSeen == seen);
    RAWFRAME_EXPECT(motion->reads() == std::vector<rawframe::schema::ComponentRuntimeId>{kKeys.velocity.id});
    RAWFRAME_EXPECT(motion->writes() == std::vector<rawframe::schema::ComponentRuntimeId>{kKeys.position.id});
}

RAWFRAME_TEST(ColumnQueryTermsAreChecked) {
    World world{makeRegistry()};
    const Keys kKeys = keysOf(world.registry());
    const auto kRefused = [&world](std::span<const ColumnTerm> terms) {
        auto query = ColumnQuery::resolve(terms, world.registry());
        return !query.has_value() && query.error().code() == code(WorldError::InvalidQueryTerm);
    };
    const std::array<ColumnTerm, 1> kTagRead = {ColumnTerm{kKeys.frozen.id, Access::Read}};
    const std::array<ColumnTerm, 2> kTwice = {ColumnTerm{kKeys.position.id, Access::Read},
                                              ColumnTerm{kKeys.position.id, Access::Write}};
    const std::array<ColumnTerm, 1> kUnknown = {ColumnTerm{rawframe::schema::ComponentRuntimeId{99}, Access::Read}};
    RAWFRAME_EXPECT(kRefused(kTagRead));
    RAWFRAME_EXPECT(kRefused(kTwice));
    RAWFRAME_EXPECT(kRefused(kUnknown));
    const std::array<ColumnTerm, 1> kTagFilter = {ColumnTerm{kKeys.frozen.id, Access::With}};
    RAWFRAME_EXPECT(ColumnQuery::resolve(kTagFilter, world.registry()).has_value());
}

RAWFRAME_TEST(PlainDataIsDescribed) {
    RAWFRAME_EXPECT(rawframe::schema::describeComponent<Position>().plainData);
    RAWFRAME_EXPECT(!rawframe::schema::describeComponent<Tracked>().plainData);
}
