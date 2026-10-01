// Text in a UI tree (D384), in Ahem, whose glyphs are boxes an em wide with
// an ascent of 0.8 em and a descent of 0.2: a node sized by its line, lines
// wrapped to a width, glyph runs drawn on the baseline in the text's color,
// a node's text changed and cleared, bytes that are not a font refused, a
// removed font's text refused, and a removed subtree's text gone with it.

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
