// A game whose level is a tilemap (D859) strewn with a scatter's crates
// (D864): the ledges game cooked, and the game read from its cooked
// description and from its sources both hold the level's tiles, parsed
// once from the cooked text, the crates placed alike, and the same digest,
// so a server's bodies and a client's tiles are one map. A game read from
// its sources without content cannot have the tiles.

#include "rawframe/cook/cook.h"
#include "rawframe/cook/game.h"
#include "rawframe/cook/kest.h"
#include "rawframe/cook/scatter.h"
#include "rawframe/cook/scene.h"
#include "rawframe/cook/tilemap.h"
#include "rawframe/execution/cancellation.h"
#include "rawframe/execution/executor.h"
#include "rawframe/game_content/cooked_content.h"
#include "rawframe/test/scratch.h"
#include "rawframe/test/test.h"
#include "rawframe/world_kest/game_files.h"

#include <array>
#include <filesystem>

using namespace rawframe;

namespace fs = std::filesystem;

RAWFRAME_TEST(AGamesTilemapIsReadOnceCookedOrNot) {
    const fs::path kBase = test::scratchDirectory("game_tilemaps");
    fs::remove_all(kBase);
    const fs::path kOutput = kBase / "content";
    static const std::array<cook::Importer, 5> kImporters = {cook::gameImporter(),
                                                             cook::kestImporter(),
                                                             cook::scatterImporter(),
                                                             cook::sceneImporter(),
                                                             cook::tilemapImporter()};
    const fs::path kLedges = fs::path{RAWFRAME_SAMPLE_GAMES} / "ledges";
    const auto kCooked =
        cook::cookSources(cook::CookRequest{.sources = kLedges, .output = kOutput, .importers = kImporters});
    RAWFRAME_EXPECT(kCooked.has_value() && kCooked->failures.empty());
    RAWFRAME_EXPECT(!world_kest::GameFiles::fromDirectory(kLedges / "ledges.game").has_value());

    execution::ManualClock clock;
    execution::CancellationScope scope{clock};
    execution::Executor io{execution::ExecutorSettings{.kind = execution::ExecutorKind::BlockingIo, .workers = 1}};
    RAWFRAME_EXPECT(io.admitOwner(execution::OwnerId{1}, {.maximumPendingTasks = 64}).has_value());
    {
        auto content = game_content::CookedContent::open(io, execution::OwnerId{1}, scope, clock, kOutput);
        RAWFRAME_EXPECT(content.has_value());
        if (content.has_value()) {
            const auto kFromCooked = world_kest::GameFiles::fromContent(
                **content, content::ResourceId{base::parseBits128Hex("a3ec792f9892d3ec8f27906a66e4d689").value});
            const auto kFromSources = world_kest::GameFiles::fromDirectory(kLedges / "ledges.game", content->get());
            RAWFRAME_EXPECT(kFromCooked.has_value() && kFromSources.has_value());
            if (kFromCooked.has_value() && kFromSources.has_value()) {
                for (const world_kest::GameFiles* kFiles : {&*kFromCooked, &*kFromSources}) {
                    RAWFRAME_EXPECT(kFiles->tilemaps().size() == 1);
                    if (kFiles->tilemaps().size() == 1) {
                        const world_kest::GameTilemapResource& kLevel = kFiles->tilemaps()[0];
                        RAWFRAME_EXPECT(kLevel.id == 0x8f5c1d2e3a4b6079 && kLevel.path == "level.tilemap");
                        RAWFRAME_EXPECT(kLevel.map != nullptr && kLevel.map->columns == 60 && kLevel.map->rows == 12);
                        // The floor, the walls' rows, and the three ledges.
                        RAWFRAME_EXPECT(kLevel.map != nullptr && tilemap::solidRuns(*kLevel.map).size() == 26);
                    }
                }
                // Twelve crates, cooked where the sources place them.
                RAWFRAME_EXPECT(kFromCooked->scatters().size() == 1 && kFromSources->scatters().size() == 1);
                if (kFromCooked->scatters().size() == 1 && kFromSources->scatters().size() == 1) {
                    RAWFRAME_EXPECT(kFromCooked->scatters()[0].placed == kFromSources->scatters()[0].placed &&
                                    kFromCooked->scatters()[0].placed.placements.size() == 12 &&
                                    kFromCooked->scatters()[0].placed.prefab == "crate.scene");
                }
                RAWFRAME_EXPECT(kFromCooked->digest() == kFromSources->digest());
            }
        }
    }
    io.stop();
    fs::remove_all(kBase);
}
