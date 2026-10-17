#include "rawframe/tilemap/tilemap.h"

#include "rawframe/tilemap/errors.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdio>

namespace rawframe::tilemap {

namespace {

std::unexpected<result::Error> refused(std::size_t line, std::string_view why) {
    return result::fail(result::ErrorClass::InvalidArgument,
                        kTilemapDomain,
                        code(TilemapError::BadSource),
                        "tilemap line " + std::to_string(line) + ": " + std::string{why});
}

/// The line's words, split on spaces and tabs, with a comment left out.
std::vector<std::string_view> wordsOf(std::string_view line) {
    if (const std::size_t kComment = line.find('#'); kComment != std::string_view::npos) {
        line = line.substr(0, kComment);
    }
    std::vector<std::string_view> words;
    std::size_t at = 0;
    while (at < line.size()) {
        while (at < line.size() && (line[at] == ' ' || line[at] == '\t' || line[at] == '\r')) {
            ++at;
        }
        const std::size_t kStart = at;
        while (at < line.size() && line[at] != ' ' && line[at] != '\t' && line[at] != '\r') {
            ++at;
        }
        if (at > kStart) {
            words.push_back(line.substr(kStart, at - kStart));
        }
    }
    return words;
}

template <class Number> std::optional<Number> numberOf(std::string_view word, int base = 10) {
    Number value{};
    const auto [kEnd, kError] = std::from_chars(word.data(), word.data() + word.size(), value, base);
    if (kError != std::errc{} || kEnd != word.data() + word.size()) {
        return std::nullopt;
    }
    return value;
}

std::optional<double> metersOf(std::string_view word) {
    double value = 0;
    const auto [kEnd, kError] = std::from_chars(word.data(), word.data() + word.size(), value);
    if (kError != std::errc{} || kEnd != word.data() + word.size() || !std::isfinite(value)) {
        return std::nullopt;
    }
    return value;
}

enum class Part : std::uint8_t {
    Header,
    Settings,
    Tiles,
    Done
};

} // namespace

result::Result<Tilemap> parseTilemap(std::string_view text, const TilemapLimits& limits) {
    Tilemap made;
    Part part = Part::Header;
    std::size_t line = 0;
    bool tiled = false;
    while (!text.empty()) {
        const std::size_t kEnd = text.find('\n');
        const std::string_view kLine = text.substr(0, kEnd);
        text = kEnd == std::string_view::npos ? std::string_view{} : text.substr(kEnd + 1);
        ++line;
        const std::vector<std::string_view> kWords = wordsOf(kLine);
        if (kWords.empty()) {
            continue;
        }
        if (part == Part::Header) {
            if (kWords.size() != 2 || kWords[0] != "tilemap" || kWords[1] != "1") {
                return refused(line, "a tilemap begins tilemap 1");
            }
            part = Part::Settings;
            continue;
        }
        if (part == Part::Done) {
            return refused(line, "a tilemap ends with its last row of tiles");
        }
        if (part == Part::Tiles) {
            if (kWords.size() != made.columns) {
                return refused(line, "a row of tiles holds " + std::to_string(made.columns) + " entries");
            }
            for (const std::string_view kWord : kWords) {
                if (kWord == ".") {
                    made.tiles.push_back(kEmpty);
                    continue;
                }
                const auto kTile = numberOf<std::uint16_t>(kWord);
                if (!kTile.has_value() || *kTile >= made.sheetColumns * made.sheetRows) {
                    return refused(line, "a tile is . or the index of one of the sheet's tiles");
                }
                made.tiles.push_back(*kTile);
            }
            if (made.tiles.size() == static_cast<std::size_t>(made.columns) * made.rows) {
                part = Part::Done;
            }
            continue;
        }
        if (kWords[0] == "tileset" && kWords.size() == 4 && !tiled) {
            const auto kTexture = kWords[1].size() == 16 ? numberOf<std::uint64_t>(kWords[1], 16) : std::nullopt;
            const auto kColumns = numberOf<std::uint32_t>(kWords[2]);
            const auto kRows = numberOf<std::uint32_t>(kWords[3]);
            if (!kTexture.has_value() || !kColumns.has_value() || !kRows.has_value() || *kColumns == 0 || *kRows == 0 ||
                static_cast<std::uint64_t>(*kColumns) * *kRows > limits.mostTiles) {
                return refused(line,
                               "a tileset is its texture's sixteen hexadecimal digits and the tiles across and down "
                               "its sheet, at most " +
                                   std::to_string(limits.mostTiles));
            }
            made.texture = *kTexture;
            made.sheetColumns = *kColumns;
            made.sheetRows = *kRows;
            tiled = true;
        } else if (kWords[0] == "cell" && kWords.size() == 2) {
            const auto kCell = metersOf(kWords[1]);
            if (!kCell.has_value() || !(*kCell > 0) || *kCell > limits.mostCell) {
                return refused(line, "a cell is above nought and within the limits, in meters");
            }
            made.cell = *kCell;
        } else if (kWords[0] == "solid") {
            for (std::size_t each = 1; each < kWords.size(); ++each) {
                const auto kTile = numberOf<std::uint16_t>(kWords[each]);
                if (!kTile.has_value() || !tiled || *kTile >= made.sheetColumns * made.sheetRows) {
                    return refused(line, "a solid tile is the index of one of its tileset's tiles");
                }
                made.solid.push_back(*kTile);
            }
        } else if (kWords[0] == "grid" && kWords.size() == 3 && made.columns == 0) {
            const auto kColumns = numberOf<std::uint32_t>(kWords[1]);
            const auto kRows = numberOf<std::uint32_t>(kWords[2]);
            if (!kColumns.has_value() || !kRows.has_value() || *kColumns == 0 || *kRows == 0 ||
                *kColumns > limits.mostCells || *kRows > limits.mostCells) {
                return refused(line, "a grid is 1 to " + std::to_string(limits.mostCells) + " cells each way");
            }
            made.columns = *kColumns;
            made.rows = *kRows;
        } else if (kWords[0] == "tiles" && kWords.size() == 1 && made.columns != 0 && tiled) {
            made.tiles.reserve(static_cast<std::size_t>(made.columns) * made.rows);
            part = Part::Tiles;
        } else {
            return refused(line, "a tilemap's settings are tileset, cell, solid, and grid lines, then its tiles");
        }
    }
    if (part != Part::Done) {
        return refused(line, "a tilemap holds a tileset, a grid, and every row of its tiles");
    }
    std::ranges::sort(made.solid);
    const auto [kFirst, kLast] = std::ranges::unique(made.solid);
    made.solid.erase(kFirst, kLast);
    return made;
}

std::string writeTilemap(const Tilemap& map) {
    char texture[17];
    std::snprintf(texture, sizeof texture, "%016llx", static_cast<unsigned long long>(map.texture));
    char cell[32];
    std::snprintf(cell, sizeof cell, "%.9g", map.cell);
    std::string made = std::string{"tilemap 1\ntileset "} + texture + " " + std::to_string(map.sheetColumns) + " " +
                       std::to_string(map.sheetRows) + "\ncell " + cell + "\n";
    if (!map.solid.empty()) {
        made += "solid";
        for (const std::uint16_t kTile : map.solid) {
            made += " " + std::to_string(kTile);
        }
        made += "\n";
    }
    made += "grid " + std::to_string(map.columns) + " " + std::to_string(map.rows) + "\ntiles\n";
    for (std::uint32_t row = 0; row < map.rows; ++row) {
        for (std::uint32_t column = 0; column < map.columns; ++column) {
            const std::uint16_t kTile = map.at(column, row);
            made += (column == 0 ? "" : " ") + (kTile == kEmpty ? std::string{"."} : std::to_string(kTile));
        }
        made += "\n";
    }
    return made;
}

std::size_t paint(Tilemap& map,
                  std::uint32_t column,
                  std::uint32_t row,
                  std::uint32_t width,
                  std::uint32_t height,
                  std::uint16_t tile) {
    std::size_t changed = 0;
    const std::uint64_t kRight = std::min<std::uint64_t>(static_cast<std::uint64_t>(column) + width, map.columns);
    const std::uint64_t kBottom = std::min<std::uint64_t>(static_cast<std::uint64_t>(row) + height, map.rows);
    for (std::uint64_t down = row; down < kBottom; ++down) {
        for (std::uint64_t across = column; across < kRight; ++across) {
            std::uint16_t& held = map.tiles[(down * map.columns) + across];
            changed += held != tile ? 1 : 0;
            held = tile;
        }
    }
    return changed;
}

std::vector<SolidRun> solidRuns(const Tilemap& map) {
    std::vector<SolidRun> made;
    const auto kSolid = [&map](std::uint16_t tile) {
        return tile != kEmpty && std::ranges::binary_search(map.solid, tile);
    };
    for (std::uint32_t row = 0; row < map.rows; ++row) {
        std::uint32_t column = 0;
        while (column < map.columns) {
            if (!kSolid(map.at(column, row))) {
                ++column;
                continue;
            }
            const std::uint32_t kStart = column;
            while (column < map.columns && kSolid(map.at(column, row))) {
                ++column;
            }
            made.push_back(SolidRun{.left = kStart * map.cell,
                                    .top = -(row * map.cell),
                                    .width = (column - kStart) * map.cell,
                                    .height = map.cell});
        }
    }
    return made;
}

SheetPlace placeOf(const Tilemap& map, std::uint16_t tile) noexcept {
    const std::uint32_t kColumns = std::max(map.sheetColumns, 1U);
    return SheetPlace{.column = tile % kColumns, .row = tile / kColumns};
}

} // namespace rawframe::tilemap
