// Coverage-guided fuzzing of a request applied to a game's UI document and
// style sheet (D640, D641), seeded with every operation of both kinds.
// Whatever a request holds, a document it changes still reads, its text
// opens again to the very same bytes, undo gives back the text it had, and
// redo the text the change made.

#include "rawframe/authoring/authored_styles.h"
#include "rawframe/authoring/authored_ui.h"
#include "rawframe/authoring/request.h"

#include <cstdlib>
#include <string>
#include <string_view>

using namespace rawframe;

namespace {

constexpr std::string_view kHud = R"({
  "kind": "ui.document",
  "formatVersion": 1,
  "nodes": [
    {
      "component": "game.hud",
      "widthOffset": 240,
      "style": "85ecf7b643b63d65",
      "children": [
        {
          "component": "game.points",
          "textSize": 14
        }
      ]
    },
    {
      "component": "game.menu"
    }
  ]
}
)";

constexpr std::string_view kSheet = R"({
  "kind": "ui.styles",
  "formatVersion": 1,
  "styles": [
    {
      "styleId": "85ecf7b643b63d65",
      "name": "button",
      "base": {
        "fill": "#101820c0",
        "radius": 6
      },
      "hovered": {
        "fill": "#304860e0"
      },
      "transitions": [
        {
          "parts": [
            "fill"
          ],
          "seconds": 0.12,
          "easing": "ease_out"
        }
      ]
    },
    {
      "styleId": "1d2c3b4a59687706",
      "name": "panel"
    }
  ]
}
)";

/// `document` changed by `request`, undone, and redone, or aborted where
/// any step breaks the properties above.
template <class Authored> void exercise(std::string_view text, const authoring::Request& request) {
    auto opened = Authored::open(base::Bits128{4, 4}, text);
    if (!opened.has_value()) {
        std::abort();
    }
    Authored& document = **opened;
    const auto kDone = document.change(0, request.operations);
    if (!kDone.has_value() || kDone->deltas == 0) {
        // Refused, or a no-op: the document as it was, nothing to undo.
        if (document.text() != text || document.generation() != 0 || document.canUndo()) {
            std::abort();
        }
        return;
    }
    const std::string kChanged = document.text();
    const auto kReopened = Authored::open(base::Bits128{4, 4}, kChanged);
    if (!kReopened.has_value() || (*kReopened)->text() != kChanged) {
        std::abort();
    }
    if (!document.undo(1).has_value() || document.text() != text || document.dirty()) {
        std::abort();
    }
    if (!document.redo(2).has_value() || document.text() != kChanged) {
        std::abort();
    }
}

} // namespace

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    const auto kRequest = authoring::readRequest({reinterpret_cast<const char*>(data), size});
    if (!kRequest.has_value()) {
        return 0;
    }
    exercise<authoring::AuthoredUi>(kHud, *kRequest);
    exercise<authoring::AuthoredStyles>(kSheet, *kRequest);
    return 0;
}
