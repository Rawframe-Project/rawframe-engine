#include "rawframe/terrain/terrain.h"
#include "shell.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace rawframe::studio {

namespace {

/// The most blocks the map draws each way: a larger grid's cells are drawn
/// several to a block.
constexpr std::uint32_t kMostBlocks = 64;
/// The map's widest side, in pixels, and a block's least and most.
constexpr float kMapSide = 560;
constexpr float kLeastBlock = 6;
constexpr float kMostBlock = 48;
/// The hue each of the first layers tints its cells with, then again.
constexpr std::array<std::uint32_t, 4> kLayerTints = {0x5E8C4AFFU, 0x8A8178FFU, 0xB59A62FFU, 0x4A6E8CFFU};

/// `tint` darkened or lightened by `light`, from nought to one.
std::uint32_t shaded(std::uint32_t tint, double light) {
    const auto kChannel = [&](int shift) {
        const double kValue = static_cast<double>((tint >> shift) & 0xFFU) * (0.45 + (0.75 * light));
        return static_cast<std::uint32_t>(std::clamp(kValue, 0.0, 255.0)) << shift;
    };
    return kChannel(24) | kChannel(16) | kChannel(8) | 0xFFU;
}

std::string metersText(double value) {
    char made[32];
    std::snprintf(made, sizeof made, "%g", value);
    return made;
}

} // namespace

void ShellParticipant::showTerrain() {
    // A terrain is its lines (D855): its layers as rows, the brush in the
    // Inspector, and its ground drawn from above in the center, a block
    // pressed stroking the brush there through the session.
    const std::optional<Value> kRead = read(scene_, "lines.read");
    const Value* kLines = kRead.has_value() ? kRead->find("lines") : nullptr;
    std::string text;
    if (kLines != nullptr && kLines->kind() == Value::Kind::Array) {
        for (const Value& kLine : kLines->items()) {
            text += (kLine.text() != nullptr ? *kLine.text() : std::string{}) + "\n";
        }
    }
    const auto kTerrain = terrain::parseTerrain(text);
    held_.mapBlocks.clear();
    if (!kTerrain.has_value()) {
        return;
    }
    for (std::size_t each = 0; each < kTerrain->layers.size(); ++each) {
        const std::string kLabel = kTerrain->layers[each].name + " (layer)";
        auto added = row(entitiesColumn_, kLabel, kText, kRow, true);
        if (!added.has_value()) {
            return;
        }
        entityRows_.push_back(*added);
        entities_.push_back(std::to_string(each));
        names_.push_back(kLabel);
        brought_.push_back(false);
        removed_.push_back(false);
        places_.push_back(std::nullopt);
    }
    showBrush();
    clear(viewRows_);
    viewFields_.clear();
    const std::uint32_t kCells = std::max(kTerrain->columns, kTerrain->rows) - 1;
    const std::uint32_t kPer = (kCells + kMostBlocks - 1) / kMostBlocks;
    const std::uint32_t kAcross = (kTerrain->columns - 1 + kPer - 1) / kPer;
    const std::uint32_t kDown = (kTerrain->rows - 1 + kPer - 1) / kPer;
    const float kBlock = std::clamp(kMapSide / static_cast<float>(std::max(kAcross, kDown)), kLeastBlock, kMostBlock);
    const auto [kLowest, kHighest] = std::ranges::minmax(kTerrain->heights);
    auto pane = box(
        centerColumn_,
        ui::Layout{
            .height = ui::pixels(0), .minHeight = ui::pixels(240), .grow = 1, .shrink = 0, .scroll = ui::Scroll::Both},
        kBackground);
    if (!pane.has_value()) {
        return;
    }
    viewRows_.push_back(*pane);
    static_cast<void>(tree_->setName(*pane, "Map"));
    auto sheet = box(*pane,
                     ui::Layout{.width = ui::pixels(static_cast<float>(kAcross) * kBlock),
                                .height = ui::pixels(static_cast<float>(kDown) * kBlock),
                                .shrink = 0},
                     kBackground);
    if (!sheet.has_value()) {
        return;
    }
    // Each block shaded by the height at its first cell's corner between
    // the lowest and the highest, tinted by that cell's layer; named by
    // its place, so a test presses one.
    for (std::uint32_t down = 0; down < kDown; ++down) {
        for (std::uint32_t across = 0; across < kAcross; ++across) {
            const std::uint32_t kColumn = across * kPer;
            const std::uint32_t kSampleRow = down * kPer;
            const std::int32_t kHeight = kTerrain->heights[(kSampleRow * kTerrain->columns) + kColumn];
            const double kLight =
                kHighest > kLowest ? static_cast<double>(kHeight - kLowest) / (kHighest - kLowest) : 0.5;
            const std::uint8_t kLayer = kTerrain->painted[(kSampleRow * (kTerrain->columns - 1)) + kColumn];
            auto drawn = box(*sheet,
                             ui::Layout{.width = ui::pixels(kBlock),
                                        .height = ui::pixels(kBlock),
                                        .shrink = 0,
                                        .placement = ui::Placement{.absolute = true,
                                                                   .x = ui::pixels(static_cast<float>(across) * kBlock),
                                                                   .y = ui::pixels(static_cast<float>(down) * kBlock)}},
                             shaded(kLayerTints[kLayer % kLayerTints.size()], kLight));
            if (!drawn.has_value() || !tree_->setRole(*drawn, ui::Role::Button).has_value() ||
                !tree_->setName(*drawn, "map " + std::to_string(across) + " " + std::to_string(down)).has_value()) {
                return;
            }
            // The brush strokes at the block's middle, in meters.
            const double kSpan = static_cast<double>(kPer) * kTerrain->cell;
            held_.mapBlocks.push_back({*drawn, {(across * kSpan) + (kSpan / 2), (down * kSpan) + (kSpan / 2)}});
        }
    }
    emitter_.log(diagnostics::Severity::Info,
                 kTerrainShown,
                 "a terrain's ground drawn from above, its layers as rows",
                 {diagnostics::field("terrain", scene_),
                  diagnostics::field("samples", kTerrain->heights.size()),
                  diagnostics::field("layers", kTerrain->layers.size()),
                  diagnostics::field("blocks", held_.mapBlocks.size()),
                  diagnostics::field("strokes", held_.strokes)});
}

void ShellParticipant::showBrush() {
    clear(componentRows_);
    fields_.clear();
    actions_.clear();
    components_ = 0;
    if (auto heading = row(componentsColumn_, "Brush", kQuiet, kPanel); heading.has_value()) {
        componentRows_.push_back(*heading);
    }
    for (const auto& [kName, kValue] : {std::pair{std::string{"tool"}, held_.brushTool},
                                        std::pair{std::string{"radius"}, metersText(held_.brushRadius)},
                                        std::pair{std::string{"amount"}, metersText(held_.brushAmount)}}) {
        auto line = fieldRow(kName, kValue);
        if (!line.has_value()) {
            return;
        }
        componentRows_.push_back(line->first);
        fields_.push_back(FieldRow{.value = line->second, .role = FieldRow::Role::BrushSetting, .field = kName});
    }
    ++components_;
}

void ShellParticipant::showTerrainLayer(std::size_t at) {
    // A layer chosen is what the brush paints.
    endEdit();
    entityAt_ = at;
    entity_ = entities_[at];
    for (std::size_t each = 0; each < entityRows_.size(); ++each) {
        static_cast<void>(
            tree_->setLook(entityRows_[each], ui::Look{.fill = each == at ? kChosen : kRow, .radius = 4}));
    }
    held_.brushTool = "paint";
    held_.brushAmount = static_cast<double>(at);
    showBrush();
}

void ShellParticipant::setBrush(const FieldRow& row, const std::string& text) {
    if (row.field == "tool") {
        held_.brushTool = text;
    } else {
        char* end = nullptr;
        const double kValue = std::strtod(text.c_str(), &end);
        if (end == text.c_str() || !std::isfinite(kValue)) {
            ++refused_;
            say("a brush's " + row.field + " is a number");
            return;
        }
        (row.field == "radius" ? held_.brushRadius : held_.brushAmount) = kValue;
    }
    say("brush " + row.field + " set to " + text);
    showBrush();
}

bool ShellParticipant::pressMap(ui::Node node) {
    const auto kBlock = std::ranges::find(held_.mapBlocks, node, &std::pair<ui::Node, std::array<double, 2>>::first);
    if (!chosen(Listed::Terrain) || kBlock == held_.mapBlocks.end()) {
        return false;
    }
    // One stroke, the session's terrain.brush, as an agent would send it.
    Value operation = Value::object();
    operation.add("operation", Value::string("terrain.brush"));
    operation.add("tool", Value::string(held_.brushTool));
    operation.add("x", Value::real(kBlock->second[0]));
    operation.add("z", Value::real(kBlock->second[1]));
    operation.add("radius", Value::real(held_.brushRadius));
    operation.add("amount", Value::real(held_.brushAmount));
    ++held_.strokes;
    const std::string kDone = held_.brushTool + (held_.brushTool.ends_with('e') ? "d" : "ed") + " at " +
                              metersText(kBlock->second[0]) + " " + metersText(kBlock->second[1]);
    commit(std::move(operation), kDone, std::nullopt);
    return true;
}

} // namespace rawframe::studio
