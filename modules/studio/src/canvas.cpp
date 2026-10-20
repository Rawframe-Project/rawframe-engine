#include "shell.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <map>

namespace rawframe::studio {

namespace {

constexpr float kNodeWidth = 190;
constexpr float kHeader = 28;
constexpr float kInputRow = 22;
constexpr float kBottom = 6;
constexpr float kColumnGap = 80;
constexpr float kRowGap = 28;
constexpr float kMargin = 28;
constexpr float kWire = 2;
constexpr float kPort = 9;
constexpr std::uint32_t kWireColor = 0x8F96A3FFU;
constexpr std::uint32_t kNodeFill = 0x1F2127FFU;

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

/// What a node is, as the design colors it (D923): a texture, an input
/// the material reads (a parameter, time, a position), its output, or
/// arithmetic; its dot's color and its header's.
struct NodeKind {
    std::uint32_t dot;
    std::uint32_t header;
};

NodeKind nodeKindOf(std::string_view type) {
    const auto kHas = [type](std::string_view part) {
        return type.find(part) != std::string_view::npos;
    };
    if (kHas("texture") || kHas("triplanar") || kHas("noise")) {
        return {0xD08A3AFFU, 0x3A2E1FFFU};
    }
    if (kHas("parameter") || kHas("time") || kHas("position") || kHas("uv") || kHas("color3") || kHas("vertex")) {
        return {0x6F7BD9FFU, 0x262B4AFFU};
    }
    if (kHas("surface") || kHas("output") || kHas("unlit")) {
        return {0x3FB950FFU, 0x1F3326FFU};
    }
    return {0x8A8D96FFU, 0x2A2C33FFU};
}

/// A constant an input holds, said short: a number rounded, a list of
/// them apart by spaces.
std::string constantText(const Value& value) {
    const auto kNumber = [](double number) {
        std::array<char, 32> text{};
        std::snprintf(text.data(), text.size(), "%.3g", number);
        return std::string{text.data()};
    };
    if (value.kind() == Value::Kind::Number) {
        return kNumber(value.real().value_or(0));
    }
    if (value.kind() == Value::Kind::Array) {
        std::string made;
        for (const Value& kItem : value.items()) {
            made += (made.empty() ? "" : " ") +
                    (kItem.kind() == Value::Kind::Number ? kNumber(kItem.real().value_or(0)) : std::string{"?"});
        }
        return made;
    }
    if (value.kind() == Value::Kind::String) {
        return *value.text();
    }
    if (value.kind() == Value::Kind::Bool) {
        return value.truth().value_or(false) ? "true" : "false";
    }
    return {};
}

} // namespace

std::string graphNodeLabel(const Value& node) {
    // A parameter by the name it is set by, as an instance names it.
    const Value* kParams = node.kind() == Value::Kind::Object ? node.find("params") : nullptr;
    const Value* kName = kParams != nullptr ? kParams->find("name") : nullptr;
    const Value* kType = node.find("type");
    std::string made = kName != nullptr && kName->text() != nullptr && kType != nullptr && kType->text() != nullptr &&
                               kType->text()->find("parameter") != std::string::npos
                           ? *kName->text()
                           : shortType(node);
    std::ranges::replace(made, '_', ' ');
    if (!made.empty()) {
        made.front() = static_cast<char>(std::toupper(static_cast<unsigned char>(made.front())));
    }
    return made;
}

std::uint32_t graphNodeDot(const Value& node) {
    const Value* kType = node.find("type");
    return nodeKindOf(kType != nullptr && kType->text() != nullptr ? std::string_view{*kType->text()}
                                                                   : std::string_view{})
        .dot;
}

void ShellParticipant::showCanvas(const Value& nodes, const std::string& prefix) {
    clear(viewRows_);
    viewFields_.clear();
    held_.canvasParts.clear();
    held_.canvasBoxes.clear();
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
    // Each node's outputs, as its type's signature lists them, where the
    // material module lists its type (D923).
    const Value* kTypes = nodeTypes_.has_value() ? nodeTypes_->find("types") : nullptr;
    const auto kOutputsOf = [kTypes](const Value& node) {
        std::vector<std::string> outputs;
        const Value* kType = node.find("type");
        for (const Value& kSignature : kTypes != nullptr ? kTypes->items() : std::span<const Value>{}) {
            const Value* kNamed = kSignature.find("type");
            if (kType == nullptr || kNamed == nullptr || kNamed->text() == nullptr || kType->text() == nullptr ||
                *kNamed->text() != *kType->text()) {
                continue;
            }
            const Value* kOutputs = kSignature.find("outputs");
            for (const Value& kOutput : kOutputs != nullptr ? kOutputs->items() : std::span<const Value>{}) {
                if (kOutput.text() != nullptr) {
                    outputs.push_back(*kOutput.text());
                }
            }
        }
        return outputs;
    };
    const auto kParamsOf = [](const Value& node) -> std::size_t {
        const Value* kParams = node.kind() == Value::Kind::Object ? node.find("params") : nullptr;
        return kParams != nullptr && kParams->kind() == Value::Kind::Object ? kParams->names().size() : 0;
    };
    const auto kHeightOf = [&](const Value& node) {
        const Value* kInputs = inputsOf(node);
        const std::size_t kRows =
            kParamsOf(node) + (kInputs != nullptr ? kInputs->items().size() : 0) + kOutputsOf(node).size();
        return kHeader + kInputRow * static_cast<float>(kRows) + kBottom;
    };
    std::map<std::string, std::pair<float, float>, std::less<>> placed;
    std::vector<float> filled;
    float wide = 0;
    float tall = 0;
    for (std::size_t each = 0; each < nodes.names().size(); ++each) {
        const std::size_t kColumn = column[nodes.names()[each]];
        filled.resize(std::max(filled.size(), kColumn + 1), kMargin);
        const float kHeight = kHeightOf(nodes.items()[each]);
        const float kX = kMargin + static_cast<float>(kColumn) * (kNodeWidth + kColumnGap);
        placed[nodes.names()[each]] = {kX, filled[kColumn]};
        filled[kColumn] += kHeight + kRowGap;
        wide = std::max(wide, kX + kNodeWidth + kMargin);
        tall = std::max(tall, filled[kColumn] + kMargin);
    }
    // A pane scrolling both ways, over a sheet as large as the graph laid
    // out, the nodes and wires placed on it.
    auto pane = box(
        centerColumn_,
        ui::Layout{
            .height = ui::pixels(0), .minHeight = ui::pixels(240), .grow = 1, .shrink = 0, .scroll = ui::Scroll::Both},
        0x15161AFFU);
    if (!pane.has_value()) {
        return;
    }
    viewRows_.push_back(*pane);
    static_cast<void>(tree_->setName(*pane, "Graph"));
    auto sheet = box(*pane, ui::Layout{.width = ui::pixels(wide), .height = ui::pixels(tall), .shrink = 0}, 0);
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
    // The wires first, under the nodes: from the output's port on the
    // source's right across half the gap, along to the input's port, and
    // in.
    for (std::size_t each = 0; each < nodes.names().size(); ++each) {
        const auto [kToX, kToY] = placed[nodes.names()[each]];
        const Value* kInputs = inputsOf(nodes.items()[each]);
        const float kInputsAt = kToY + kHeader + kInputRow * static_cast<float>(kParamsOf(nodes.items()[each]));
        for (std::size_t input = 0; kInputs != nullptr && input < kInputs->items().size(); ++input) {
            const std::string* kFrom = sourceOf(kInputs->items()[input]);
            const auto kSource = kFrom != nullptr ? placed.find(*kFrom) : placed.end();
            if (kSource == placed.end()) {
                continue;
            }
            ++wires;
            const Value& kSourceNode = *nodes.find(*kFrom);
            const std::vector<std::string> kOutputs = kOutputsOf(kSourceNode);
            const Value* kOutput = kInputs->items()[input].find("output");
            const auto kOutputAt = kOutput != nullptr && kOutput->text() != nullptr
                                       ? std::ranges::find(kOutputs, *kOutput->text())
                                       : kOutputs.end();
            const float kOutputRow = kOutputAt != kOutputs.end() ? static_cast<float>(kOutputAt - kOutputs.begin()) : 0;
            const Value* kSourceInputs = inputsOf(kSourceNode);
            const float kX1 = kSource->second.first + kNodeWidth;
            const float kY1 =
                kOutputs.empty()
                    ? kSource->second.second + kHeader / 2
                    : kSource->second.second + kHeader +
                          kInputRow *
                              (static_cast<float>(kParamsOf(kSourceNode) +
                                                  (kSourceInputs != nullptr ? kSourceInputs->items().size() : 0)) +
                               kOutputRow + 0.5F);
            const float kY2 = kInputsAt + kInputRow * (static_cast<float>(input) + 0.5F);
            const float kMiddle = std::max(kX1 + kColumnGap / 2, kToX - kColumnGap / 2);
            for (const auto& [kX, kY, kWidth, kHeight] :
                 {std::array<float, 4>{kX1, kY1 - kWire / 2, kMiddle - kX1 + kWire, kWire},
                  std::array<float, 4>{kMiddle, std::min(kY1, kY2) - kWire / 2, kWire, std::abs(kY2 - kY1) + kWire},
                  std::array<float, 4>{kMiddle, kY2 - kWire / 2, std::max(kToX - kMiddle, kWire), kWire}}) {
                static_cast<void>(box(*sheet, kPlace(kX, kY, kWidth, kHeight), kWireColor));
            }
        }
    }
    // Each node a card as the design's (D923): a header in its kind's
    // color with its dot and name, standing for its row in the Hierarchy;
    // its params' values; its inputs, each with its port on the left and
    // the constant it holds; its outputs at the right with their ports.
    for (std::size_t each = 0; each < nodes.names().size(); ++each) {
        const std::string& kId = nodes.names()[each];
        const auto kListed = std::ranges::find(entities_, prefix + kId);
        if (kListed == entities_.end()) {
            continue;
        }
        const Value& kNodeValue = nodes.items()[each];
        const std::size_t kEntity = static_cast<std::size_t>(kListed - entities_.begin());
        const auto [kX, kY] = placed[kId];
        const Value* kInputs = inputsOf(kNodeValue);
        const Value* kType = kNodeValue.find("type");
        const NodeKind kKind = nodeKindOf(
            kType != nullptr && kType->text() != nullptr ? std::string_view{*kType->text()} : std::string_view{});
        ui::Layout placing = kPlace(kX, kY, kNodeWidth, kHeightOf(kNodeValue));
        placing.direction = ui::Direction::Column;
        placing.border = {1, 1, 1, 1};
        auto node = box(*sheet, placing, kNodeFill);
        if (!node.has_value()) {
            return;
        }
        const bool kChosenNode = prefix + kId == entity_;
        static_cast<void>(tree_->setLook(
            *node, ui::Look{.fill = kNodeFill, .borderColor = kChosenNode ? look::kAccent : look::kEdge, .radius = 6}));
        held_.canvasBoxes.emplace_back(*node, kEntity);
        held_.canvasParts.emplace_back(*node, kEntity);
        auto header = box(*node,
                          ui::Layout{.height = ui::pixels(kHeader),
                                     .alignItems = ui::Align::Center,
                                     .gap = 7,
                                     .shrink = 0,
                                     .padding = {9, 8, 0, 0}},
                          0);
        if (!header.has_value()) {
            return;
        }
        static_cast<void>(tree_->setLook(*header, ui::Look{.fill = kKind.header, .radius = 5}));
        if (auto dot = box(*header, ui::Layout{.width = ui::pixels(8), .height = ui::pixels(8), .shrink = 0}, 0);
            dot.has_value()) {
            static_cast<void>(tree_->setLook(*dot, ui::Look{.fill = kKind.dot, .radius = 2}));
            static_cast<void>(tree_->setInteraction(*dot, ui::Interaction{.hits = ui::Interaction::Hits::Nothing}));
        }
        auto title = box(*header, ui::Layout{.shrink = 1}, 0);
        // It says its type; it is named by its type and its whole id, as
        // two nodes of one graph may share their first digits.
        if (!title.has_value() || !strongWords(*title, graphNodeLabel(kNodeValue), look::kInk, 13).has_value() ||
            !tree_->setInteraction(*title, ui::Interaction{.hits = ui::Interaction::Hits::Nothing}).has_value() ||
            !tree_->setRole(*header, ui::Role::Button).has_value() ||
            !tree_->setName(*header, shortType(kNodeValue) + " " + kId).has_value()) {
            return;
        }
        held_.canvasParts.emplace_back(*header, kEntity);
        const auto kLine = [&](std::string_view name, std::string_view value, int port) -> std::optional<ui::Node> {
            auto line = box(*node,
                            ui::Layout{.height = ui::pixels(kInputRow),
                                       .justify = port > 0 ? ui::Justify::End : ui::Justify::Start,
                                       .alignItems = ui::Align::Center,
                                       .gap = 8,
                                       .shrink = 0,
                                       .padding = {14, 14, 0, 0}},
                            0);
            if (!line.has_value()) {
                return std::nullopt;
            }
            if (auto said = box(*line, ui::Layout{.shrink = 0}, 0); said.has_value()) {
                static_cast<void>(words(*said, name, port == 0 ? look::kFaintInk : look::kSoftInk, 12));
                static_cast<void>(
                    tree_->setInteraction(*said, ui::Interaction{.hits = ui::Interaction::Hits::Nothing}));
            }
            if (!value.empty()) {
                if (auto held = box(*line, ui::Layout{.shrink = 1}, 0); held.has_value()) {
                    static_cast<void>(monoWords(*held, value, look::kLabelInk, look::kSmallSize));
                    static_cast<void>(
                        tree_->setInteraction(*held, ui::Interaction{.hits = ui::Interaction::Hits::Nothing}));
                }
            }
            // Its port on the card's edge, a ring where nothing is wired.
            if (port != 0) {
                if (auto dot = box(*line,
                                   ui::Layout{.width = ui::pixels(kPort),
                                              .height = ui::pixels(kPort),
                                              .shrink = 0,
                                              .border = {2, 2, 2, 2},
                                              .placement = {.absolute = true,
                                                            .x = port < 0 ? ui::pixels(-kPort / 2 - 1) : ui::share(1),
                                                            .y = ui::share(0.5),
                                                            .anchorX = port < 0 ? 0.0F : 0.5F,
                                                            .anchorY = 0.5F}},
                                   0);
                    dot.has_value()) {
                    const bool kWired = port == -2 || port == 2;
                    static_cast<void>(tree_->setLook(
                        *dot, ui::Look{.fill = kWired ? kKind.dot : kNodeFill, .borderColor = kKind.dot, .radius = 5}));
                    static_cast<void>(
                        tree_->setInteraction(*dot, ui::Interaction{.hits = ui::Interaction::Hits::Nothing}));
                }
            }
            held_.canvasParts.emplace_back(*line, kEntity);
            return *line;
        };
        const Value* kParams = kNodeValue.find("params");
        for (std::size_t param = 0;
             kParams != nullptr && kParams->kind() == Value::Kind::Object && param < kParams->names().size();
             ++param) {
            if (!kLine(kParams->names()[param], constantText(kParams->items()[param]), 0).has_value()) {
                return;
            }
        }
        for (std::size_t input = 0; kInputs != nullptr && input < kInputs->items().size(); ++input) {
            const bool kWired = sourceOf(kInputs->items()[input]) != nullptr;
            auto line = kLine(kInputs->names()[input],
                              kWired ? std::string{} : constantText(kInputs->items()[input]),
                              kWired ? -2 : -1);
            // Read as the node's input, not as the Inspector's field of
            // that name.
            if (!line.has_value() || !tree_->setName(*line, kInputs->names()[input] + " input").has_value()) {
                return;
            }
        }
        for (const std::string& kOutput : kOutputsOf(kNodeValue)) {
            if (!kLine(kOutput, {}, 2).has_value()) {
                return;
            }
        }
    }
    emitter_.log(diagnostics::Severity::Info,
                 kCanvasShown,
                 "a graph's nodes and wires laid out in the center",
                 {diagnostics::field("nodes", held_.canvasBoxes.size()),
                  diagnostics::field("wires", wires),
                  diagnostics::field("columns", filled.size())});
}

std::optional<std::size_t> ShellParticipant::canvasEntityOf(ui::Node node) const {
    // The center's parts, and the environment panel's entities (D834).
    for (const auto* kParts : {&held_.canvasParts, &held_.environmentParts}) {
        const auto kPart = std::ranges::find(*kParts, node, &std::pair<ui::Node, std::size_t>::first);
        if (kPart != kParts->end()) {
            return kPart->second;
        }
    }
    return std::nullopt;
}

void ShellParticipant::markCanvas() {
    for (const auto& [kBox, kEntity] : held_.canvasBoxes) {
        static_cast<void>(tree_->setLook(kBox,
                                         ui::Look{.fill = kNodeFill,
                                                  .borderColor = kEntity == entityAt_ ? look::kAccent : look::kEdge,
                                                  .radius = 6}));
    }
}

} // namespace rawframe::studio
