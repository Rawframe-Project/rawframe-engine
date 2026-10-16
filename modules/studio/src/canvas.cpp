#include "shell.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <map>

namespace rawframe::studio {

namespace {

constexpr float kNodeWidth = 200;
constexpr float kHeader = 26;
constexpr float kInputRow = 20;
constexpr float kColumnGap = 64;
constexpr float kRowGap = 20;
constexpr float kMargin = 16;
constexpr float kWire = 2;
constexpr std::uint32_t kWireColor = 0x7FA7D9FFU;

/// The node an input is wired from, if it is wired.
const std::string* sourceOf(const Value& input) {
    const Value* kNode = input.kind() == Value::Kind::Object ? input.find("node") : nullptr;
    return kNode != nullptr && kNode->kind() == Value::Kind::String ? kNode->text() : nullptr;
}

/// A node's inputs by name, or none where it holds no object of them: an
/// array's items have no names to show.
const Value* inputsOf(const Value& node) {
    const Value* kInputs = node.kind() == Value::Kind::Object ? node.find("inputs") : nullptr;
    return kInputs != nullptr && kInputs->kind() == Value::Kind::Object ? kInputs : nullptr;
}

/// A node's type said short: its namespace and version left off.
std::string shortType(const Value& node) {
    const Value* kType = node.find("type");
    std::string made = kType != nullptr && kType->kind() == Value::Kind::String ? *kType->text() : std::string{};
    if (const std::size_t kSlash = made.rfind('/'); kSlash != std::string::npos) {
        made = made.substr(kSlash + 1);
    }
    if (const std::size_t kAt = made.find('@'); kAt != std::string::npos) {
        made.resize(kAt);
    }
    return made;
}

} // namespace

void ShellParticipant::showCanvas(const Value& nodes, const std::string& prefix) {
    clear(viewRows_);
    viewFields_.clear();
    canvasParts_.clear();
    canvasBoxes_.clear();
    if (nodes.kind() != Value::Kind::Object || nodes.names().empty()) {
        return;
    }
    // Each node's column the longest run of wires into it, so every wire
    // runs left to right; its row its place among its column's, in the
    // document's order. A wire from a node the graph does not hold, or a
    // loop, adds nothing: the graph's own reader refuses those.
    std::map<std::string, std::size_t, std::less<>> column;
    for (std::size_t pass = 0; pass <= nodes.names().size(); ++pass) {
        bool moved = false;
        for (std::size_t each = 0; each < nodes.names().size(); ++each) {
            std::size_t at = 0;
            const Value* kInputs = inputsOf(nodes.items()[each]);
            for (std::size_t input = 0; kInputs != nullptr && input < kInputs->items().size(); ++input) {
                const std::string* kFrom = sourceOf(kInputs->items()[input]);
                if (kFrom != nullptr && nodes.find(*kFrom) != nullptr && *kFrom != nodes.names()[each]) {
                    at = std::max(at, column[*kFrom] + 1);
                }
            }
            if (at > nodes.names().size()) {
                at = nodes.names().size();
            }
            if (column[nodes.names()[each]] != at) {
                column[nodes.names()[each]] = at;
                moved = true;
            }
        }
        if (!moved) {
            break;
        }
    }
    std::map<std::string, std::pair<float, float>, std::less<>> placed;
    std::vector<float> filled;
    float wide = 0;
    float tall = 0;
    for (std::size_t each = 0; each < nodes.names().size(); ++each) {
        const std::size_t kColumn = column[nodes.names()[each]];
        filled.resize(std::max(filled.size(), kColumn + 1), kMargin);
        const Value* kInputs = inputsOf(nodes.items()[each]);
        const float kHeight =
            kHeader + kInputRow * static_cast<float>(kInputs != nullptr ? kInputs->items().size() : 0);
        const float kX = kMargin + static_cast<float>(kColumn) * (kNodeWidth + kColumnGap);
        placed[nodes.names()[each]] = {kX, filled[kColumn]};
        filled[kColumn] += kHeight + kRowGap;
        wide = std::max(wide, kX + kNodeWidth + kMargin);
        tall = std::max(tall, filled[kColumn]);
    }
    // A pane scrolling both ways, over a sheet as large as the graph laid
    // out, the nodes and wires placed on it.
    auto pane = box(
        centerColumn_,
        ui::Layout{
            .height = ui::pixels(0), .minHeight = ui::pixels(240), .grow = 1, .shrink = 0, .scroll = ui::Scroll::Both},
        kBackground);
    if (!pane.has_value()) {
        return;
    }
    viewRows_.push_back(*pane);
    static_cast<void>(tree_->setName(*pane, "Graph"));
    auto sheet =
        box(*pane, ui::Layout{.width = ui::pixels(wide), .height = ui::pixels(tall), .shrink = 0}, kBackground);
    if (!sheet.has_value()) {
        return;
    }
    const auto kPlace = [](float x, float y, float width, float height) {
        return ui::Layout{.width = ui::pixels(width),
                          .height = ui::pixels(height),
                          .shrink = 0,
                          .placement = ui::Placement{.absolute = true, .x = ui::pixels(x), .y = ui::pixels(y)}};
    };
    std::size_t wires = 0;
    // The wires first, under the nodes: from a node's right edge at its
    // header across half the gap, along to the input's row, and in.
    for (std::size_t each = 0; each < nodes.names().size(); ++each) {
        const auto [kToX, kToY] = placed[nodes.names()[each]];
        const Value* kInputs = inputsOf(nodes.items()[each]);
        for (std::size_t input = 0; kInputs != nullptr && input < kInputs->items().size(); ++input) {
            const std::string* kFrom = sourceOf(kInputs->items()[input]);
            const auto kSource = kFrom != nullptr ? placed.find(*kFrom) : placed.end();
            if (kSource == placed.end()) {
                continue;
            }
            ++wires;
            const float kX1 = kSource->second.first + kNodeWidth;
            const float kY1 = kSource->second.second + kHeader / 2;
            const float kY2 = kToY + kHeader + kInputRow * (static_cast<float>(input) + 0.5F);
            const float kMiddle = std::max(kX1 + kColumnGap / 2, kToX - kColumnGap / 2);
            for (const auto& [kX, kY, kWidth, kHeight] :
                 {std::array<float, 4>{kX1, kY1 - kWire / 2, kMiddle - kX1 + kWire, kWire},
                  std::array<float, 4>{kMiddle, std::min(kY1, kY2) - kWire / 2, kWire, std::abs(kY2 - kY1) + kWire},
                  std::array<float, 4>{kMiddle, kY2 - kWire / 2, std::max(kToX - kMiddle, kWire), kWire}}) {
                static_cast<void>(box(*sheet, kPlace(kX, kY, kWidth, kHeight), kWireColor));
            }
        }
    }
    // Each node a box: its type and its id's first digits as a button
    // standing for its row in the Hierarchy, then its inputs by name.
    for (std::size_t each = 0; each < nodes.names().size(); ++each) {
        const std::string& kId = nodes.names()[each];
        const auto kListed = std::ranges::find(entities_, prefix + kId);
        if (kListed == entities_.end()) {
            continue;
        }
        const std::size_t kEntity = static_cast<std::size_t>(kListed - entities_.begin());
        const auto [kX, kY] = placed[kId];
        const Value* kInputs = inputsOf(nodes.items()[each]);
        const float kHeight =
            kHeader + kInputRow * static_cast<float>(kInputs != nullptr ? kInputs->items().size() : 0);
        ui::Layout placing = kPlace(kX, kY, kNodeWidth, kHeight);
        placing.direction = ui::Direction::Column;
        auto node = box(*sheet, placing, prefix + kId == entity_ ? kChosen : kRow);
        if (!node.has_value()) {
            return;
        }
        canvasBoxes_.emplace_back(*node, kEntity);
        canvasParts_.emplace_back(*node, kEntity);
        auto header =
            box(*node, ui::Layout{.height = ui::pixels(kHeader), .shrink = 0, .padding = {8, 4, 8, 4}}, kHeaderFill);
        // It says its type; it is named by its type and its whole id, as
        // two nodes of one graph may share their first digits.
        if (!header.has_value() || !words(*header, shortType(nodes.items()[each]), kText, 13).has_value() ||
            !tree_->setRole(*header, ui::Role::Button).has_value() ||
            !tree_->setName(*header, shortType(nodes.items()[each]) + " " + kId).has_value()) {
            return;
        }
        canvasParts_.emplace_back(*header, kEntity);
        for (std::size_t input = 0; kInputs != nullptr && input < kInputs->items().size(); ++input) {
            auto line =
                box(*node, ui::Layout{.height = ui::pixels(kInputRow), .shrink = 0, .padding = {10, 2, 8, 2}}, 0);
            // Read as the node's input, not as the Inspector's field of
            // that name.
            if (!line.has_value() || !words(*line, kInputs->names()[input], kQuiet, 12).has_value() ||
                !tree_->setName(*line, kInputs->names()[input] + " input").has_value()) {
                return;
            }
            canvasParts_.emplace_back(*line, kEntity);
        }
    }
    emitter_.log(diagnostics::Severity::Info,
                 kCanvasShown,
                 "a graph's nodes and wires laid out in the center",
                 {diagnostics::field("nodes", canvasBoxes_.size()),
                  diagnostics::field("wires", wires),
                  diagnostics::field("columns", filled.size())});
}

std::optional<std::size_t> ShellParticipant::canvasEntityOf(ui::Node node) const {
    const auto kPart = std::ranges::find(canvasParts_, node, &std::pair<ui::Node, std::size_t>::first);
    return kPart != canvasParts_.end() ? std::optional{kPart->second} : std::nullopt;
}

void ShellParticipant::markCanvas() {
    for (const auto& [kBox, kEntity] : canvasBoxes_) {
        static_cast<void>(tree_->setLook(kBox, ui::Look{.fill = kEntity == entityAt_ ? kChosen : kRow, .radius = 4}));
    }
}

} // namespace rawframe::studio
