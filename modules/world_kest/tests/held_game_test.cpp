// A game read from files held in memory (D166): the same game as from its
// directory, a scene instanced through a sidecar in a subdirectory, and a
// file that is not held or is too large. Runs in the web build too.

#include "rawframe/scene/scene.h"
#include "rawframe/test/files.h"
#include "rawframe/test/test.h"
#include "rawframe/world_kest/game_files.h"

#include <string>
#include <utility>
#include <vector>

using namespace rawframe;

namespace {

using Held = std::vector<std::pair<std::string, std::string>>;

/// Every file of the test games, by its path relative to their directory.
Held testGames() {
    Held files;
    for (std::string& path : test::filesUnder(RAWFRAME_WORLD_KEST_GAMES, "")) {
        std::string text = test::readFile(RAWFRAME_WORLD_KEST_GAMES + path);
        files.emplace_back(std::move(path), std::move(text));
    }
    return files;
}

constexpr std::string_view kEmptyScene =
    "{\n  \"kind\": \"rawframe.scene\",\n  \"formatVersion\": 1,\n  \"schema\": {},\n  \"entities\": []\n}\n";

} // namespace

RAWFRAME_TEST(AHeldGameIsItsDirectorysGame) {
    const Held kFiles = testGames();
    RAWFRAME_EXPECT(!kFiles.empty());
    auto held = world_kest::GameFiles::fromHeld("movers.game", kFiles);
    RAWFRAME_EXPECT(held.has_value());
    if (!held.has_value()) {
        return;
    }
    RAWFRAME_EXPECT(held->named());
#if RAWFRAME_FILE_SYSTEM
    // Nothing held is watched for a reload.
    RAWFRAME_EXPECT(!held->directory().has_value());
#endif
    const auto kProgram = held->compile("movers.kest");
    RAWFRAME_EXPECT(kProgram.has_value() && (*kProgram)->layout("Position").has_value());
#if RAWFRAME_FILE_SYSTEM
    // Read from the directory, the same text and the same digest.
    const auto kRead = world_kest::GameFiles::fromDirectory(std::string{RAWFRAME_WORLD_KEST_GAMES} + "movers.game");
    RAWFRAME_EXPECT(kRead.has_value() && kRead->text() == held->text() && kRead->digest() == held->digest());
#endif
}

RAWFRAME_TEST(AHeldGameFindsAnInstancedSceneByItsSidecar) {
    Held files = testGames();
    std::string game = test::readFile(std::string{RAWFRAME_WORLD_KEST_GAMES} + "movers.game");
    files.emplace_back("mine/movers.game", game.substr(0, game.find("spawn 3")) + "scene start.scene\n");
    const base::Bits128 kPair = base::parseBits128Hex("6a0e3f1c2b4d5e6f7a8b9c0d1e2f3a4b").value;
    scene::Scene start;
    start.instances.push_back({.scene = kPair, .entities = {}, .overrides = {}});
    files.emplace_back("mine/start.scene", *scene::writeScene(start));
    files.emplace_back("mine/prefabs/pair.scene", std::string{kEmptyScene});
    const std::string kSidecar = "{\n  \"schema\": 1,\n  \"resourceId\": \"6a0e3f1c2b4d5e6f7a8b9c0d1e2f3a4b\",\n"
                                 "  \"importer\": \"rawframe.scene\"\n}\n";
    files.emplace_back("mine/prefabs/pair.scene.rfmeta", kSidecar);
    // Held paths are relative to the description's directory; a game in a
    // subdirectory holds only what is under it.
    Held mine;
    for (const auto& [path, text] : files) {
        if (path.starts_with("mine/")) {
            mine.emplace_back(path.substr(5), text);
        }
    }
    mine.emplace_back("movers.kest", test::readFile(std::string{RAWFRAME_WORLD_KEST_GAMES} + "movers.kest"));
    const auto kGame = world_kest::GameFiles::fromHeld("movers.game", mine);
    RAWFRAME_EXPECT(kGame.has_value());
    if (kGame.has_value()) {
        const auto kFound = kGame->sceneById(kPair);
        RAWFRAME_EXPECT(kFound.has_value() && *kFound == kEmptyScene);
    }
    // Without the sidecar the instance names nothing.
    std::erase_if(mine, [](const auto& file) {
        return file.first.ends_with(".rfmeta");
    });
    RAWFRAME_EXPECT(!world_kest::GameFiles::fromHeld("movers.game", mine).has_value());
}

RAWFRAME_TEST(AHeldGameRefusesWhatIsNotHeldOrTooLarge) {
    Held files = testGames();
    // The description itself, and a scene it names, not held.
    RAWFRAME_EXPECT(!world_kest::GameFiles::fromHeld("absent.game", files).has_value());
    std::string game = test::readFile(std::string{RAWFRAME_WORLD_KEST_GAMES} + "movers.game");
    files.emplace_back("unheld.game", game + "scene nowhere.scene\n");
    RAWFRAME_EXPECT(!world_kest::GameFiles::fromHeld("unheld.game", files).has_value());
    // A Kest file over 1 MiB, as from a directory.
    files.emplace_back("large.kest", std::string((std::size_t{1} << 20U) + 1, ' '));
    RAWFRAME_EXPECT(!world_kest::GameFiles::fromHeld("movers.game", files).has_value());
}
