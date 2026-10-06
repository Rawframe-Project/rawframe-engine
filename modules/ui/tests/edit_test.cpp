// Editing a node's text (D426), in Ahem, whose glyphs are boxes an em wide:
// typed text and editing keys on the caret and the selection, by cluster,
// word, and line; a single line keeping no line breaks; the text's room
// kept whole code points at a time; points finding positions in the
// content box; an input method's composition shown inline and ended; and
// the caret and the selection drawn where they are.

#include "rawframe/test/files.h"
#include "rawframe/test/test.h"
#include "rawframe/ui/text_edit.h"
#include "rawframe/ui/tree.h"

#include <array>
#include <memory>
#include <span>
#include <string>

using namespace rawframe;
using namespace rawframe::ui;

namespace {

/// A tree whose root holds one field, `width` pixels wide with `padding`
/// round it, showing `text` in Ahem at 10 pixels.
struct Field {
    std::unique_ptr<Tree> tree;
    Node root;
    Node node;

    explicit Field(std::string_view text, float width = 200, float padding = 0) {
        static const std::string kAhem = test::readFile(RAWFRAME_UI_FONTS "Ahem.ttf");
        tree = std::move(*Tree::create(16));
        RAWFRAME_EXPECT(tree->addFont(std::as_bytes(std::span{kAhem.data(), kAhem.size()})).has_value());
        root = *tree->add(1);
        node = *tree->addEditable(2);
        RAWFRAME_EXPECT(tree->attach(root, node).has_value());
        RAWFRAME_EXPECT(
            tree->setLayout(root, {.direction = Direction::Column, .alignItems = Align::Start, .padding = {7, 7, 3, 3}})
                .has_value());
        RAWFRAME_EXPECT(tree->setLayout(node, {.width = pixels(width), .padding = {padding, padding, padding, padding}})
                            .has_value());
        RAWFRAME_EXPECT(tree->setText(node, text, {.size = 10}).has_value());
        layOut();
    }

    void layOut() {
        RAWFRAME_EXPECT(tree->layOut(root, 400, 400).has_value());
    }

    std::string text() const {
        return std::string{tree->textOf(node)};
    }
};

bool selected(const TextEdit& edit, std::uint32_t start, std::uint32_t end) {
    return edit.selection() == TextRange{.start = start, .end = end};
}

} // namespace

RAWFRAME_TEST(TypedTextAndKeysEditAtTheCaret) {
    Field field{""};
    TextEdit edit{*field.tree, field.node, {}};
    RAWFRAME_EXPECT(edit.type("hello").has_value() && field.text() == "hello" && edit.caret().offset == 5);
    field.layOut();
    RAWFRAME_EXPECT(edit.press(EditKey::Left, {}).has_value() && edit.press(EditKey::Left, {}).has_value());
    RAWFRAME_EXPECT(edit.caret().offset == 3);
    RAWFRAME_EXPECT(edit.press(EditKey::Backspace, {}).has_value() && field.text() == "helo" &&
                    edit.caret().offset == 2);
    field.layOut();
    RAWFRAME_EXPECT(edit.press(EditKey::Delete, {}).has_value() && field.text() == "heo");
    field.layOut();
    RAWFRAME_EXPECT(edit.press(EditKey::Home, {}).has_value() && edit.caret().offset == 0);
    RAWFRAME_EXPECT(edit.press(EditKey::End, {}).has_value() && edit.caret().offset == 3);
    // Shift extends the selection, which typing replaces.
    RAWFRAME_EXPECT(edit.press(EditKey::Left, {.extend = true}).has_value() &&
                    edit.press(EditKey::Left, {.extend = true}).has_value() && selected(edit, 1, 3));
    RAWFRAME_EXPECT(edit.type("X").has_value() && field.text() == "hX" && selected(edit, 2, 2));
    field.layOut();
    // An arrow collapses a selection to the end it points at.
    RAWFRAME_EXPECT(edit.press(EditKey::SelectAll, {}).has_value() && selected(edit, 0, 2));
    RAWFRAME_EXPECT(edit.press(EditKey::Left, {}).has_value() && selected(edit, 0, 0));
    RAWFRAME_EXPECT(edit.press(EditKey::SelectAll, {}).has_value() && edit.press(EditKey::Backspace, {}).has_value() &&
                    field.text().empty());
}

RAWFRAME_TEST(AWordGoesWholeAndLinesKeepTheirX) {
    Field field{"one two three"};
    TextEdit edit{*field.tree, field.node, {}};
    RAWFRAME_EXPECT(edit.press(EditKey::Backspace, {.word = true}).has_value() && field.text() == "one two ");
    field.layOut();
    RAWFRAME_EXPECT(edit.press(EditKey::Left, {.word = true}).has_value() && edit.caret().offset == 4);
    RAWFRAME_EXPECT(edit.press(EditKey::Delete, {.word = true}).has_value() && field.text() == "one  ");
    field.layOut();
    RAWFRAME_EXPECT(edit.press(EditKey::End, {.word = true}).has_value() && edit.caret().offset == 5);

    // "AB " and "CD" on two lines 30 pixels wide: down from after A is
    // after C, and up again after A.
    Field lines{"AB CD", 30};
    TextEdit wrapped{*lines.tree, lines.node, {.multiline = true}};
    RAWFRAME_EXPECT(wrapped.press(EditKey::Home, {.word = true}).has_value() &&
                    wrapped.press(EditKey::Right, {}).has_value() && wrapped.caret().offset == 1);
    RAWFRAME_EXPECT(wrapped.press(EditKey::Down, {}).has_value() && wrapped.caret().offset == 4);
    RAWFRAME_EXPECT(wrapped.press(EditKey::Up, {}).has_value() && wrapped.caret().offset == 1);
}

RAWFRAME_TEST(WhatIsTypedIsKeptToTheFieldAndItsRoom) {
    Field field{""};
    TextEdit line{*field.tree, field.node, {.maximumBytes = 5}};
    // A single line keeps no line breaks, tabs, or other controls.
    RAWFRAME_EXPECT(line.type("a\nb\tc\x01").has_value() && field.text() == "abc");
    // The room is kept whole code points at a time: e-acute is two bytes.
    RAWFRAME_EXPECT(line.type("d\xC3\xA9").has_value() && field.text() == "abcd" && line.caret().offset == 4);
    Field lines{""};
    TextEdit multiline{*lines.tree, lines.node, {.multiline = true}};
    RAWFRAME_EXPECT(multiline.type("a\nb\tc\x7F").has_value() && lines.text() == "a\nb\tc");
}

RAWFRAME_TEST(APointFindsAPlaceInTheContentBox) {
    Field field{"ABCDE", 200, 5};
    TextEdit edit{*field.tree, field.node, {}};
    // 23 pixels into the text is nearer the edge at 20.
    RAWFRAME_EXPECT(edit.pointAt(28, 10, false).has_value() && selected(edit, 2, 2));
    RAWFRAME_EXPECT(edit.pointAt(46, 10, true).has_value() && selected(edit, 2, 4));
    const auto kCaret = edit.caretRect();
    RAWFRAME_EXPECT(kCaret.has_value() && kCaret->x == 45 && kCaret->y == 5 && kCaret->height == 10);
    // Past the text's end is its end.
    RAWFRAME_EXPECT(edit.pointAt(180, 10, false).has_value() && edit.caret().offset == 5);
    RAWFRAME_EXPECT(!edit.pointAt(std::nanf(""), 0, false).has_value());
}

RAWFRAME_TEST(ACompositionIsShownInlineUntilItEnds) {
    Field field{"ab"};
    TextEdit edit{*field.tree, field.node, {}};
    RAWFRAME_EXPECT(edit.press(EditKey::Left, {}).has_value());
    const std::array<CompositionPart, 2> kParts = {
        CompositionPart{.start = 0, .length = 1, .style = CompositionPart::Style::Target},
        CompositionPart{.start = 1, .length = 1, .style = CompositionPart::Style::Underline}};
    RAWFRAME_EXPECT(edit.compose("xy", 1, kParts).has_value() && field.text() == "axyb");
    RAWFRAME_EXPECT((field.tree->composition(field.node) == TextRange{.start = 1, .end = 3}));
    field.layOut();
    // The caret is drawn inside the composition, one byte in.
    const auto kCaret = edit.caretRect();
    RAWFRAME_EXPECT(kCaret.has_value() && kCaret->x == 20);
    // Cancelled, then committed as typed text.
    RAWFRAME_EXPECT(edit.compose("", -1, {}).has_value() && field.text() == "ab");
    RAWFRAME_EXPECT(edit.type("Z").has_value() && field.text() == "aZb" && edit.caret().offset == 2);
    // Typing while composing ends the composition first.
    RAWFRAME_EXPECT(edit.compose("q", 1, {}).has_value() && field.text() == "aZqb");
    RAWFRAME_EXPECT(edit.type("Q").has_value() && field.text() == "aZQb");
    RAWFRAME_EXPECT((field.tree->composition(field.node) == TextRange{.start = 0, .end = 0}));
    // Parts past the composition are drawn as none, not refused.
    const std::array<CompositionPart, 1> kPast = {CompositionPart{.start = 0, .length = 9}};
    RAWFRAME_EXPECT(edit.compose("w", 1, kPast).has_value() && field.text() == "aZQwb");
}

RAWFRAME_TEST(TheCaretAndTheSelectionAreDrawnWhereTheyAre) {
    Field field{"ABCD"};
    TextEdit edit{*field.tree, field.node, {.caretColor = 0xFF0000FF, .selectionColor = 0x0000FFFF}};
    DrawList list;
    RAWFRAME_EXPECT(field.tree->draw(field.root, 1, list).has_value());
    const std::size_t kBoxes = list.boxes.size();
    RAWFRAME_EXPECT(edit.decorate(field.root, list).has_value() && list.boxes.size() == kBoxes + 1);
    // After the text, in the root's padding: 7 in, 3 down.
    const Box& kCaret = list.boxes.back();
    RAWFRAME_EXPECT(kCaret.rect.x == 47 && kCaret.rect.y == 3 && kCaret.rect.width == 1 && kCaret.rect.height == 10);
    RAWFRAME_EXPECT(kCaret.fill[0] == 1 && kCaret.fill[1] == 0 && kCaret.fill[3] == 1);
    RAWFRAME_EXPECT(list.commands.back().kind == DrawCommand::Kind::Box &&
                    list.commands.back().index == list.boxes.size() - 1);
    // A selection's box goes under the caret.
    RAWFRAME_EXPECT(edit.press(EditKey::Left, {.extend = true}).has_value() &&
                    edit.press(EditKey::Left, {.extend = true}).has_value());
    RAWFRAME_EXPECT(field.tree->draw(field.root, 1, list).has_value() && edit.decorate(field.root, list).has_value());
    const Box& kSelection = list.boxes[list.boxes.size() - 2];
    RAWFRAME_EXPECT(kSelection.rect.x == 27 && kSelection.rect.width == 20 && kSelection.fill[2] == 1);
    RAWFRAME_EXPECT(list.boxes.back().rect.x == 27);
}

RAWFRAME_TEST(OnlyANodeAddedEditableIsEditedAndItKeepsItsKey) {
    Field field{"AB"};
    RAWFRAME_EXPECT(field.tree->keyOf(field.node) == 2);
    const Node kLabel = *field.tree->add(3);
    RAWFRAME_EXPECT(field.tree->setText(kLabel, "AB", {.size = 10}).has_value());
    RAWFRAME_EXPECT(!field.tree->textAt(kLabel, 0, 0).has_value() && field.tree->textOf(kLabel) == "AB");
    // Cleared, it shows nothing and is still editable.
    RAWFRAME_EXPECT(field.tree->clearText(field.node).has_value() && field.tree->textOf(field.node).empty());
    TextEdit edit{*field.tree, field.node, {}};
    RAWFRAME_EXPECT(edit.type("C").has_value() && field.text() == "C");
    // Removed, its key is no one's.
    RAWFRAME_EXPECT(field.tree->remove(field.node).has_value() && field.tree->keyOf(field.node) == 0);
    RAWFRAME_EXPECT(!field.tree->textAt(field.node, 0, 0).has_value());
}
