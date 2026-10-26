#pragma once

// A tilemap document: a 2D level's tiles on
// a grid, drawn from one sheet of tiles, some of which bodies collide with,
// as text. Written by hand, by a tool, by the session's tilemap.paint, or
// by Studio's Tiles mode; cooked by the `rawframe.tilemap` importer and
// drawn by the canvas where a `rawframe.canvas.Tilemap` places it.
//
//     tilemap 1
//     tileset a44ecb4a39ac5cc8 8 4
//     cell 1
//     solid 0 1 2
//     grid 6 3
//     tiles
//     . . . . . .
//     . . 4 4 . .
//     0 1 1 1 1 2
//
// `tileset` names the sheet's texture by the identity the game gives it,
// then how many tiles its rows and columns hold, the sheet split evenly; a
// tile's index counts along its rows from nought. `cell` is a tile's side
// in meters. `solid` lists the tiles bodies collide with.
// The rows run from the top down and each from the left, `.` an empty
// cell. `#` begins a comment.

#include "rawframe/base/bits128.h"
#include "rawframe/result/result.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace rawframe::tilemap {

/// The resource a tilemap is cooked into: its text, checked, as it
/// was written, which a client reads as `parseTilemap` reads it.
inline constexpr base::Bits128 kTilemapType = base::parseBits128Hex("9bc264ed9bfe68492b86641942cc74e9").value;
inline constexpr std::string_view kTilemapRepresentation = "rawframe.tilemap.text";

/// An empty cell.
inline constexpr std::uint16_t kEmpty = 0xFFFF;

struct Tilemap {
    /// The sheet's texture, by the identity the game gives it.
    std::uint64_t texture = 0;
    /// Tiles across and down the sheet.
    std::uint32_t sheetColumns = 1;
    std::uint32_t sheetRows = 1;
    double cell = 1;
    /// The tiles bodies collide with, ascending.
    std::vector<std::uint16_t> solid;
    std::uint32_t columns = 0;
    std::uint32_t rows = 0;
    /// Each cell's tile or kEmpty, row by row from the top, each from the
    /// left.
    std::vector<std::uint16_t> tiles;

    [[nodiscard]] std::uint16_t at(std::uint32_t column, std::uint32_t row) const noexcept {
        return tiles[(static_cast<std::size_t>(row) * columns) + column];
    }

    friend bool operator==(const Tilemap&, const Tilemap&) = default;
};

/// The explicit limit profile of one tilemap.
struct TilemapLimits {
    std::uint32_t mostCells = 1024;
    std::uint16_t mostTiles = 4096;
    double mostCell = 100;
};

/// Refuses (`InvalidArgument`, naming the line) what is not a tilemap of
/// this form within `limits`: a tileset of a texture and a sheet of at most
/// `mostTiles` tiles, a cell above nought and at most `mostCell` meters,
/// tiles of the sheet, a grid of 1 to `mostCells` each way, and a row of
/// tiles for each of its rows.
[[nodiscard]] result::Result<Tilemap> parseTilemap(std::string_view text, const TilemapLimits& limits = {});

/// The document `map` is, in the form `parseTilemap` reads back to it.
[[nodiscard]] std::string writeTilemap(const Tilemap& map);

/// Puts `tile` (or kEmpty) in the cells of the rectangle from `column`,
/// `row`, `width` by `height`, the part within the grid; how many changed.
std::size_t paint(Tilemap& map,
                  std::uint32_t column,
                  std::uint32_t row,
                  std::uint32_t width,
                  std::uint32_t height,
                  std::uint16_t tile);

/// A rectangle of solid cells, a row's run of them, in meters from the
/// map's top-left corner with +Y up: a static body's box.
struct SolidRun {
    double left = 0;
    double top = 0;
    double width = 0;
    double height = 0;
};

/// The map's solid cells, each row's runs of them joined, so a body
/// sliding along a floor meets no seam between its tiles.
[[nodiscard]] std::vector<SolidRun> solidRuns(const Tilemap& map);

/// Where tile `tile` lies in its sheet: its column and row there.
struct SheetPlace {
    std::uint32_t column = 0;
    std::uint32_t row = 0;
};
[[nodiscard]] SheetPlace placeOf(const Tilemap& map, std::uint16_t tile) noexcept;

} // namespace rawframe::tilemap
