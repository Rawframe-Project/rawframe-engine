// A game bringing a textured model whole (D314): the plaza cooked, its
// crate's glTF made a mesh, its materials, and its texture, and the game
// read from its cooked description and from its sources both know each
// subasset by its resource's first half, under the crate's path.

#include "rawframe/cook/animation.h"
#include "rawframe/cook/audio.h"
#include "rawframe/cook/cook.h"
#include "rawframe/cook/game.h"
#include "rawframe/cook/kest.h"
#include "rawframe/cook/material.h"
#include "rawframe/cook/mesh.h"
#include "rawframe/cook/mod.h"
#include "rawframe/cook/scene.h"
#include "rawframe/cook/text.h"
#include "rawframe/cook/texture.h"
#include "rawframe/execution/cancellation.h"
#include "rawframe/execution/executor.h"
#include "rawframe/game_content/cooked_content.h"
#include "rawframe/test/scratch.h"
#include "rawframe/test/test.h"
#include "rawframe/world_kest/game_files.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

using namespace rawframe;

namespace fs = std::filesystem;

namespace {

std::string hexOf(std::uint64_t id) {
    std::string made(16, '0');
    for (std::size_t at = 16; at > 0; --at, id >>= 4U) {
        made[at - 1] = "0123456789abcdef"[id & 0xFU];
    }
    return made;
}

/// What a game's materials and textures are, as `id path` lines in order.
std::vector<std::string> resourcesOf(const world_kest::GameFiles& files) {
    std::vector<std::string> made;
    for (const world_kest::GameMaterialResource& each : files.materials()) {
        made.push_back(hexOf(each.id) + " " + each.path + (each.subasset ? " subasset" : ""));
    }
    for (const world_kest::GameTextureResource& each : files.textures()) {
        made.push_back(hexOf(each.id) + " " + each.path);
    }
    std::ranges::sort(made);
    return made;
}

} // namespace

RAWFRAME_TEST(AGameKnowsItsMeshesSubassetsCookedOrNot) {
    const fs::path kBase = test::scratchDirectory("game_subassets");
    fs::remove_all(kBase);
    const fs::path kOutput = kBase / "content";
    static const std::array<cook::Importer, 10> kImporters = {cook::animationImporter(),
                                                              cook::audioImporter(),
                                                              cook::gameImporter(),
                                                              cook::kestImporter(),
                                                              cook::materialImporter(),
                                                              cook::meshImporter(),
                                                              cook::modImporter(),
                                                              cook::sceneImporter(),
                                                              cook::textImporter(),
                                                              cook::textureImporter()};
    const fs::path kPlaza = fs::path{RAWFRAME_SAMPLE_GAMES} / "plaza";
    const auto kCooked =
        cook::cookSources(cook::CookRequest{.sources = kPlaza, .output = kOutput, .importers = kImporters});
    RAWFRAME_EXPECT(kCooked.has_value() && kCooked->failures.empty());

    execution::ManualClock clock;
    execution::CancellationScope scope{clock};
    execution::Executor io{execution::ExecutorSettings{.kind = execution::ExecutorKind::BlockingIo, .workers = 1}};
    RAWFRAME_EXPECT(io.admitOwner(execution::OwnerId{1}, {.maximumPendingTasks = 64}).has_value());
    {
        auto content = game_content::CookedContent::open(io, execution::OwnerId{1}, scope, clock, kOutput);
        RAWFRAME_EXPECT(content.has_value());
        if (content.has_value()) {
            const auto kFromCooked = world_kest::GameFiles::fromContent(
                **content, content::ResourceId{base::parseBits128Hex("9c729ea87f092246fec883d1a3f8d6cf").value});
            const auto kFromSources = world_kest::GameFiles::fromDirectory(kPlaza / "plaza.game", content->get());
            RAWFRAME_EXPECT(kFromCooked.has_value() && kFromSources.has_value());
            if (kFromCooked.has_value() && kFromSources.has_value()) {
                const std::vector<std::string> kKnown = resourcesOf(*kFromCooked);
                RAWFRAME_EXPECT(kKnown == resourcesOf(*kFromSources));
                // The crate's planks and iron, the mound's grass, and the
                // planks' texture, beside the materials and textures the
                // game's lines name, the sky's picture and the hall's among
                // them (D322, D326).
                for (const std::string_view kSubasset : {"4fa8f5945fc4b1ae crate.gltf#material/Iron subasset",
                                                         "ea67406491ff8d5d crate.gltf#material/Planks subasset",
                                                         "3aa889e544dcfceb mound.gltf#material/Grass subasset",
                                                         "71cc8dbcf82d6cfa crate.gltf#texture/Planks",
                                                         "9f822820a44af4fc paving.png",
                                                         "a97797a3a58f6d7a sky.hdr",
                                                         "ac452b5e41f745bf hall.hdr"}) {
                    RAWFRAME_EXPECT(std::ranges::contains(kKnown, kSubasset));
                }
                RAWFRAME_EXPECT(kKnown.size() == 10);
            }
        }
    }
    io.stop();
    fs::remove_all(kBase);
}
