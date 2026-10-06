// Scrolling (D441): a column that scrolls holds rows reaching past it (rows
// that do not shrink, as a flex item does by default to fit). A wheel turned
// over it takes the turn and moves the rows, eased over the next layouts and
// drawn where they moved to, cut at its edges; points hit the row now under
// them; one turned past its end goes no further; a turn over nothing that
// scrolls is not taken; and an offset set at once is kept within what the
// rows reach.

#include "rawframe/test/test.h"
#include "rawframe/ui/tree.h"

#include <cmath>
#include <limits>
#include <vector>

using namespace rawframe;
using namespace rawframe::ui;

RAWFRAME_TEST(AWheelScrollsTheColumnUnderItAndItsRowsAreDrawnAndHitWhereTheyMoved) {
    auto tree = Tree::create(64);
    RAWFRAME_EXPECT(tree.has_value());
    if (!tree.has_value()) {
        return;
    }
    Tree& ui = **tree;
    const Node kRoot = *ui.add(1);
    const Node kList = *ui.add(2);
    RAWFRAME_EXPECT(ui.setLayout(kList,
                                 Layout{.width = pixels(200),
                                        .height = pixels(100),
                                        .direction = Direction::Column,
                                        .scroll = Scroll::Vertical})
                        .has_value());
    RAWFRAME_EXPECT(ui.attach(kRoot, kList).has_value());
    const Node kAside = *ui.add(3);
    RAWFRAME_EXPECT(ui.setLayout(kAside, Layout{.width = pixels(200), .height = pixels(100)}).has_value());
    RAWFRAME_EXPECT(ui.setLook(kAside, Look{.fill = 0x00FF00FF}).has_value());
    RAWFRAME_EXPECT(ui.attach(kRoot, kAside).has_value());
    RAWFRAME_EXPECT(
        ui.setLayout(kRoot, Layout{.width = pixels(200), .height = pixels(300), .direction = Direction::Column})
            .has_value());
    // Ten rows of 40 in a column of 100: 300 to scroll.
    std::vector<Node> rows;
    for (std::uint64_t each = 0; each < 10; ++each) {
        const Node kRow = *ui.add(10 + each);
        RAWFRAME_EXPECT(ui.setLayout(kRow, Layout{.height = pixels(40), .shrink = 0}).has_value());
        RAWFRAME_EXPECT(
            ui.setLook(kRow, Look{.fill = 0x102030FFU + static_cast<std::uint32_t>(each << 8U)}).has_value());
        RAWFRAME_EXPECT(ui.attach(kList, kRow).has_value());
        rows.push_back(kRow);
    }
    RAWFRAME_EXPECT(ui.layOut(kRoot, 200, 300, 1.0).has_value());
    RAWFRAME_EXPECT(ui.hit(kRoot, 50, 50)->node == rows[1]);

    // A turn toward the user scrolls the rows up by a step, eased.
    RAWFRAME_EXPECT(ui.wheel(kRoot, 50, 50, 0, -1, 1.0).value_or(false));
    for (double seconds = 1.05; seconds < 2; seconds += 0.05) {
        RAWFRAME_EXPECT(ui.layOut(kRoot, 200, 300, seconds).has_value());
    }
    const float kStep = ui.scrollOf(kList)[1];
    RAWFRAME_EXPECT(kStep > 0 && kStep <= 300);
    RAWFRAME_EXPECT(ui.scrollOf(kList)[0] == 0);
    // The row now under the point is the one the step brought there.
    const auto kUnder = ui.hit(kRoot, 50, 50);
    RAWFRAME_EXPECT(kUnder.has_value() && kUnder->node == rows[static_cast<std::size_t>((50 + kStep) / 40)]);
    DrawList list;
    RAWFRAME_EXPECT(ui.draw(kRoot, 1, list).has_value());
    RAWFRAME_EXPECT(list.skipped == 0);
    // The first row drawn where the step moved it, cut by the column's clip.
    bool moved = false;
    for (const Box& kBox : list.boxes) {
        if (std::abs(kBox.rect.y + kStep) < 0.01F && kBox.rect.height == 40) {
            moved = kBox.clip != 0;
        }
    }
    RAWFRAME_EXPECT(moved);

    // Past the end: no further than the rows reach.
    RAWFRAME_EXPECT(ui.scrollTo(kList, 0, 1000).has_value());
    RAWFRAME_EXPECT(ui.scrollOf(kList)[1] == 300);
    RAWFRAME_EXPECT(ui.layOut(kRoot, 200, 300, 3.0).has_value());
    RAWFRAME_EXPECT(ui.hit(kRoot, 50, 90)->node == rows[9]);
    RAWFRAME_EXPECT(ui.wheel(kRoot, 50, 50, 0, -1, 3.0).has_value());
    RAWFRAME_EXPECT(ui.layOut(kRoot, 200, 300, 4.0).has_value());
    RAWFRAME_EXPECT(ui.scrollOf(kList)[1] == 300);

    // Over what does not scroll, a turn is not taken.
    RAWFRAME_EXPECT(ui.wheel(kRoot, 50, 150, 0, -1, 5.0).has_value());
    RAWFRAME_EXPECT(!ui.wheel(kRoot, 50, 150, 0, -1, 6.0).value_or(true));
    RAWFRAME_EXPECT(!ui.wheel(kRoot, std::numeric_limits<float>::quiet_NaN(), 50, 0, -1, 7.0).has_value());
}
