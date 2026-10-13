// Quarry's land (D735): made a few columns a tick from the World's seed,
// every column's ground two to ten blocks deep, stone under earth under
// grass; the same seed draws the same land, another seed another; and a
// player digs the top block of the column its tool aims at, or places
// brick on it, then rests. The test makes the player as admission would and
// sets its tool between ticks, as its client's input would.

#include "game_harness.h"
#include "rawframe/test/test.h"

#include <array>
#include <cstdint>
#include <map>
#include <string>
#include <utility>
#include <vector>

using namespace rawframe;
using namespace rawframe::game_test;

namespace {

constexpr auto kTerrainId = schema::ComponentTypeId::fromText("5c3e5cc3-74d0-4669-8cb9-08ee3e1ec13d");
constexpr auto kBlockId = schema::ComponentTypeId::fromText("19d85c29-6787-4293-ad64-5078eb4255c7");
constexpr auto kDiggerId = schema::ComponentTypeId::fromText("0f03f35b-6f21-46f9-8780-a71c82028ccb");
constexpr auto kToolId = schema::ComponentTypeId::fromText("61646d7c-70f6-4a5d-b965-b39c0c8c1ea4");

/// quarry.Terrain, quarry.Block, quarry.Digger, and controls.Tool, as the
/// program lays them out.
struct Terrain {
    std::uint32_t seed = 0;
    std::uint32_t next = 0;
    bool made = false;
    std::uint32_t blocks = 0;
};

struct Block {
    std::uint32_t x = 0;
    std::uint32_t y = 0;
    std::uint32_t z = 0;
    std::uint8_t kind = 0;
};

struct Digger {
    std::uint32_t rest = 0;
    std::uint32_t dug = 0;
    std::uint32_t placed = 0;
};

struct Tool {
    float x = 0;
    float z = 0;
    float dig = 0;
    float place = 0;
};

constexpr std::uint8_t kStone = 1;
constexpr std::uint8_t kEarth = 2;
constexpr std::uint8_t kGrass = 3;
constexpr std::uint8_t kBrick = 4;

/// Quarry, composed and started from its directory with the World's seed.
class PlayedQuarry : public PlayedGame {
public:
    explicit PlayedQuarry(std::uint64_t seed)
        : PlayedGame(kWatched,
                     "kest.game = " + std::string{RAWFRAME_SAMPLE_GAMES} +
                         "quarry/quarry.game\nworld.tick_rate = 60\nworld.maximum_ticks_per_iteration = 1\n"
                         "world.root_seed = " +
                         std::to_string(seed) + "\n") {
    }
};

/// Every column's blocks by height, bottom first.
using Columns = std::map<std::pair<std::uint32_t, std::uint32_t>, std::map<std::uint32_t, std::uint8_t>>;

Columns columnsOf(world::World& world) {
    Columns made;
    for (const world::EntityHandle kEntity : holding(world, kBlockId)) {
        const Block* const kBlock = valueOf<Block>(world, kEntity, kBlockId);
        made[{kBlock->x, kBlock->z}][kBlock->y] = kBlock->kind;
    }
    return made;
}

/// How many blocks the column at `x`, `z` holds.
std::size_t depthOf(world::World& world, std::uint32_t x, std::uint32_t z) {
    return columnsOf(world)[{x, z}].size();
}

/// The land's one entity's state.
const Terrain* terrainOf(world::World& world) {
    const std::vector<world::EntityHandle> kHeld = holding(world, kTerrainId);
    return kHeld.size() == 1 ? valueOf<Terrain>(world, kHeld.front(), kTerrainId) : nullptr;
}

} // namespace

RAWFRAME_TEST(QuarrysLandIsDrawnFromTheWorldsSeed) {
    Columns first;
    for (const std::uint64_t kSeed : {7ULL, 7ULL, 8ULL}) {
        PlayedQuarry played{kSeed};
        RAWFRAME_EXPECT(played.started());
        if (!played.started()) {
            return;
        }
        world::World& world = *simulation->world();
        // Forty-eight columns a tick: not made after twenty ticks, made
        // after twenty-two.
        played.run(20);
        RAWFRAME_EXPECT(terrainOf(world) != nullptr && !terrainOf(world)->made);
        played.run(2);
        const Terrain* const kTerrain = terrainOf(world);
        RAWFRAME_EXPECT(kTerrain != nullptr && kTerrain->made && kTerrain->next == 32 * 32 && kTerrain->seed != 0);
        const Columns kColumns = columnsOf(world);
        RAWFRAME_EXPECT(kColumns.size() == 32 * 32);
        std::size_t blocks = 0;
        bool layered = true;
        for (const auto& [kAt, kColumn] : kColumns) {
            const auto kTop = static_cast<std::uint32_t>(kColumn.size());
            blocks += kColumn.size();
            // Whole from the bottom, two to ten deep, stone under two of
            // earth under grass.
            layered = layered && kTop >= 2 && kTop <= 10 && kColumn.rbegin()->first == kTop - 1;
            for (const auto& [kY, kKind] : kColumn) {
                const std::uint8_t kWanted = kY + 1 == kTop ? kGrass : kY + 3 >= kTop ? kEarth : kStone;
                layered = layered && kKind == kWanted;
            }
        }
        RAWFRAME_EXPECT(layered && kTerrain != nullptr && blocks == kTerrain->blocks);
        // The same seed draws the same land; another, another.
        if (first.empty()) {
            first = kColumns;
        } else if (kSeed == 7) {
            RAWFRAME_EXPECT(kColumns == first);
        } else {
            RAWFRAME_EXPECT(kColumns != first);
        }
    }
}

RAWFRAME_TEST(APlayerDigsAndBuildsWhereItsToolAims) {
    PlayedQuarry played{7};
    RAWFRAME_EXPECT(played.started());
    if (!played.started()) {
        return;
    }
    world::World& world = *simulation->world();
    const auto kMade = world.create();
    RAWFRAME_EXPECT(kMade.has_value());
    if (!kMade.has_value()) {
        return;
    }
    const world::EntityHandle kPlayer = *kMade;
    put(world, kPlayer, kToolId, Tool{});
    put(world, kPlayer, kDiggerId, Digger{});
    // Asked to dig before the land is made: nothing happens.
    valueOf<Tool>(world, kPlayer, kToolId)->dig = 1;
    played.run(5);
    RAWFRAME_EXPECT(valueOf<Digger>(world, kPlayer, kDiggerId)->dug == 0);
    valueOf<Tool>(world, kPlayer, kToolId)->dig = 0;
    played.run(20);
    RAWFRAME_EXPECT(terrainOf(world) != nullptr && terrainOf(world)->made);
    // Aimed at the land's middle, column 16, 16: its top block dug, then
    // rested thirty ticks before the next.
    Tool* const kTool = valueOf<Tool>(world, kPlayer, kToolId);
    *kTool = Tool{.x = 0.01F, .z = 0.01F, .dig = 1};
    const std::size_t kDeep = depthOf(world, 16, 16);
    played.run(2);
    RAWFRAME_EXPECT(depthOf(world, 16, 16) == kDeep - 1);
    RAWFRAME_EXPECT(valueOf<Digger>(world, kPlayer, kDiggerId)->dug == 1);
    played.run(20);
    RAWFRAME_EXPECT(depthOf(world, 16, 16) == kDeep - 1);
    played.run(15);
    RAWFRAME_EXPECT(depthOf(world, 16, 16) == kDeep - 2);
    // Brick placed on what is left, on the column at the land's far corner
    // too, where the tool aims past the edge.
    *kTool = Tool{.x = 0.01F, .z = 0.01F, .place = 1};
    played.run(35);
    std::map<std::uint32_t, std::uint8_t> middle = columnsOf(world)[{16, 16}];
    RAWFRAME_EXPECT(middle.size() == kDeep - 1 && middle.rbegin()->second == kBrick);
    *kTool = Tool{.x = 3, .z = 3, .place = 1};
    const std::size_t kCorner = depthOf(world, 31, 31);
    played.run(35);
    RAWFRAME_EXPECT(depthOf(world, 31, 31) == kCorner + 1);
    const Digger* const kDigger = valueOf<Digger>(world, kPlayer, kDiggerId);
    RAWFRAME_EXPECT(kDigger->dug == 2 && kDigger->placed == 2);
}
