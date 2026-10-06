// A `ui.styles` document (D431): read with its looks, the parts each sets,
// and its transitions; made into a tree's classes that draw as it says;
// and refused, naming the field, where it is not canonical or out of range.

#include "rawframe/test/test.h"
#include "rawframe/ui/styles.h"

#include <array>
#include <cmath>
#include <string>
#include <string_view>

using namespace rawframe;
using namespace rawframe::ui;

namespace {

constexpr std::string_view kSheet = R"({
  "kind": "ui.styles",
  "formatVersion": 1,
  "styles": [
    {
      "styleId": "6c1f0e3b9a2d4c57",
      "name": "button",
      "base": {
        "fill": "#ff0000ff",
        "radius": 6
      },
      "focused": {
        "outerShadow": {
          "color": "#ffd24aff",
          "spread": 2
        }
      },
      "hovered": {
        "fill": "#00ff00ff"
      },
      "transitions": [
        {
          "parts": [
            "fill"
          ],
          "seconds": 0.5,
          "easing": "linear"
        },
        {
          "parts": [
            "outerShadow"
          ],
          "kind": "spring",
          "frequency": 4
        }
      ]
    },
    {
      "styleId": "0a7d2e9c41b85f36",
      "name": "quiet",
      "disabled": {
        "fill": "#80808080"
      }
    }
  ]
}
)";

/// `sheet` with its first `from` replaced by `to`.
std::string with(std::string_view from, std::string_view to) {
    std::string text{kSheet};
    if (const std::size_t kAt = text.find(from); kAt != std::string::npos) {
        text.replace(kAt, from.size(), to);
    }
    return text;
}

} // namespace

RAWFRAME_TEST(AStyleSheetIsReadAsWritten) {
    const auto kRead = readStyles(kSheet);
    RAWFRAME_EXPECT(kRead.has_value());
    if (!kRead.has_value()) {
        return;
    }
    const StyleSheet& kSheetRead = *kRead;
    RAWFRAME_EXPECT(kSheetRead.styles.size() == 2 && kSheetRead.styleWithId(0x0a7d2e9c41b85f36) == 1 &&
                    !kSheetRead.styleWithId(1).has_value());
    const StyleClass& kButton = kSheetRead.styles[0];
    RAWFRAME_EXPECT(kButton.name == "button" && kButton.looks[0].look.fill == 0xFF0000FF &&
                    kButton.looks[0].look.radius == 6 && kButton.looks[0].parts == (LookPart::Fill | LookPart::Radius));
    RAWFRAME_EXPECT(kButton.looks[1].parts == static_cast<LookParts>(LookPart::OuterShadow) &&
                    kButton.looks[1].look.outerShadow.spread == 2 &&
                    kButton.looks[1].look.outerShadow.color == 0xFFD24AFF);
    RAWFRAME_EXPECT(kButton.looks[3].parts == 0 && kButton.transitions.size() == 2);
    RAWFRAME_EXPECT(kButton.transitions[0].first == static_cast<LookParts>(LookPart::Fill) &&
                    kButton.transitions[0].second.seconds == 0.5F &&
                    kButton.transitions[0].second.easing == Transition::Easing::Linear);
    RAWFRAME_EXPECT(kButton.transitions[1].second.kind == Transition::Kind::Spring &&
                    kButton.transitions[1].second.frequency == 4 && kButton.transitions[1].second.dampingRatio == 1);
    RAWFRAME_EXPECT(kSheetRead.styles[1].looks[4].look.fill == 0x80808080);
}

RAWFRAME_TEST(AStyleSheetMakesItsClasses) {
    auto tree = Tree::create(16);
    const auto kRead = readStyles(kSheet);
    if (!tree.has_value() || !kRead.has_value()) {
        RAWFRAME_EXPECT(false);
        return;
    }
    Tree& ui = **tree;
    const auto kMade = addStyles(ui, *kRead);
    RAWFRAME_EXPECT(kMade.has_value() && kMade->size() == 2);
    const Node kButton = *ui.add(1);
    RAWFRAME_EXPECT(ui.setLayout(kButton, {.width = pixels(40), .height = pixels(20)}).has_value());
    RAWFRAME_EXPECT(ui.setLook(kButton, {}, 0).has_value());
    RAWFRAME_EXPECT(ui.setClasses(kButton, std::span{kMade->data(), 1}).has_value());
    const auto kRedAt = [&](double seconds) {
        DrawList list;
        return ui.layOut(kButton, 100, 100, seconds).has_value() && ui.draw(kButton, 1, list).has_value() &&
                       !list.boxes.empty()
                   ? list.boxes[0].fill[0]
                   : -1.0F;
    };
    RAWFRAME_EXPECT(std::abs(kRedAt(1) - 1) < 0.02F);
    // Hovered, its fill moves to green over half a second.
    RAWFRAME_EXPECT(ui.setStates(kButton, {.hovered = true}).has_value());
    RAWFRAME_EXPECT(std::abs(kRedAt(1) - 1) < 0.02F);
    const float kHalfway = kRedAt(1.25);
    RAWFRAME_EXPECT(kHalfway > 0.05F && kHalfway < 0.95F);
    RAWFRAME_EXPECT(std::abs(kRedAt(2)) < 0.02F);
}

RAWFRAME_TEST(AStyleSheetIsRefusedWhereItIsWrong) {
    // Refused, its path naming `field`.
    const auto kRefused = [](const std::string& text, std::string_view field) {
        const auto kRead = readStyles(text);
        if (kRead.has_value()) {
            return false;
        }
        for (const auto& kContext : kRead.error().context()) {
            if (kContext.key == "path" && kContext.value.find(field) != std::string_view::npos) {
                return true;
            }
        }
        return false;
    };
    RAWFRAME_EXPECT(readStyles(kSheet).has_value());
    RAWFRAME_EXPECT(kRefused(with("\"formatVersion\": 1", "\"formatVersion\": 2"), "formatVersion"));
    RAWFRAME_EXPECT(kRefused(with("ui.styles", "ui.style"), "kind"));
    RAWFRAME_EXPECT(kRefused(with("#ff0000ff", "#FF0000FF"), "fill"));
    RAWFRAME_EXPECT(kRefused(with("#ff0000ff", "#ff0000"), "fill"));
    RAWFRAME_EXPECT(kRefused(with("\"radius\": 6", "\"radius\": -6"), "radius"));
    RAWFRAME_EXPECT(kRefused(with("0a7d2e9c41b85f36", "6c1f0e3b9a2d4c57"), "styleId"));
    RAWFRAME_EXPECT(kRefused(with("\"quiet\"", "\"button\""), "name"));
    RAWFRAME_EXPECT(kRefused(with("\"quiet\"", "\"Quiet\""), "name"));
    RAWFRAME_EXPECT(kRefused(with("\"easing\": \"linear\"", "\"easing\": \"ease\""), "easing"));
    RAWFRAME_EXPECT(kRefused(with("\"easing\": \"linear\"", "\"easing\": \"bounce\""), "easing"));
    RAWFRAME_EXPECT(kRefused(with("\"seconds\": 0.5", "\"seconds\": 600"), "seconds"));
    RAWFRAME_EXPECT(kRefused(with("\"frequency\": 4", "\"seconds\": 4"), "seconds"));
    RAWFRAME_EXPECT(kRefused(with("\"outerShadow\"\n          ],", "\"fill\"\n          ],"), "parts"));
    RAWFRAME_EXPECT(kRefused(with("\"fill\": \"#00ff00ff\"", "\"tint\": \"#00ff00ff\""), "hovered"));
    // Out of order: a look's radius before its fill.
    RAWFRAME_EXPECT(kRefused(
        with("\"fill\": \"#ff0000ff\",\n        \"radius\": 6", "\"radius\": 6,\n        \"fill\": \"#ff0000ff\""),
        "base"));
}
