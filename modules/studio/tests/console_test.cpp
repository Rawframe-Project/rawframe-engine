// Studio's Console: a log record read as a line, and a place it
// names in the game's own files opened only when it is one of them, however
// a record a played game wrote names it.

#include "../src/console.h"
#include "../src/records.h"
#include "rawframe/test/test.h"

#include <filesystem>
#include <fstream>
#include <string>

using namespace rawframe::studio;

RAWFRAME_TEST(ARecordIsALineWithItsLevelAndPlace) {
    const auto kLine =
        consoleLineOf(R"({"kind":"log","severity":"warning","code":"reload_refused","message":"not reloaded",)"
                      R"("fields":{"error":"game/scripts/move.kest:12:3: unknown name"}})",
                      ConsoleLine::Source::Server);
    RAWFRAME_EXPECT(kLine.has_value());
    if (!(kLine.has_value())) {
        return;
    }
    RAWFRAME_EXPECT(kLine->level == ConsoleLine::Level::Warning);
    RAWFRAME_EXPECT(kLine->text == "reload_refused: not reloaded; game/scripts/move.kest:12:3: unknown name");
    RAWFRAME_EXPECT(kLine->at.has_value());
    if (!(kLine->at.has_value())) {
        return;
    }
    RAWFRAME_EXPECT(kLine->at->file == "game/scripts/move.kest");
    RAWFRAME_EXPECT(kLine->at->line == 12 && kLine->at->column == 3);
    // A record saying no error, its short values instead.
    const auto kSaid = consoleLineOf(R"({"kind":"log","severity":"info","code":"lifecycle","message":"host lifecycle",)"
                                     R"("fields":{"state":"draining","iterations":12,"reason":"stopped"}})",
                                     ConsoleLine::Source::Server);
    RAWFRAME_EXPECT(kSaid.has_value() &&
                    kSaid->text == "lifecycle: host lifecycle; state draining, iterations 12, reason stopped");
    RAWFRAME_EXPECT(!consoleLineOf(R"({"kind":"trace"})", ConsoleLine::Source::Client).has_value());
    RAWFRAME_EXPECT(!consoleLineOf("not a record", ConsoleLine::Source::Client).has_value());
}

RAWFRAME_TEST(APlaceThatClimbsOutOfTheGameIsNone) {
    RAWFRAME_EXPECT(!gamePlaceIn("game/../../home/author/.ssh/config:1:1: x").has_value());
    RAWFRAME_EXPECT(!gamePlaceIn("game/a/../../b.kest:1:1: x").has_value());
    RAWFRAME_EXPECT(!gamePlaceIn("game/./a.kest:1:1: x").has_value());
    RAWFRAME_EXPECT(!gamePlaceIn("game//etc/passwd:1:1: x").has_value());
    RAWFRAME_EXPECT(!gamePlaceIn("game/a b.kest:1:1: x").has_value());
    RAWFRAME_EXPECT(!gamePlaceIn("mygame/a.kest:1:1: x").has_value());
    // The first plain one after any that is not.
    const auto kPlace = gamePlaceIn("game/../x:1:1 then game/a.kest:4:2: y");
    RAWFRAME_EXPECT(kPlace.has_value());
    if (!(kPlace.has_value())) {
        return;
    }
    RAWFRAME_EXPECT(kPlace->file == "game/a.kest" && kPlace->line == 4);
}

RAWFRAME_TEST(OnlyTheGamesOwnFilesAreOpened) {
    const std::filesystem::path kRoot = std::filesystem::temp_directory_path() / "rawframe_console_test";
    std::filesystem::remove_all(kRoot);
    std::filesystem::create_directories(kRoot / "game" / "scripts");
    std::ofstream(kRoot / "game" / "scripts" / "move.kest") << "x";
    std::ofstream(kRoot / "outside.txt") << "x";
    const std::filesystem::path kGame = kRoot / "game";
    const auto kInside = gameFile(kGame, "game/scripts/move.kest");
    RAWFRAME_EXPECT(kInside.has_value());
    if (!(kInside.has_value())) {
        return;
    }
    RAWFRAME_EXPECT(std::filesystem::equivalent(*kInside, kGame / "scripts" / "move.kest"));
    RAWFRAME_EXPECT(!gameFile(kGame, "game/../outside.txt").has_value());
    RAWFRAME_EXPECT(!gameFile(kGame, "game/scripts/../../outside.txt").has_value());
    RAWFRAME_EXPECT(!gameFile(kGame, "game/" + (kRoot / "outside.txt").string()).has_value());
    RAWFRAME_EXPECT(!gameFile(kGame, "scripts/move.kest").has_value());
    RAWFRAME_EXPECT(!gameFile(kGame, "game/").has_value());
    // A link inside the game to a file outside it is not the game's.
    std::error_code error;
    std::filesystem::create_symlink(kRoot / "outside.txt", kGame / "link.kest", error);
    if (!error) {
        RAWFRAME_EXPECT(!gameFile(kGame, "game/link.kest").has_value());
    }
    std::filesystem::remove_all(kRoot);
}
