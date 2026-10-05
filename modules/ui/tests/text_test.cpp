// Text in a UI tree (D384), in Ahem, whose glyphs are boxes an em wide with
// an ascent of 0.8 em and a descent of 0.2: a node sized by its line, lines
// wrapped to a width, glyph runs drawn on the baseline in the text's color,
// a node's text changed and cleared, bytes that are not a font refused, a
// removed font's text refused, a removed subtree's text gone with it, and
// glyphs rendered into the tree's atlas (D398).

#include "rawframe/test/files.h"
#include "rawframe/test/test.h"
#include "rawframe/ui/errors.h"
#include "rawframe/ui/tree.h"

#include <span>
#include <string>

using namespace rawframe;
using namespace rawframe::ui;

namespace {

std::span<const std::byte> bytesOf(const std::string& text) noexcept {
    return std::as_bytes(std::span{text.data(), text.size()});
}

bool sized(const Rect& rect, float width, float height) {
    return rect.width == width && rect.height == height;
}

} // namespace

RAWFRAME_TEST(ANodeIsSizedByItsTextAndWrapsToItsWidth) {
    const std::string kAhem = test::readFile(RAWFRAME_UI_FONTS "Ahem.ttf");
    auto tree = Tree::create(16);
    RAWFRAME_EXPECT(tree.has_value());
    if (!tree.has_value()) {
        return;
    }
    Tree& ui = **tree;
    const auto kFont = ui.addFont(bytesOf(kAhem));
    RAWFRAME_EXPECT(kFont.has_value() && kFont->key != 0);
    const Node kColumn = *ui.add(1);
    const Node kLabel = *ui.add(2);
    const Node kWrapped = *ui.add(3);
    RAWFRAME_EXPECT(ui.attach(kColumn, kLabel).has_value());
    RAWFRAME_EXPECT(ui.attach(kColumn, kWrapped).has_value());
    RAWFRAME_EXPECT(ui.setLayout(kColumn, {.direction = Direction::Column, .alignItems = Align::Start}).has_value());
    // The default font is the first added; text keeps its look.
    RAWFRAME_EXPECT(ui.setText(kLabel, "ABC", {.size = 10, .color = 0xFF0000FF}).has_value());
    RAWFRAME_EXPECT(ui.setLayout(kWrapped, {.width = pixels(25)}).has_value());
    RAWFRAME_EXPECT(ui.setText(kWrapped, "AB CD", {.font = *kFont, .size = 10}).has_value());
    RAWFRAME_EXPECT(ui.layOut(kColumn, 200, 200).has_value());
    RAWFRAME_EXPECT(sized(ui.rectOf(kLabel), 30, 10));
    // "AB " then "CD": the space hangs past the first line.
    RAWFRAME_EXPECT(sized(ui.rectOf(kWrapped), 25, 20));
    RAWFRAME_EXPECT(sized(ui.rectOf(kColumn), 30, 30));

    DrawList list;
    RAWFRAME_EXPECT(ui.draw(kColumn, 1, list).has_value());
    RAWFRAME_EXPECT(list.skipped == 0);
    RAWFRAME_EXPECT(list.glyphRuns.size() >= 2);
    if (list.glyphRuns.empty()) {
        return;
    }
    const GlyphRun& kFirst = list.glyphRuns.front();
    RAWFRAME_EXPECT(kFirst.font == *kFont || kFirst.font == Font{});
    RAWFRAME_EXPECT(kFirst.size == 10 && kFirst.count == 3);
    // On the baseline, 0.8 em down; red in linear light.
    RAWFRAME_EXPECT(kFirst.y + list.glyphs.at(kFirst.first).y == 8);
    RAWFRAME_EXPECT(kFirst.color[0] == 1 && kFirst.color[1] == 0 && kFirst.color[3] == 1);
    RAWFRAME_EXPECT(list.glyphs.at(kFirst.first + 1).x - list.glyphs.at(kFirst.first).x == 10);
    std::uint32_t glyphs = 0;
    for (const GlyphRun& kRun : list.glyphRuns) {
        glyphs += kRun.count;
    }
    // Three, then four of the wrapped text: the space hanging past its
    // first line is not drawn.
    RAWFRAME_EXPECT(glyphs == 7 && list.glyphRuns.size() == 3);
    RAWFRAME_EXPECT(ui.textFailures() == 0);

    // Changed, the node measures again; cleared, it has no size of its own.
    RAWFRAME_EXPECT(ui.setText(kLabel, "ABCDE", {.size = 10}).has_value());
    RAWFRAME_EXPECT(ui.layOut(kColumn, 200, 200).has_value());
    RAWFRAME_EXPECT(sized(ui.rectOf(kLabel), 50, 10));
    RAWFRAME_EXPECT(ui.clearText(kLabel).has_value());
    RAWFRAME_EXPECT(ui.layOut(kColumn, 200, 200).has_value());
    RAWFRAME_EXPECT(sized(ui.rectOf(kLabel), 0, 0));
    // A layout set after the text keeps the node sized by it.
    RAWFRAME_EXPECT(ui.setLayout(kWrapped, {.width = pixels(50)}).has_value());
    RAWFRAME_EXPECT(ui.layOut(kColumn, 200, 200).has_value());
    RAWFRAME_EXPECT(sized(ui.rectOf(kWrapped), 50, 10));
}

RAWFRAME_TEST(FontsAndTextLooksOutOfRangeAreRefused) {
    const std::string kAhem = test::readFile(RAWFRAME_UI_FONTS "Ahem.ttf");
    auto tree = Tree::create(16, 2);
    if (!tree.has_value()) {
        return;
    }
    Tree& ui = **tree;
    const std::string kNotAFont(64, 'x');
    const auto kRefused = ui.addFont(bytesOf(kNotAFont));
    RAWFRAME_EXPECT(!kRefused.has_value() && kRefused.error().code() == code(UiError::Format));
    // A font cut short before its header is refused, not read past.
    const auto kCut = ui.addFont(bytesOf(kAhem.substr(0, 4096)));
    RAWFRAME_EXPECT(!kCut.has_value() && kCut.error().code() == code(UiError::Format));
    const auto kFont = ui.addFont(bytesOf(kAhem));
    RAWFRAME_EXPECT(kFont.has_value());
    RAWFRAME_EXPECT(ui.addFont(bytesOf(kAhem)).has_value());
    const auto kPast = ui.addFont(bytesOf(kAhem));
    RAWFRAME_EXPECT(!kPast.has_value() && kPast.error().code() == code(UiError::Capacity));

    const Node kNode = *ui.add(1);
    RAWFRAME_EXPECT(!ui.setText(kNode, "A", {.size = 0}).has_value());
    RAWFRAME_EXPECT(!ui.setText(kNode, "A", {.weight = 0}).has_value());
    RAWFRAME_EXPECT(!ui.setText(kNode, "A", {.lineHeight = -1}).has_value());
    RAWFRAME_EXPECT(ui.removeFont(*kFont).has_value());
    const auto kGone = ui.setText(kNode, "A", {.font = *kFont});
    RAWFRAME_EXPECT(!kGone.has_value() && kGone.error().code() == code(UiError::Stale));
    RAWFRAME_EXPECT(!ui.removeFont(*kFont).has_value());
}

RAWFRAME_TEST(ARemovedSubtreesTextGoesWithIt) {
    const std::string kAhem = test::readFile(RAWFRAME_UI_FONTS "Ahem.ttf");
    auto tree = Tree::create(4);
    if (!tree.has_value()) {
        return;
    }
    Tree& ui = **tree;
    RAWFRAME_EXPECT(ui.addFont(bytesOf(kAhem)).has_value());
    // As many texts as nodes, three times over: a subtree's blocks are given
    // back when it goes, and a node in a reused slot has no text.
    for (int round = 0; round < 3; ++round) {
        const Node kPanel = *ui.add(1);
        const Node kLabel = *ui.add(2);
        RAWFRAME_EXPECT(ui.attach(kPanel, kLabel).has_value());
        RAWFRAME_EXPECT(ui.setText(kPanel, "AB", {.size = 10}).has_value());
        RAWFRAME_EXPECT(ui.setText(kLabel, "ABCD", {.size = 10}).has_value());
        RAWFRAME_EXPECT(ui.layOut(kPanel, 100, 100).has_value());
        RAWFRAME_EXPECT(sized(ui.rectOf(kLabel), 40, 10));
        RAWFRAME_EXPECT(ui.remove(kPanel).has_value());
    }
    const Node kPlain = *ui.add(3);
    RAWFRAME_EXPECT(ui.layOut(kPlain, 100, 100).has_value());
    RAWFRAME_EXPECT(sized(ui.rectOf(kPlain), 0, 0));
}

RAWFRAME_TEST(GlyphsAreRenderedIntoTheAtlasOnceAndPlacedOnDevicePixels) {
    const std::string kAhem = test::readFile(RAWFRAME_UI_FONTS "Ahem.ttf");
    // An atlas Maul UI cannot cut into plots is refused (D404).
    RAWFRAME_EXPECT(!Tree::create(16, 4, 32).has_value() && !Tree::create(16, 4, 66).has_value());
    // Room for few glyphs: 64 pixels square, sixteen plots of 16, each
    // taking one ten-pixel image inside its gutter.
    auto tree = Tree::create(16, 4, 64);
    RAWFRAME_EXPECT(tree.has_value());
    if (!tree.has_value()) {
        return;
    }
    Tree& ui = **tree;
    RAWFRAME_EXPECT(ui.addFont(bytesOf(kAhem)).has_value());
    const Node kLabel = *ui.add(1);
    RAWFRAME_EXPECT(ui.setText(kLabel, "XX X", {.size = 10}).has_value());
    RAWFRAME_EXPECT(ui.layOut(kLabel, 200, 200).has_value());

    DrawList list;
    RAWFRAME_EXPECT(ui.draw(kLabel, 1, list).has_value());
    RAWFRAME_EXPECT(list.atlas != nullptr && list.glyphsLeftOut == 0 && list.glyphs.size() == 4);
    if (list.atlas == nullptr || list.glyphs.size() != 4) {
        return;
    }
    // An em box, ten pixels on the pen, its top 0.8 em above the baseline,
    // covered whole; the same glyph at the same quarter is rendered once,
    // and the space has no image.
    const Glyph& kFirst = list.glyphs[0];
    RAWFRAME_EXPECT(kFirst.image.x == 0 && kFirst.image.y == 0 && sized(kFirst.image, 10, 10));
    RAWFRAME_EXPECT(sized(kFirst.atlas, 10, 10) && list.glyphs[1].image.x == 10);
    RAWFRAME_EXPECT(list.glyphs[1].atlas.x == kFirst.atlas.x && list.glyphs[1].atlas.y == kFirst.atlas.y);
    RAWFRAME_EXPECT(sized(list.glyphs[2].image, 0, 0) && list.glyphs[3].image.x == 30);
    const GlyphAtlas& kAtlas = *list.atlas;
    RAWFRAME_EXPECT(kAtlas.side == 64 && kAtlas.coverage.size() == 64 * 64 && kAtlas.revision == 1);
    const auto kAt = [&](float x, float y) {
        return kAtlas.coverage.at((static_cast<std::size_t>(y) * kAtlas.side) + static_cast<std::size_t>(x));
    };
    RAWFRAME_EXPECT(kAt(kFirst.atlas.x, kFirst.atlas.y) == 255 && kAt(kFirst.atlas.x + 9, kFirst.atlas.y + 9) == 255);
    RAWFRAME_EXPECT(kAt(kFirst.atlas.x + 10, kFirst.atlas.y) == 0);
    // Drawn again, nothing is rendered.
    RAWFRAME_EXPECT(ui.draw(kLabel, 1, list).has_value());
    RAWFRAME_EXPECT(list.atlas->revision == 1);

    // At a scale of 1.25, a 12.5-pixel em, the second pen half a pixel past
    // an edge: its image starts on that pixel, rendered apart at that half.
    RAWFRAME_EXPECT(ui.draw(kLabel, 1.25F, list).has_value());
    RAWFRAME_EXPECT(list.glyphsLeftOut == 0 && list.atlas->revision == 2);
    RAWFRAME_EXPECT(list.glyphs[0].atlas.height == 13 && list.glyphs[0].image.y == 0);
    RAWFRAME_EXPECT(list.glyphs[1].image.x * 1.25F == 12);
    RAWFRAME_EXPECT(list.glyphs[1].atlas.x != list.glyphs[0].atlas.x ||
                    list.glyphs[1].atlas.y != list.glyphs[0].atlas.y);

    // Sixteen glyphs fill the sixteen plots, the three drawn before emptied
    // for them; a seventeenth in one draw has no plot to take and is left
    // out, and so is a glyph larger than a plot.
    RAWFRAME_EXPECT(ui.setText(kLabel, "ABCDEFGHIJKLMNOP", {.size = 10}).has_value());
    RAWFRAME_EXPECT(ui.layOut(kLabel, 200, 200).has_value());
    RAWFRAME_EXPECT(ui.draw(kLabel, 1, list).has_value());
    RAWFRAME_EXPECT(list.glyphs.size() == 16 && list.glyphsLeftOut == 0 && list.atlas->revision == 3);
    RAWFRAME_EXPECT(ui.setText(kLabel, "ABCDEFGHIJKLMNOPQ", {.size = 10}).has_value());
    RAWFRAME_EXPECT(ui.layOut(kLabel, 200, 200).has_value());
    RAWFRAME_EXPECT(ui.draw(kLabel, 1, list).has_value());
    RAWFRAME_EXPECT(list.glyphs.size() == 17 && list.glyphsLeftOut == 1);
    RAWFRAME_EXPECT(ui.setText(kLabel, "XX X", {.size = 10}).has_value());
    RAWFRAME_EXPECT(ui.layOut(kLabel, 200, 200).has_value());
    RAWFRAME_EXPECT(ui.draw(kLabel, 4, list).has_value());
    RAWFRAME_EXPECT(list.glyphsLeftOut == 3 && sized(list.glyphs[0].image, 0, 0));
}
