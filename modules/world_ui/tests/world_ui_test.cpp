// The UI as World data (D376): node components laid out in their player's
// view, nested by their components' parents on the same entity or on the
// player, siblings in order; a node changes, goes, and is left out as its
// component does; nothing unchanged is laid out again; a new World starts
// afresh; values the tree cannot take, a gradient of no kind among them,
// are left out and counted; and a node shows its label's words in the
// game's font once it is read, sized by them (D386); a press lands on the
// node that takes it, with its code, or passes through (D421); and a text
// field takes the keyboard by a press, shows what is typed with its caret,
// and gives its text by Enter (D426); and a node shows the words of a
// component of typed words in place of its label while it holds any
// (D427); and an empty field without the keyboard shows its label as its
// placeholder (D428).

#include "rawframe/test/files.h"
#include "rawframe/test/test.h"
#include "rawframe/world_ui/errors.h"
#include "rawframe/world_ui/world_ui.h"

#include <array>
#include <cstring>
#include <memory>
#include <optional>
#include <string>
#include <utility>

using namespace rawframe;
using namespace rawframe::world_ui;

namespace {

constexpr auto kHudId = schema::ComponentTypeId::fromText("1d3c5b7a-0e2f-4a61-8b93-c5d7e9f1a203");
constexpr auto kMeterId = schema::ComponentTypeId::fromText("2e4d6c8b-1f30-4b72-9ca4-d6e8f0a2b314");
constexpr auto kRowId = schema::ComponentTypeId::fromText("3f5e7d9c-2041-4c83-adb5-e7f9a1b3c425");
constexpr auto kWordsId = schema::ComponentTypeId::fromText("4a6f8e0d-3152-4d94-bec6-f80ab2c4d536");

std::shared_ptr<const schema::SchemaRegistry> registry() {
    schema::RegistryBuilder builder;
    for (const auto& [kId, kName] :
         {std::pair{kHudId, "test.hud"}, std::pair{kMeterId, "test.meter"}, std::pair{kRowId, "test.row"}}) {
        builder.add(schema::ComponentDescriptor{
            .id = kId, .name = kName, .size = sizeof(Node), .alignment = alignof(Node), .plainData = true});
    }
    builder.add(schema::ComponentDescriptor{
        .id = kWordsId, .name = "test.words", .size = sizeof(Typed), .alignment = alignof(Typed), .plainData = true});
    return *builder.freeze();
}

/// A HUD on the player: a panel 300 by 40 along the view's top, a meter
/// inside it, and a row for each other entity inside the panel too.
struct Rig {
    std::shared_ptr<const schema::SchemaRegistry> schema = registry();
    world::World world{schema};
    std::unique_ptr<WorldUi> ui =
        *WorldUi::create({.nodes = {kHudId, kMeterId, kRowId}, .parents = {std::nullopt, 0, 0}});
    world::EntityHandle player = *world.create();

    template <typename Value> void put(world::EntityHandle entity, schema::ComponentTypeId id, Value value) {
        const schema::ComponentRuntimeId kComponent = *schema->find(id);
        if (void* held = world.getErased(entity, kComponent)) {
            std::memcpy(held, &value, sizeof(Value));
            return;
        }
        RAWFRAME_EXPECT(world.insertErased(entity, kComponent, &value).has_value());
    }

    /// One frame of the player's view at (100, 50), 400 by 300, in a
    /// window 640 by 360 at `scale`.
    bool frame(float scale = 1) {
        const std::array<UiView, 1> kViews = {
            UiView{.world = &world, .player = player, .x = 100, .y = 50, .width = 400, .height = 300}};
        return ui->update(kViews, 640, 360, scale).has_value();
    }
};

Node panel() {
    return Node{
        .widthOffset = 300, .heightOffset = 40, .gap = 10, .shrink = 1, .padding = 5, .fill = 0x202020FF, .radius = 6};
}

Node meter(float width) {
    return Node{.widthOffset = width, .shrink = 1, .fill = 0x40C040FF};
}

bool at(const ui::Box& box, float x, float y, float width, float height) {
    return box.rect.x == x && box.rect.y == y && box.rect.width == width && box.rect.height == height;
}

} // namespace

RAWFRAME_TEST(NodesAreLaidOutInTheirPlayersView) {
    Rig rig;
    rig.put(rig.player, kHudId, panel());
    rig.put(rig.player, kMeterId, meter(100));
    // Two rows on other entities, in the panel on the player, by order.
    const world::EntityHandle kFirst = *rig.world.create();
    const world::EntityHandle kSecond = *rig.world.create();
    rig.put(kFirst, kRowId, Node{.widthOffset = 20, .shrink = 1, .fill = 0xC04040FF, .order = 2});
    rig.put(kSecond, kRowId, Node{.widthOffset = 30, .shrink = 1, .fill = 0x4040C0FF, .order = 1});
    RAWFRAME_EXPECT(rig.frame());
    const ui::DrawList& kDrawn = rig.ui->drawn();
    RAWFRAME_EXPECT(kDrawn.boxes.size() == 4 && kDrawn.scale == 1);
    if (kDrawn.boxes.size() != 4) {
        return;
    }
    // The panel at the view's top left; in it the meter, then the rows,
    // the second first by its order, stretched across the padding.
    RAWFRAME_EXPECT(at(kDrawn.boxes[0], 100, 50, 300, 40) && kDrawn.boxes[0].radii[0] == 6);
    RAWFRAME_EXPECT(at(kDrawn.boxes[1], 105, 55, 100, 30));
    RAWFRAME_EXPECT(at(kDrawn.boxes[2], 215, 55, 30, 30));
    RAWFRAME_EXPECT(at(kDrawn.boxes[3], 255, 55, 20, 30));
    RAWFRAME_EXPECT(rig.ui->statistics().made == 4 && rig.ui->statistics().leftOut == 0);
}

RAWFRAME_TEST(ANodeFollowsItsComponent) {
    Rig rig;
    rig.put(rig.player, kHudId, panel());
    rig.put(rig.player, kMeterId, meter(100));
    RAWFRAME_EXPECT(rig.frame());
    // Unchanged, nothing is given to the tree again.
    RAWFRAME_EXPECT(rig.frame());
    RAWFRAME_EXPECT(rig.ui->statistics().made == 2 && rig.ui->statistics().changed == 0);
    rig.put(rig.player, kMeterId, meter(250));
    RAWFRAME_EXPECT(rig.frame());
    RAWFRAME_EXPECT(rig.ui->statistics().changed == 1 && rig.ui->drawn().boxes.size() == 2 &&
                    at(rig.ui->drawn().boxes[1], 105, 55, 250, 30));
    // Absolute, along the view's bottom, centered.
    Node bottom = panel();
    bottom.absolute = 1;
    bottom.xScale = 0.5F;
    bottom.yScale = 1;
    bottom.yOffset = -10;
    bottom.anchorX = 0.5F;
    bottom.anchorY = 1;
    rig.put(rig.player, kHudId, bottom);
    RAWFRAME_EXPECT(rig.frame());
    RAWFRAME_EXPECT(at(rig.ui->drawn().boxes[0], 150, 300, 300, 40));
    // The panel gone, the meter has no parent: left out, counted.
    RAWFRAME_EXPECT(rig.world.removeErased(rig.player, *rig.schema->find(kHudId)).has_value());
    RAWFRAME_EXPECT(rig.frame());
    RAWFRAME_EXPECT(rig.ui->drawn().boxes.empty() && rig.ui->statistics().leftOut == 1);
    // Back, the meter is in it again.
    rig.put(rig.player, kHudId, panel());
    RAWFRAME_EXPECT(rig.frame());
    RAWFRAME_EXPECT(rig.ui->drawn().boxes.size() == 2 && at(rig.ui->drawn().boxes[1], 105, 55, 250, 30));
}

RAWFRAME_TEST(TheListIsInLogicalPixelsAtTheDevicesScale) {
    Rig rig;
    rig.put(rig.player, kHudId, panel());
    RAWFRAME_EXPECT(rig.frame(2));
    RAWFRAME_EXPECT(rig.ui->drawn().scale == 2 && at(rig.ui->drawn().boxes[0], 100, 50, 300, 40));
}

RAWFRAME_TEST(ANewWorldStartsAfresh) {
    Rig rig;
    rig.put(rig.player, kHudId, panel());
    RAWFRAME_EXPECT(rig.frame());
    world::World other{rig.schema};
    const world::EntityHandle kPlayer = *other.create();
    Node node = panel();
    node.widthOffset = 120;
    RAWFRAME_EXPECT(other.insertErased(kPlayer, *rig.schema->find(kHudId), &node).has_value());
    const std::array<UiView, 1> kViews = {
        UiView{.world = &other, .player = kPlayer, .x = 0, .y = 0, .width = 640, .height = 360}};
    RAWFRAME_EXPECT(rig.ui->update(kViews, 640, 360, 1).has_value());
    RAWFRAME_EXPECT(rig.ui->drawn().boxes.size() == 1 && at(rig.ui->drawn().boxes[0], 0, 0, 120, 40));
    // No World, nothing drawn.
    const std::array<UiView, 1> kNone = {UiView{.width = 640, .height = 360}};
    RAWFRAME_EXPECT(rig.ui->update(kNone, 640, 360, 1).has_value());
    RAWFRAME_EXPECT(rig.ui->drawn().boxes.empty());
}

RAWFRAME_TEST(WhatTheTreeCannotTakeIsLeftOut) {
    Rig rig;
    Node bad = panel();
    bad.direction = 9;
    rig.put(rig.player, kHudId, bad);
    RAWFRAME_EXPECT(rig.frame() && rig.frame());
    // Counted each frame it stays so, and the meter in it has no parent.
    RAWFRAME_EXPECT(rig.ui->drawn().boxes.empty() && rig.ui->statistics().leftOut == 2);
    Node negative = panel();
    negative.radius = -1;
    rig.put(rig.player, kHudId, negative);
    RAWFRAME_EXPECT(rig.frame() && rig.ui->drawn().boxes.empty());
    rig.put(rig.player, kHudId, panel());
    RAWFRAME_EXPECT(rig.frame() && rig.ui->drawn().boxes.size() == 1);
    // A gradient of a kind past the set is refused; a linear one drawn.
    Node ramped = panel();
    ramped.gradientKind = 9;
    ramped.gradientFrom = 0xFF0000FF;
    ramped.gradientTo = 0x0000FFFF;
    rig.put(rig.player, kHudId, ramped);
    RAWFRAME_EXPECT(rig.frame() && rig.ui->drawn().boxes.empty());
    ramped.gradientKind = 1;
    rig.put(rig.player, kHudId, ramped);
    RAWFRAME_EXPECT(rig.frame() && rig.ui->drawn().boxes.size() == 1 && rig.ui->drawn().gradients.size() == 2 &&
                    rig.ui->drawn().boxes[0].gradient == 1);
    // At most so many nodes.
    auto small = WorldUi::create({.nodes = {kHudId}, .parents = {std::nullopt}, .maximumNodes = 1});
    RAWFRAME_EXPECT(small.has_value());
    const world::EntityHandle kOther = *rig.world.create();
    rig.put(kOther, kHudId, panel());
    const std::array<UiView, 1> kViews = {
        UiView{.world = &rig.world, .player = rig.player, .width = 640, .height = 360}};
    RAWFRAME_EXPECT((*small)->update(kViews, 640, 360, 1).has_value());
    RAWFRAME_EXPECT((*small)->drawn().boxes.size() == 1 && (*small)->statistics().leftOut == 1);
    // A parent past the components is refused.
    const auto kRefused = WorldUi::create({.nodes = {kHudId}, .parents = {std::size_t{1}}});
    RAWFRAME_EXPECT(!kRefused.has_value() && kRefused.error().domain() == kWorldUiDomain);
}

RAWFRAME_TEST(ANodeShowsItsLabelsWordsInTheGamesFont) {
    Rig rig;
    std::uint64_t asked = 0;
    rig.ui =
        *WorldUi::create({.nodes = {kHudId, kMeterId, kRowId},
                          .parents = {std::nullopt, 0, 0},
                          .fonts = {0xF2, 0xF1},
                          .words = [&asked](std::uint64_t label, std::int64_t value) -> std::optional<std::string> {
                              ++asked;
                              if (label != 0xA1) {
                                  return std::nullopt;
                              }
                              return std::string(static_cast<std::size_t>(value), 'X');
                          }});
    // Words in Ahem, whose glyphs are em boxes: three of them, 10 pixels.
    rig.put(
        rig.player, kHudId, Node{.text = 0xA1, .textValue = 3, .font = 0xF1, .textSize = 10, .textColor = 0xFFFFFFFF});
    RAWFRAME_EXPECT(rig.frame());
    RAWFRAME_EXPECT(asked == 1 && rig.ui->statistics().texts == 1);
    // No font read yet: nothing is drawn.
    RAWFRAME_EXPECT(rig.ui->drawn().glyphRuns.empty());

    const std::string kAhem = test::readFile(RAWFRAME_UI_FONTS "Ahem.ttf");
    const auto kBytes = std::as_bytes(std::span{kAhem.data(), kAhem.size()});
    RAWFRAME_EXPECT(rig.ui->addFont(0xF1, kBytes).has_value());
    // Read once, and only fonts the game declares.
    RAWFRAME_EXPECT(!rig.ui->addFont(0xF1, kBytes).has_value() && !rig.ui->addFont(0xF3, kBytes).has_value());
    RAWFRAME_EXPECT(rig.frame());
    const ui::DrawList& kDrawn = rig.ui->drawn();
    RAWFRAME_EXPECT(kDrawn.glyphRuns.size() == 1 && kDrawn.glyphs.size() == 3);
    if (kDrawn.glyphRuns.size() == 1) {
        // At the view's top left, on a baseline 8 pixels down.
        RAWFRAME_EXPECT(kDrawn.glyphRuns[0].size == 10 && kDrawn.glyphRuns[0].x + kDrawn.glyphs[0].x == 100 &&
                        kDrawn.glyphRuns[0].y + kDrawn.glyphs[0].y == 58);
    }
    // Its words change with its value; a label the game does not have, or a
    // text look past the constants, shows nothing.
    rig.put(
        rig.player, kHudId, Node{.text = 0xA1, .textValue = 5, .font = 0xF1, .textSize = 10, .textColor = 0xFFFFFFFF});
    RAWFRAME_EXPECT(rig.frame() && rig.ui->drawn().glyphs.size() == 5);
    rig.put(rig.player, kHudId, Node{.text = 0xA9, .textSize = 10, .textColor = 0xFFFFFFFF});
    RAWFRAME_EXPECT(rig.frame() && rig.ui->drawn().glyphs.empty() && rig.ui->statistics().textsUnknown == 1);
    rig.put(rig.player, kHudId, Node{.text = 0xA1, .textValue = 2, .textAlign = 3});
    RAWFRAME_EXPECT(rig.frame() && rig.ui->drawn().glyphs.empty());
    RAWFRAME_EXPECT(rig.ui->statistics().leftOut == 1);
    // Font nought is the first declared font read so far: here the one.
    rig.put(rig.player, kHudId, Node{.text = 0xA1, .textValue = 2, .textSize = 10, .textColor = 0xFFFFFFFF});
    RAWFRAME_EXPECT(rig.frame() && rig.ui->drawn().glyphs.size() == 2);
}

RAWFRAME_TEST(APressLandsOnANodeThatTakesIt) {
    // The panel along the view's top at (100, 50), the meter in it at
    // (105, 55): neither takes a press until it says so.
    Rig rig;
    rig.put(rig.player, kHudId, panel());
    rig.put(rig.player, kMeterId, meter(100));
    RAWFRAME_EXPECT(rig.frame());
    RAWFRAME_EXPECT(!rig.ui->press(110, 60).has_value() && !rig.ui->press(10, 10).has_value());
    // A meter that is its own, with a code: pressed on it, the code; on the
    // panel around it, through.
    Node pressing = meter(100);
    pressing.hit = 1;
    pressing.press = 42;
    rig.put(rig.player, kMeterId, pressing);
    RAWFRAME_EXPECT(rig.frame());
    RAWFRAME_EXPECT(rig.ui->press(110, 60) == std::optional<std::int64_t>{42});
    RAWFRAME_EXPECT(!rig.ui->press(350, 60).has_value());
    // A panel that blocks and says nothing; one that leaves its subtree out.
    Node blocking = panel();
    blocking.hit = 1;
    rig.put(rig.player, kHudId, blocking);
    RAWFRAME_EXPECT(rig.frame());
    RAWFRAME_EXPECT(rig.ui->press(350, 60) == std::optional<std::int64_t>{0});
    RAWFRAME_EXPECT(rig.ui->press(110, 60) == std::optional<std::int64_t>{42});
    blocking.hit = 2;
    rig.put(rig.player, kHudId, blocking);
    RAWFRAME_EXPECT(rig.frame());
    RAWFRAME_EXPECT(!rig.ui->press(110, 60).has_value());
    // A hit or a layer past the constants is left out.
    blocking.hit = 3;
    rig.put(rig.player, kHudId, blocking);
    RAWFRAME_EXPECT(rig.frame());
    RAWFRAME_EXPECT(!rig.ui->press(110, 60).has_value() && rig.ui->statistics().leftOut > 0);
}

RAWFRAME_TEST(ATextFieldTakesTheKeyboardAndGivesItsText) {
    Rig rig;
    rig.ui = *WorldUi::create({.nodes = {kHudId, kMeterId, kRowId}, .parents = {std::nullopt, 0, 0}, .fonts = {0xF1}});
    const std::string kAhem = test::readFile(RAWFRAME_UI_FONTS "Ahem.ttf");
    RAWFRAME_EXPECT(rig.ui->addFont(0xF1, std::as_bytes(std::span{kAhem.data(), kAhem.size()})).has_value());
    // A field 200 by 20 at the view's top left, (100, 50), in Ahem at 10.
    Node field{.widthOffset = 200,
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

RAWFRAME_TEST(ANodeShowsTypedWordsInPlaceOfItsLabel) {
    Rig rig;
    rig.ui = *WorldUi::create({.nodes = {kHudId, kMeterId, kRowId},
                               .parents = {std::nullopt, 0, 0},
                               .shows = {kWordsId, std::nullopt, std::nullopt},
                               .fonts = {0xF1},
                               .words = [](std::uint64_t, std::int64_t) -> std::optional<std::string> {
                                   return std::string{"LABEL"};
                               }});
    const std::string kAhem = test::readFile(RAWFRAME_UI_FONTS "Ahem.ttf");
    RAWFRAME_EXPECT(rig.ui->addFont(0xF1, std::as_bytes(std::span{kAhem.data(), kAhem.size()})).has_value());
    const auto kTyped = [](std::string_view text) {
        Typed typed{.length = static_cast<std::uint32_t>(text.size())};
        std::memcpy(typed.bytes.data(), text.data(), text.size());
        return typed;
    };
    rig.put(rig.player, kHudId, Node{.text = 0xA1, .textSize = 10, .textColor = 0xFFFFFFFF});
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
    rig.put(rig.player, kMeterId, Node{.text = 0xA1, .textSize = 10, .textColor = 0xFFFFFFFF});
    RAWFRAME_EXPECT(rig.frame() && rig.ui->drawn().glyphs.size() == 6);
}

RAWFRAME_TEST(AnEmptyFieldShowsItsLabelUntilItHasTheKeyboard) {
    Rig rig;
    rig.ui = *WorldUi::create({.nodes = {kHudId, kMeterId, kRowId},
                               .parents = {std::nullopt, 0, 0},
                               .fonts = {0xF1},
                               .words = [](std::uint64_t, std::int64_t) -> std::optional<std::string> {
                                   return std::string{"NAME"};
                               }});
    const std::string kAhem = test::readFile(RAWFRAME_UI_FONTS "Ahem.ttf");
    RAWFRAME_EXPECT(rig.ui->addFont(0xF1, std::as_bytes(std::span{kAhem.data(), kAhem.size()})).has_value());
    rig.put(rig.player,
            kHudId,
            Node{.widthOffset = 200,
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
