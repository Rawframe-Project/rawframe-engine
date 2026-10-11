// A `ui.document` (D619): read as a tree of node components, each with the
// fields it starts as and the words it shows; and refused, naming the
// field, where it is not canonical, out of range, or past its limits.

#include "rawframe/test/test.h"
#include "rawframe/ui/document.h"

#include <cstring>
#include <string>
#include <string_view>

using namespace rawframe;
using namespace rawframe::ui;

namespace {

constexpr std::string_view kDocument = R"({
  "kind": "ui.document",
  "formatVersion": 1,
  "nodes": [
    {
      "component": "game.hud",
      "widthOffset": 240,
      "alignItems": "center",
      "padding": 6,
      "absolute": true,
      "xScale": 0.5,
      "yOffset": -12,
      "fill": "#101820c0",
      "children": [
        {
          "component": "game.points",
          "order": 2,
          "text": "f1182d0420cc09a0",
          "textValue": -3,
          "textSize": 14,
          "textWrap": "no_wrap"
        },
        {
          "component": "game.sign",
          "shows": "game.words",
          "press": 7,
          "hit": "self",
          "edit": "line",
          "editLimit": 32,
          "style": "139f1a51051d860e"
        }
      ]
    },
    {
      "component": "game.banner",
      "layer": "overlay"
    }
  ]
}
)";

std::string with(std::string_view from, std::string_view to) {
    std::string text{kDocument};
    if (const std::size_t kAt = text.find(from); kAt != std::string::npos) {
        text.replace(kAt, from.size(), to);
    }
    return text;
}

/// Refused, its path naming `field`.
bool refused(const std::string& text, std::string_view field, const DocumentLimits& limits = {}) {
    const auto kRead = readDocument(text, limits);
    if (kRead.has_value()) {
        return false;
    }
    for (const auto& kContext : kRead.error().context()) {
        if (kContext.key == "path" && kContext.value.find(field) != std::string_view::npos) {
            return true;
        }
    }
    return false;
}

} // namespace

RAWFRAME_TEST(AUiDocumentIsReadAsATreeOfComponents) {
    const auto kRead = readDocument(kDocument);
    RAWFRAME_EXPECT(kRead.has_value());
    if (!kRead.has_value()) {
        return;
    }
    const std::vector<DocumentNode>& kNodes = kRead->nodes;
    RAWFRAME_EXPECT(kNodes.size() == 4);
    if (kNodes.size() != 4) {
        return;
    }
    // Each before those inside it, siblings as written.
    RAWFRAME_EXPECT(kNodes[0].component == "game.hud" && !kNodes[0].parent.has_value());
    RAWFRAME_EXPECT(kNodes[1].component == "game.points" && kNodes[1].parent == 0);
    RAWFRAME_EXPECT(kNodes[2].component == "game.sign" && kNodes[2].parent == 0 && kNodes[2].shows == "game.words");
    RAWFRAME_EXPECT(kNodes[3].component == "game.banner" && !kNodes[3].parent.has_value() && kNodes[3].shows.empty());
    const NodeFields& kHud = kNodes[0].fields;
    RAWFRAME_EXPECT(kHud.widthOffset == 240 && kHud.alignItems == 4 && kHud.padding == 6 && kHud.absolute == 1 &&
                    kHud.xScale == 0.5F && kHud.yOffset == -12 && kHud.fill == 0x101820C0 && kHud.direction == 0);
    const NodeFields& kPoints = kNodes[1].fields;
    RAWFRAME_EXPECT(kPoints.order == 2 && kPoints.text == 0xF1182D0420CC09A0 && kPoints.textValue == -3 &&
                    kPoints.textSize == 14 && kPoints.textWrap == 1);
    const NodeFields& kSign = kNodes[2].fields;
    RAWFRAME_EXPECT(kSign.press == 7 && kSign.hit == 1 && kSign.edit == 1 && kSign.editLimit == 32 &&
                    kSign.style == 0x139F1A51051D860E);
    RAWFRAME_EXPECT(kNodes[3].fields.layer == 3);
    // Every field left out is nought, as presentation attaches a node.
    NodeFields banner;
    banner.layer = 3;
    RAWFRAME_EXPECT(std::memcmp(&banner, &kNodes[3].fields, sizeof banner) == 0);
}

RAWFRAME_TEST(AUiDocumentIsRefusedWhereItIsWrong) {
    RAWFRAME_EXPECT(readDocument(kDocument).has_value());
    RAWFRAME_EXPECT(refused(with("\"formatVersion\": 1", "\"formatVersion\": 2"), "formatVersion"));
    RAWFRAME_EXPECT(refused(with("ui.document", "ui.styles"), "kind"));
    // A component twice, a bad name, a node showing its own words.
    RAWFRAME_EXPECT(refused(with("\"game.banner\"", "\"game.hud\""), "nodes[1].component"));
    RAWFRAME_EXPECT(refused(with("\"game.banner\"", "\"9banner\""), "nodes[1].component"));
    RAWFRAME_EXPECT(refused(with("\"game.words\"", "\"game.sign\""), "children[1].shows"));
    // Values: a case past its set, a default written, a color, an identity,
    // a number out of range, a field holding too much, a truth at false.
    RAWFRAME_EXPECT(refused(with("\"center\"", "\"middle\""), "alignItems"));
    RAWFRAME_EXPECT(refused(with("\"center\"", "\"auto\""), "alignItems"));
    RAWFRAME_EXPECT(refused(with("#101820c0", "#101820C0"), "fill"));
    RAWFRAME_EXPECT(refused(with("#101820c0", "#00000000"), "fill"));
    RAWFRAME_EXPECT(refused(with("f1182d0420cc09a0", "f1182d0420cc09a"), "text"));
    RAWFRAME_EXPECT(refused(with("\"widthOffset\": 240", "\"widthOffset\": 5000"), "widthOffset"));
    RAWFRAME_EXPECT(refused(with("\"widthOffset\": 240", "\"widthOffset\": 0"), "widthOffset"));
    RAWFRAME_EXPECT(refused(with("\"editLimit\": 32", "\"editLimit\": 300"), "editLimit"));
    RAWFRAME_EXPECT(refused(with("\"order\": 2", "\"order\": 2.5"), "order"));
    RAWFRAME_EXPECT(refused(with("\"absolute\": true", "\"absolute\": false"), "absolute"));
    // Unknown, out of order, and empty children.
    RAWFRAME_EXPECT(refused(with("\"padding\": 6", "\"paddings\": 6"), "nodes[0]"));
    RAWFRAME_EXPECT(refused(
        with("\"padding\": 6,\n      \"absolute\": true", "\"absolute\": true,\n      \"padding\": 6"), "nodes[0]"));
    RAWFRAME_EXPECT(refused(with("\"layer\": \"overlay\"", "\"layer\": \"overlay\",\n      \"children\": []"),
                            "nodes[1].children"));
    // Limits: nodes, and nodes inside nodes.
    RAWFRAME_EXPECT(refused(std::string{kDocument}, "nodes[1]", DocumentLimits{.maximumNodes = 3}));
    RAWFRAME_EXPECT(refused(std::string{kDocument}, "children[0]", DocumentLimits{.maximumDepth = 1}));
}
