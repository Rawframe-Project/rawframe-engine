#include "rawframe/tilemap/tilemap.h"
#include "shell.h"

#include <algorithm>
#include <array>
#include <cstdlib>

namespace rawframe::studio {

namespace {

/// The most blocks the map draws each way: a larger grid's cells are drawn
/// several to a block, and the most tiles listed as rows.
constexpr std::uint32_t kMostBlocks = 64;
constexpr std::uint32_t kMostListed = 64;
/// The map's widest side, in pixels, and a block's least and most.
constexpr float kMapSide = 560;
constexpr float kLeastBlock = 6;
constexpr float kMostBlock = 48;
/// The hue each of the first tiles is drawn in, then again; an empty cell
/// is the background.
constexpr std::array<std::uint32_t, 6> kTileTints = {
    0x6B5A3EFFU, 0x8A8178FFU, 0x5E8C4AFFU, 0x4A6E8CFFU, 0xB59A62FFU, 0x8C4A5EFFU};

/// A tile's name as the rows and the status say it.
std::string tileText(int tile) {
    return tile < 0 ? std::string{"none"} : "tile " + std::to_string(tile);
}

} // namespace

void ShellParticipant::showTilemap() {
    // A tilemap is its lines: its sheet's tiles as rows, the tile
    // painted in the Inspector, and its grid drawn in the center, a block
    // pressed painting the tile there through the session.
    const std::optional<Value> kRead = read(scene_, "lines.read");
    const Value* kLines = kRead.has_value() ? kRead->find("lines") : nullptr;
    std::string text;
    if (kLines != nullptr && kLines->kind() == Value::Kind::Array) {
        for (const Value& kLine : kLines->items()) {
            text += (kLine.text() != nullptr ? *kLine.text() : std::string{}) + "\n";
        }
    }
    const auto kMap = tilemap::parseTilemap(text);
    held_.tileBlocks.clear();
    if (!kMap.has_value()) {
        return;
    }
    const std::uint32_t kSheet = std::min(kMap->sheetColumns * kMap->sheetRows, kMostListed);
    held_.tileRows.clear();
    for (std::uint32_t each = 0; each <= kSheet; ++each) {
        // The sheet's tiles, then none, which takes tiles away.
        const int kTile = each == kSheet ? -1 : static_cast<int>(each);
        const bool kSolid = kTile >= 0 && std::ranges::binary_search(kMap->solid, static_cast<std::uint16_t>(kTile));
        const std::string kLabel = tileText(kTile) + (kTile < 0 ? " (erase)" : kSolid ? " (solid)" : " (tile)");
        auto added = row(entitiesColumn_, kLabel, kText, kRow, true);
        if (!added.has_value()) {
            return;
        }
        entityRows_.push_back(*added);
        entities_.push_back(std::to_string(kTile));
        names_.push_back(kLabel);
        brought_.push_back(false);
        removed_.push_back(false);
        places_.push_back(std::nullopt);
        held_.tileRows.push_back(kTile);
    }
    showTilePaint();
    clear(viewRows_);
    viewFields_.clear();
    clear(held_.cameraRows);
    const std::uint32_t kPer = (std::max(kMap->columns, kMap->rows) + kMostBlocks - 1) / kMostBlocks;
    const std::uint32_t kAcross = (kMap->columns + kPer - 1) / kPer;
    const std::uint32_t kDown = (kMap->rows + kPer - 1) / kPer;
    const float kBlock = std::clamp(kMapSide / static_cast<float>(std::max(kAcross, kDown)), kLeastBlock, kMostBlock);
    auto pane = box(
        centerColumn_,
        ui::Layout{
            .height = ui::pixels(0), .minHeight = ui::pixels(240), .grow = 1, .shrink = 0, .scroll = ui::Scroll::Both},
        kBackground);
    if (!pane.has_value()) {
        return;
    }
    viewRows_.push_back(*pane);
    static_cast<void>(tree_->setName(*pane, "Tiles"));
    auto sheet = box(*pane,
                     ui::Layout{.width = ui::pixels(static_cast<float>(kAcross) * kBlock),
                                .height = ui::pixels(static_cast<float>(kDown) * kBlock),
                                .shrink = 0},
                     kBackground);
    if (!sheet.has_value()) {
        return;
    }
    // Each block drawn in its first cell's tile, named by its place, so a
    // test presses one.
    for (std::uint32_t down = 0; down < kDown; ++down) {
        for (std::uint32_t across = 0; across < kAcross; ++across) {
            const std::uint32_t kLeft = across * kPer;
            const std::uint32_t kTop = down * kPer;
            const std::uint16_t kTile = kMap->at(kLeft, kTop);
            const std::uint32_t kFill = kTile == tilemap::kEmpty ? kPanel : kTileTints[kTile % kTileTints.size()];
            auto drawn = box(*sheet,
                             ui::Layout{.width = ui::pixels(kBlock - 1),
                                        .height = ui::pixels(kBlock - 1),
                                        .shrink = 0,
                                        .placement = ui::Placement{.absolute = true,
                                                                   .x = ui::pixels(static_cast<float>(across) * kBlock),
                                                                   .y = ui::pixels(static_cast<float>(down) * kBlock)}},
                             kFill);
            if (!drawn.has_value() || !tree_->setRole(*drawn, ui::Role::Button).has_value() ||
                !tree_->setName(*drawn, "cell " + std::to_string(across) + " " + std::to_string(down)).has_value()) {
                return;
            }
            // A block paints its cells, those within the grid.
            held_.tileBlocks.push_back(
                {*drawn, {kLeft, kTop, std::min(kPer, kMap->columns - kLeft), std::min(kPer, kMap->rows - kTop)}});
        }
    }
    emitter_.log(diagnostics::Severity::Info,
                 kTilemapShown,
                 "a tilemap's grid drawn, its sheet's tiles as rows",
                 {diagnostics::field("tilemap", scene_),
                  diagnostics::field("cells", kMap->tiles.size()),
                  diagnostics::field("sheet", kMap->sheetColumns * kMap->sheetRows),
                  diagnostics::field("blocks", held_.tileBlocks.size()),
                  diagnostics::field("painted", held_.tilesPainted)});
}

void ShellParticipant::showTilePaint() {
    clear(componentRows_);
    fields_.clear();
    actions_.clear();
    components_ = 0;
    if (auto heading = row(componentsColumn_, "Paint", kQuiet, kPanel); heading.has_value()) {
        componentRows_.push_back(*heading);
    }
    auto line = fieldRow("tile", held_.tilePaint < 0 ? std::string{"none"} : std::to_string(held_.tilePaint));
    if (!line.has_value()) {
        return;
    }
    componentRows_.push_back(line->first);
    fields_.push_back(FieldRow{.value = line->second, .role = FieldRow::Role::TileSetting, .field = "tile"});
    ++components_;
}

void ShellParticipant::showTileRow(std::size_t at) {
    // A tile chosen is what a block pressed paints.
    endEdit();
    entityAt_ = at;
    entity_ = entities_[at];
    for (std::size_t each = 0; each < entityRows_.size(); ++each) {
        static_cast<void>(
            tree_->setLook(entityRows_[each], ui::Look{.fill = each == at ? kChosen : kRow, .radius = 4}));
    }
    if (at < held_.tileRows.size()) {
        held_.tilePaint = held_.tileRows[at];
    }
    showTilePaint();
}

void ShellParticipant::setTilePaint(const std::string& text) {
    char* end = nullptr;
    const long kValue = std::strtol(text.c_str(), &end, 10);
    if (text == "none") {
        held_.tilePaint = -1;
    } else if (end != text.c_str() && *end == '\0' && kValue >= 0 && kValue < tilemap::kEmpty) {
        held_.tilePaint = static_cast<int>(kValue);
    } else {
        ++refused_;
        say("a tile is a tile's number of the sheet, or none");
        return;
    }
    say("paint set to " + tileText(held_.tilePaint));
    showTilePaint();
}

bool ShellParticipant::pressTiles(ui::Node node) {
    const auto kBlock =
        std::ranges::find(held_.tileBlocks, node, &std::pair<ui::Node, std::array<std::uint32_t, 4>>::first);
    if (!chosen(Listed::Tilemap) || kBlock == held_.tileBlocks.end()) {
        return false;
    }
    // One paint, the session's tilemap.paint, as an agent would send it.
    const auto& [kLeft, kTop, kWidth, kHeight] = kBlock->second;
    Value operation = Value::object();
    operation.add("operation", Value::string("tilemap.paint"));
    operation.add("column", Value::real(kLeft));
    operation.add("row", Value::real(kTop));
    operation.add("width", Value::real(kWidth));
    operation.add("height", Value::real(kHeight));
    operation.add("tile", Value::real(held_.tilePaint));
    ++held_.tilesPainted;
    const std::string kDone = (held_.tilePaint < 0 ? std::string{"cleared"} : "painted " + tileText(held_.tilePaint)) +
                              " at " + std::to_string(kLeft) + " " + std::to_string(kTop);
    commit(std::move(operation), kDone, std::nullopt);
    return true;
}

} // namespace rawframe::studio
