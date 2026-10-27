// A text field: it takes the keyboard by a press, shows what is
// typed with its caret, and gives its text by Enter; a screen reader types
// into it as typing does; a node shows the words of a component of
// typed words in place of its label while it holds any; an empty
// field without the keyboard shows its label as its placeholder; and
// Tab moves the keyboard between a view's fields.

#include "rawframe/test/files.h"
#include "rawframe/test/test.h"
#include "rawframe/ui/access.h"
#include "rawframe/world_ui/world_ui.h"
#include "rig.h"

#include <array>
#include <cstring>
#include <memory>
#include <optional>
#include <string>
#include <utility>

using namespace rawframe;
using namespace rawframe::world_ui;
using namespace rawframe::world_ui::test_rig;

RAWFRAME_TEST(ATextFieldTakesTheKeyboardAndGivesItsText) {
    Rig rig;
    rig.ui = *WorldUi::create({.nodes = {kHudId, kMeterId, kRowId}, .parents = {std::nullopt, 0, 0}, .fonts = {0xF1}});
    const std::string kAhem = test::readFile(RAWFRAME_UI_FONTS "Ahem.ttf");
    RAWFRAME_EXPECT(rig.ui->addFont(0xF1, std::as_bytes(std::span{kAhem.data(), kAhem.size()})).has_value());
    // A field 200 by 20 at the view's top left, (100, 50), in Ahem at 10.
    ui::NodeFields field{.widthOffset = 200,
                         .heightOffset = 20,
                         .font = 0xF1,
                         .textSize = 10,
                         .textColor = 0xFFFFFFFF,
                         .press = 7,
                         .edit = 1};
    rig.put(rig.player, kHudId, field);
    RAWFRAME_EXPECT(rig.frame() && !rig.ui->caret().has_value());
    const auto kType = [&rig](std::string_view text) {
        rig.ui->type(view::Typing{.kind = view::Typing::Kind::Text, .text = std::string{text}});
    };
    const auto kKey = [&rig](view::TypingKey key) {
        rig.ui->type(view::Typing{.kind = view::Typing::Kind::Key, .key = key});
    };
    // A press going down, then what the input asks of it.
    const auto kPress = [&rig](float x, float y) {
        rig.ui->pressAt(x, y);
        return rig.ui->press(x, y);
    };
    const auto kCaretAt = [&rig](float x) {
        return rig.ui->caret().has_value() && (*rig.ui->caret())[0] == x && (*rig.ui->caret())[1] == 50;
    };
    // Typed with no field holding the keyboard: nothing.
    kType("no");
    RAWFRAME_EXPECT(rig.ui->statistics().typed == 0);
    // A press on it takes the press and the keyboard.
    RAWFRAME_EXPECT(kPress(105, 55) == std::optional<std::int64_t>{7});
    // At once, before the next frame: the keys after the press find it.
    RAWFRAME_EXPECT(kCaretAt(100));
    RAWFRAME_EXPECT(rig.frame() && kCaretAt(100) && rig.ui->statistics().focused == 1);
    kType("hi");
    RAWFRAME_EXPECT(rig.frame() && kCaretAt(120) && rig.ui->drawn().glyphs.size() == 2);
    kKey(view::TypingKey::Left);
    RAWFRAME_EXPECT(rig.frame() && kCaretAt(110));
    // Enter gives the text and lets the keyboard go.
    kKey(view::TypingKey::Submit);
    auto given = rig.ui->takeSubmitted();
    RAWFRAME_EXPECT(given.size() == 1 && given[0].press == 7 && given[0].text == "hi");
    RAWFRAME_EXPECT(rig.frame() && !rig.ui->caret().has_value() && rig.ui->statistics().submitted == 1);
    RAWFRAME_EXPECT(rig.ui->takeSubmitted().empty());
    // A press at the field's end puts the caret there; one elsewhere, or
    // Escape, lets go.
    RAWFRAME_EXPECT(kPress(250, 55).has_value() && rig.frame() && kCaretAt(120));
    RAWFRAME_EXPECT(!kPress(600, 300).has_value() && rig.frame() && !rig.ui->caret().has_value());
    RAWFRAME_EXPECT(kPress(105, 55).has_value() && rig.frame() && rig.ui->caret().has_value());
    kKey(view::TypingKey::Dismiss);
    RAWFRAME_EXPECT(rig.frame() && !rig.ui->caret().has_value());

    // A message's field gives its text, empties, and keeps the keyboard.
    // Made a field of another kind, it keeps its text: here "hi".
    field.edit = 2;
    rig.put(rig.player, kHudId, field);
    RAWFRAME_EXPECT(rig.frame() && kPress(105, 55).has_value());
    RAWFRAME_EXPECT(rig.frame() && rig.ui->drawn().glyphs.size() == 2);
    kKey(view::TypingKey::SelectAll);
    kType("yo");
    kKey(view::TypingKey::Submit);
    given = rig.ui->takeSubmitted();
    RAWFRAME_EXPECT(given.size() == 1 && given[0].text == "yo");
    RAWFRAME_EXPECT(rig.frame() && kCaretAt(100) && rig.ui->drawn().glyphs.empty());

    // Lines take Enter as a line break; a limit keeps the text within it.
    field.edit = 3;
    rig.put(rig.player, kHudId, field);
    RAWFRAME_EXPECT(rig.frame() && kPress(105, 55).has_value());
    kType("a");
    kKey(view::TypingKey::Submit);
    kType("b");
    RAWFRAME_EXPECT(rig.frame() && rig.ui->takeSubmitted().empty() && rig.ui->caret().has_value() &&
                    (*rig.ui->caret())[1] == 60);
    field.edit = 1;
    field.editLimit = 3;
    rig.put(rig.player, kHudId, field);
    RAWFRAME_EXPECT(rig.frame() && kPress(105, 55).has_value());
    kKey(view::TypingKey::SelectAll);
    kType("abcdef");
    kKey(view::TypingKey::Submit);
    given = rig.ui->takeSubmitted();
    RAWFRAME_EXPECT(given.size() == 1 && given[0].text == "abc");
    // A field gone lets the keyboard go.
    RAWFRAME_EXPECT(kPress(105, 55).has_value());
    RAWFRAME_EXPECT(rig.world.removeErased(rig.player, *rig.schema->find(kHudId)).has_value());
    RAWFRAME_EXPECT(rig.frame() && !rig.ui->caret().has_value());
    kType("x");
    RAWFRAME_EXPECT(rig.frame() && rig.ui->takeSubmitted().empty());
}

RAWFRAME_TEST(AScreenReaderTypesIntoAFieldAsTypingDoes) {
    Rig rig;
    rig.ui = *WorldUi::create({.nodes = {kHudId, kMeterId, kRowId}, .parents = {std::nullopt, 0, 0}, .fonts = {0xF1}});
    const std::string kAhem = test::readFile(RAWFRAME_UI_FONTS "Ahem.ttf");
    RAWFRAME_EXPECT(rig.ui->addFont(0xF1, std::as_bytes(std::span{kAhem.data(), kAhem.size()})).has_value());
    // The field of the test above, read by a screen reader.
    rig.put(rig.player,
            kHudId,
            ui::NodeFields{.widthOffset = 200,
                           .heightOffset = 20,
                           .font = 0xF1,
                           .textSize = 10,
                           .textColor = 0xFFFFFFFF,
                           .press = 7,
                           .edit = 1});
    ui::AccessSeat seat;
    RAWFRAME_EXPECT(rig.frame() && rig.ui->seatIn(seat).has_value() && seat.open({}).has_value());
    RAWFRAME_EXPECT(rig.ui->enterNavigation() && rig.frame() && seat.update().has_value());
    const std::optional<ui::Node> kField = seat.access() != nullptr ? seat.access()->focused() : std::nullopt;
    RAWFRAME_EXPECT(kField.has_value() && !rig.ui->caret().has_value());
    if (!kField.has_value()) {
        return;
    }
    const auto kAsk = [&](ui::AccessRequest::Kind kind, std::uint32_t anchor, std::uint32_t focus, std::string text) {
        return rig.ui->ask({.kind = kind, .node = *kField, .anchor = anchor, .focus = focus, .text = std::move(text)});
    };
    const auto kCaretAt = [&rig](float x) {
        return rig.ui->caret().has_value() && (*rig.ui->caret())[0] == x && (*rig.ui->caret())[1] == 50;
    };
    // The page's field typed into: the field takes the keyboard, and the
    // text is put where the page says, the caret after it.
    RAWFRAME_EXPECT(!kAsk(ui::AccessRequest::Kind::Replace, 0, 0, "h\xC3\xA9").has_value());
    RAWFRAME_EXPECT(rig.frame() && kCaretAt(120) && rig.ui->drawn().glyphs.size() == 2);
    RAWFRAME_EXPECT(rig.ui->statistics().focused == 1 && rig.ui->statistics().typed == 1);
    // A selection made backward, then the range replaced, either way round.
    RAWFRAME_EXPECT(!kAsk(ui::AccessRequest::Kind::Select, 3, 1, {}).has_value() && rig.frame() && kCaretAt(110));
    RAWFRAME_EXPECT(!kAsk(ui::AccessRequest::Kind::Replace, 3, 1, "o").has_value() && rig.frame() && kCaretAt(120));
    RAWFRAME_EXPECT(!kAsk(ui::AccessRequest::Kind::Replace, 0, 0, "\xC3\xA9").has_value() && rig.frame() &&
                    kCaretAt(110));
    // A range past the text, or inside a code point, changes nothing.
    RAWFRAME_EXPECT(!kAsk(ui::AccessRequest::Kind::Select, 4, 4, {}).has_value() && rig.frame() && kCaretAt(130));
    RAWFRAME_EXPECT(!kAsk(ui::AccessRequest::Kind::Select, 0, 9, {}).has_value() && rig.frame() && kCaretAt(130));
    RAWFRAME_EXPECT(!kAsk(ui::AccessRequest::Kind::Replace, 1, 1, "x").has_value() && rig.frame() && kCaretAt(130) &&
                    rig.ui->statistics().typed == 3);
    // Enter gives the text the screen reader typed.
    rig.ui->type(view::Typing{.kind = view::Typing::Kind::Key, .key = view::TypingKey::Submit});
    const auto kGiven = rig.ui->takeSubmitted();
    RAWFRAME_EXPECT(kGiven.size() == 1 && kGiven[0].press == 7 && kGiven[0].text == "\xC3\xA9ho");
}

RAWFRAME_TEST(ANodeShowsTypedWordsInPlaceOfItsLabel) {
    Rig rig;
    rig.ui = *WorldUi::create(
        {.nodes = {kHudId, kMeterId, kRowId},
         .parents = {std::nullopt, 0, 0},
         .shows = {kWordsId, std::nullopt, std::nullopt},
         .fonts = {0xF1},
         .words = [](std::size_t, std::uint64_t, std::array<std::int64_t, 2>) -> std::optional<std::string> {
             return std::string{"LABEL"};
         }});
    const std::string kAhem = test::readFile(RAWFRAME_UI_FONTS "Ahem.ttf");
    RAWFRAME_EXPECT(rig.ui->addFont(0xF1, std::as_bytes(std::span{kAhem.data(), kAhem.size()})).has_value());
    const auto kTyped = [](std::string_view text) {
        Typed typed{.length = static_cast<std::uint32_t>(text.size())};
        std::memcpy(typed.bytes.data(), text.data(), text.size());
        return typed;
    };
    rig.put(rig.player, kHudId, ui::NodeFields{.text = 0xA1, .textSize = 10, .textColor = 0xFFFFFFFF});
    // Without words, the label.
    RAWFRAME_EXPECT(rig.frame() && rig.ui->drawn().glyphs.size() == 5 && rig.ui->statistics().typedShown == 0);
    // With them, the words; changed, the new ones, though the node is not.
    rig.put(rig.player, kWordsId, kTyped("ann"));
    RAWFRAME_EXPECT(rig.frame() && rig.ui->drawn().glyphs.size() == 3 && rig.ui->statistics().typedShown == 1);
    rig.put(rig.player, kWordsId, kTyped("anna"));
    RAWFRAME_EXPECT(rig.frame() && rig.ui->drawn().glyphs.size() == 4);
    // Unchanged, nothing is given again.
    RAWFRAME_EXPECT(rig.frame() && rig.ui->statistics().typedShown == 2);
    // Empty, or saying more than it holds, the label again.
    rig.put(rig.player, kWordsId, Typed{});
    RAWFRAME_EXPECT(rig.frame() && rig.ui->drawn().glyphs.size() == 5);
    rig.put(rig.player, kWordsId, Typed{.length = 900});
    RAWFRAME_EXPECT(rig.frame() && rig.ui->drawn().glyphs.size() == 5);
    // A node with no words line shows its label beside it.
    rig.put(rig.player, kWordsId, kTyped("x"));
    rig.put(rig.player, kMeterId, ui::NodeFields{.text = 0xA1, .textSize = 10, .textColor = 0xFFFFFFFF});
    RAWFRAME_EXPECT(rig.frame() && rig.ui->drawn().glyphs.size() == 6);
}

RAWFRAME_TEST(AnEmptyFieldShowsItsLabelUntilItHasTheKeyboard) {
    Rig rig;
    rig.ui = *WorldUi::create(
        {.nodes = {kHudId, kMeterId, kRowId},
         .parents = {std::nullopt, 0, 0},
         .fonts = {0xF1},
         .words = [](std::size_t, std::uint64_t, std::array<std::int64_t, 2>) -> std::optional<std::string> {
             return std::string{"NAME"};
         }});
    const std::string kAhem = test::readFile(RAWFRAME_UI_FONTS "Ahem.ttf");
    RAWFRAME_EXPECT(rig.ui->addFont(0xF1, std::as_bytes(std::span{kAhem.data(), kAhem.size()})).has_value());
    rig.put(rig.player,
            kHudId,
            ui::NodeFields{.widthOffset = 200,
                           .heightOffset = 20,
                           .text = 0xA1,
                           .font = 0xF1,
                           .textSize = 10,
                           .textColor = 0xFFFFFFFF,
                           .press = 3,
                           .edit = 1});
    const auto kGlyphs = [&rig] {
        return rig.frame() ? rig.ui->drawn().glyphs.size() : std::size_t{99};
    };
    const auto kKey = [&rig](view::TypingKey key) {
        rig.ui->type(view::Typing{.kind = view::Typing::Kind::Key, .key = key});
    };
    // Empty and without the keyboard: its label, at half its alpha.
    RAWFRAME_EXPECT(kGlyphs() == 4 && !rig.ui->drawn().glyphRuns.empty() &&
                    rig.ui->drawn().glyphRuns[0].color[3] < 0.6F);
    // With the keyboard, nothing, the caret at its start.
    rig.ui->pressAt(150, 55);
    RAWFRAME_EXPECT(kGlyphs() == 0 && rig.ui->caret().has_value() && (*rig.ui->caret())[0] == 100);
    // The placeholder is never given as text.
    kKey(view::TypingKey::Submit);
    auto given = rig.ui->takeSubmitted();
    RAWFRAME_EXPECT(given.size() == 1 && given[0].text.empty() && kGlyphs() == 4);
    // What is typed is kept when the keyboard goes; emptied, the label again.
    rig.ui->pressAt(150, 55);
    rig.ui->type(view::Typing{.kind = view::Typing::Kind::Text, .text = "ab"});
    kKey(view::TypingKey::Dismiss);
    RAWFRAME_EXPECT(kGlyphs() == 2);
    rig.ui->pressAt(150, 55);
    kKey(view::TypingKey::SelectAll);
    kKey(view::TypingKey::Backspace);
    RAWFRAME_EXPECT(kGlyphs() == 0);
    rig.ui->pressAt(600, 300);
    RAWFRAME_EXPECT(kGlyphs() == 4);
}

RAWFRAME_TEST(TabMovesTheKeyboardBetweenAViewsFields) {
    Rig rig;
    rig.ui = *WorldUi::create({.nodes = {kHudId, kMeterId, kRowId}, .parents = {std::nullopt, 0, 0}, .fonts = {0xF1}});
    const std::string kAhem = test::readFile(RAWFRAME_UI_FONTS "Ahem.ttf");
    RAWFRAME_EXPECT(rig.ui->addFont(0xF1, std::as_bytes(std::span{kAhem.data(), kAhem.size()})).has_value());
    // In the panel, side by side: the row's field first (105, 55), then
    // the meter's (215, 55), whatever order they were made in.
    const auto kField = [](std::int64_t press, std::int32_t order) {
        return ui::NodeFields{.widthOffset = 100,
                              .heightOffset = 20,
                              .order = order,
                              .font = 0xF1,
                              .textSize = 10,
                              .textColor = 0xFFFFFFFF,
                              .press = press,
                              .edit = 1};
    };
    rig.put(rig.player, kHudId, panel());
    rig.put(rig.player, kMeterId, kField(1, 1));
    rig.put(rig.player, kRowId, kField(2, 0));
    RAWFRAME_EXPECT(rig.frame());
    const auto kType = [&rig](std::string_view text) {
        rig.ui->type(view::Typing{.kind = view::Typing::Kind::Text, .text = std::string{text}});
    };
    const auto kTab = [&rig](bool back) {
        rig.ui->type(view::Typing{.kind = view::Typing::Kind::Key, .key = view::TypingKey::Next, .extend = back});
    };
    const auto kCaretAt = [&rig](float x) {
        return rig.ui->caret().has_value() && (*rig.ui->caret())[0] == x;
    };
    // Tab with no field holding the keyboard: nothing.
    kTab(false);
    RAWFRAME_EXPECT(rig.ui->statistics().focused == 0 && !rig.ui->caret().has_value());
    rig.ui->pressAt(225, 60);
    kType("m");
    // From the last, round to the first.
    kTab(false);
    RAWFRAME_EXPECT(rig.ui->statistics().focused == 2 && kCaretAt(105));
    kType("r");
    // Reached by Tab, a field's text is all selected: typing replaces it.
    kTab(false);
    RAWFRAME_EXPECT(rig.ui->statistics().focused == 3);
    kType("M");
    // Shift+Tab, back to the row's, where Enter gives its text.
    kTab(true);
    RAWFRAME_EXPECT(rig.ui->statistics().focused == 4);
    rig.ui->type(view::Typing{.kind = view::Typing::Kind::Key, .key = view::TypingKey::End});
    rig.ui->type(view::Typing{.kind = view::Typing::Kind::Key, .key = view::TypingKey::Submit});
    rig.ui->pressAt(225, 60);
    rig.ui->type(view::Typing{.kind = view::Typing::Kind::Key, .key = view::TypingKey::Submit});
    const auto kGiven = rig.ui->takeSubmitted();
    RAWFRAME_EXPECT(kGiven.size() == 2 && kGiven[0].press == 2 && kGiven[0].text == "r" && kGiven[1].press == 1 &&
                    kGiven[1].text == "M");
    // A field alone, now first in the panel, keeps the keyboard.
    RAWFRAME_EXPECT(rig.world.removeErased(rig.player, *rig.schema->find(kRowId)).has_value());
    RAWFRAME_EXPECT(rig.frame());
    rig.ui->pressAt(150, 60);
    const std::uint64_t kFocused = rig.ui->statistics().focused;
    kTab(false);
    RAWFRAME_EXPECT(rig.ui->statistics().focused == kFocused && rig.ui->caret().has_value());
}
