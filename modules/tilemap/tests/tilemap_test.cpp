// Tilemap documents: tiles read and written back as read; painted
// within the grid and nowhere past it; a row's solid tiles joined into one
// body's box; a tile found in its sheet; and what is not a tilemap refused
// by its line.

#include "rawframe/test/test.h"
#include "rawframe/tilemap/tilemap.h"

#include <string_view>

using namespace rawframe;
using namespace rawframe::tilemap;

namespace {

constexpr std::string_view kLevel = "tilemap 1\n"
                                    "tileset a44ecb4a39ac5cc8 8 4\n"
                                    "cell 1\n"
                                    "solid 0 1 2\n"
                                    "grid 6 3\n"
                                    "tiles\n"
                                    ". . . . . .\n"
                                    ". . 4 4 . .\n"
                                    "0 1 1 . 1 2\n";

Tilemap read(std::string_view text) {
    auto made = parseTilemap(text);
    RAWFRAME_EXPECT(made.has_value());
    return made.has_value() ? *made : Tilemap{};
}

} // namespace

RAWFRAME_TEST(ATilemapIsWrittenAsItReads) {
    const Tilemap kRead = read(kLevel);
    RAWFRAME_EXPECT(kRead.texture == 0xa44ecb4a39ac5cc8ULL && kRead.sheetColumns == 8 && kRead.sheetRows == 4 &&
                    kRead.columns == 6 && kRead.rows == 3);
    RAWFRAME_EXPECT(kRead.at(2, 1) == 4 && kRead.at(0, 0) == kEmpty && kRead.at(5, 2) == 2);
    RAWFRAME_EXPECT(writeTilemap(kRead) == kLevel);
    // Solid tiles listed twice or out of order are kept once, in order.
    RAWFRAME_EXPECT(read("tilemap 1\ntileset 0000000000000001 4 1\nsolid 3 1 3\ngrid 1 1\ntiles\n.\n").solid ==
                    (std::vector<std::uint16_t>{1, 3}));
}

RAWFRAME_TEST(PaintingReachesOnlyTheGrid) {
    Tilemap level = read(kLevel);
    RAWFRAME_EXPECT(paint(level, 4, 1, 5, 5, 7) == 4);
    RAWFRAME_EXPECT(level.at(4, 1) == 7 && level.at(5, 2) == 7 && level.at(3, 1) == 4);
    RAWFRAME_EXPECT(paint(level, 4, 1, 1, 1, 7) == 0);
    RAWFRAME_EXPECT(paint(level, 9, 9, 2, 2, 7) == 0);
    RAWFRAME_EXPECT(paint(level, 0, 2, 1, 1, kEmpty) == 1 && level.at(0, 2) == kEmpty);
    RAWFRAME_EXPECT(read(writeTilemap(level)) == level);
}

RAWFRAME_TEST(ARowsSolidTilesAreOneBox) {
    // The bottom row: 0 1 1, a gap, 1 2: two runs; tile 4 is not solid.
    const std::vector<SolidRun> kRuns = solidRuns(read(kLevel));
    RAWFRAME_EXPECT(kRuns.size() == 2);
    RAWFRAME_EXPECT(kRuns.size() == 2 && kRuns[0].left == 0 && kRuns[0].width == 3 && kRuns[0].top == -2 &&
                    kRuns[1].left == 4 && kRuns[1].width == 2);
    // Tile 12 of a sheet of eight a row is its second row's fifth.
    const SheetPlace kPlace = placeOf(read(kLevel), 12);
    RAWFRAME_EXPECT(kPlace.column == 4 && kPlace.row == 1);
}

RAWFRAME_TEST(WhatIsNotATilemapIsRefusedByItsLine) {
    for (const std::string_view kText : {
             "grid 1 1\n",
             "tilemap 2\n",
             "tilemap 1\ntileset a44ecb4a 8 4\ngrid 1 1\ntiles\n.\n",
             "tilemap 1\ntileset 0000000000000001 0 4\ngrid 1 1\ntiles\n.\n",
             "tilemap 1\ntileset 0000000000000001 8 4\ncell 0\n",
             "tilemap 1\ntileset 0000000000000001 8 4\ngrid 0 1\n",
             "tilemap 1\ntileset 0000000000000001 8 4\ngrid 2 1\ntiles\n. .\n. .\n",
             "tilemap 1\ntileset 0000000000000001 8 4\ngrid 2 2\ntiles\n. .\n",
             "tilemap 1\ntileset 0000000000000001 8 4\ngrid 2 1\ntiles\n. x\n",
             "tilemap 1\ntileset 0000000000000001 8 4\ngrid 2 1\ntiles\n. 32\n",
             "tilemap 1\ngrid 1 1\ntiles\n.\n",
             "tilemap 1\ntileset 0000000000000001 2 2\ngrid 1 1\ntiles\n4\n",
             "tilemap 1\ntileset 0000000000000001 4096 2\n",
         }) {
        const auto kRead = parseTilemap(kText);
        RAWFRAME_EXPECT(!kRead.has_value());
        RAWFRAME_EXPECT(kRead.has_value() || kRead.error().description().find("tilemap line") != std::string::npos);
    }
}
