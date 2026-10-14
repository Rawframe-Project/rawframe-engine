// The game's UI document taken again as an author edits it (D619): a node
// moved out of its parent and back, a node given typed words to show, and
// a document the UI does not take changing nothing.

#include "rig.h"

#include <cstring>
#include <optional>
#include <string_view>
#include <vector>

using namespace rawframe;
using namespace rawframe::world_ui;
using namespace rawframe::world_ui::test_rig;

namespace {

/// Where the meter, 80 by 10, is drawn; none where it is not.
std::optional<ui::Rect> meterIn(const ui::DrawList& drawn) {
    for (const ui::Box& kBox : drawn.boxes) {
        if (kBox.rect.width == 80 && kBox.rect.height == 10) {
            return kBox.rect;
        }
    }
    return std::nullopt;
}

} // namespace

RAWFRAME_TEST(AnEditedDocumentNestsTheUiAnew) {
    Rig rig;
    rig.put(rig.player, kHudId, panel());
    rig.put(rig.player, kMeterId, ui::NodeFields{.widthOffset = 80, .heightOffset = 10, .fill = 0x40C040FF});
    // Inside the panel, past its padding.
    RAWFRAME_EXPECT(rig.frame());
    std::optional<ui::Rect> meter = meterIn(rig.ui->drawn());
    RAWFRAME_EXPECT(meter.has_value() && meter->x == 105 && meter->y == 55);
    // Out of it: a root of the view, below the panel.
    RAWFRAME_EXPECT(rig.ui->rearrange({std::nullopt, std::nullopt, 0}, {}).has_value());
    RAWFRAME_EXPECT(rig.frame());
    meter = meterIn(rig.ui->drawn());
    RAWFRAME_EXPECT(meter.has_value() && meter->x == 100 && meter->y == 90);
    // A parent out of range, or words that are not typed words, change
    // nothing.
    RAWFRAME_EXPECT(!rig.ui->rearrange({std::nullopt, 7, 0}, {}).has_value());
    RAWFRAME_EXPECT(!rig.ui->rearrange({std::nullopt, 0, 0}, {std::nullopt, kRowId, std::nullopt}).has_value());
    RAWFRAME_EXPECT(rig.frame());
    meter = meterIn(rig.ui->drawn());
    RAWFRAME_EXPECT(meter.has_value() && meter->x == 100 && meter->y == 90);
    // Back inside, now showing the player's typed words.
    Typed typed{.length = 3};
    std::memcpy(typed.bytes.data(), "ann", 3);
    rig.put(rig.player, kWordsId, typed);
    RAWFRAME_EXPECT(rig.ui->statistics().typedShown == 0);
    RAWFRAME_EXPECT(rig.ui->rearrange({std::nullopt, 0, 0}, {std::nullopt, kWordsId, std::nullopt}).has_value());
    RAWFRAME_EXPECT(rig.frame());
    meter = meterIn(rig.ui->drawn());
    RAWFRAME_EXPECT(meter.has_value() && meter->x == 105 && meter->y == 55);
    RAWFRAME_EXPECT(rig.ui->statistics().typedShown == 1 && rig.ui->statistics().rearranged == 2);
}
