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
// placeholder (D428); Tab moves the keyboard between a view's fields
// (D429); navigation moves focus without a pointer (D430); a styled node
// looks as its class says in each state it is in (D431); and a node that
// scrolls moves its children under a wheel turned over it (D444).

#include "rawframe/test/files.h"
#include "rawframe/test/test.h"
#include "rawframe/ui/styles.h"
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
    /// window 640 by 360 at `scale`, at `seconds`.
    bool frame(float scale = 1, double seconds = 0) {
        const std::array<UiView, 1> kViews = {
            UiView{.world = &world, .player = player, .x = 100, .y = 50, .width = 400, .height = 300}};
        return ui->update(kViews, 640, 360, scale, seconds).has_value();
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
    rig.ui = *WorldUi::create(
        {.nodes = {kHudId, kMeterId, kRowId},
         .parents = {std::nullopt, 0, 0},
         .fonts = {0xF2, 0xF1},
         .words = [&asked](std::size_t, std::uint64_t label, std::int64_t value) -> std::optional<std::string> {
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

RAWFRAME_TEST(EachLocalPlayersWordsAreInTheirOwnLocale) {
    // Two local players, each in their own view of their own World, the
    // second asking for words twice as long (ADR-0050's per-player locale).
    Rig rig;
    std::vector<std::size_t> askedBy;
    rig.ui = *WorldUi::create(
        {.nodes = {kHudId, kMeterId, kRowId},
         .parents = {std::nullopt, 0, 0},
         .fonts = {0xF1},
         .words = [&askedBy](std::size_t player, std::uint64_t, std::int64_t value) -> std::optional<std::string> {
             askedBy.push_back(player);
             return std::string(static_cast<std::size_t>(value) * (player + 1), 'X');
         }});
    const std::string kAhem = test::readFile(RAWFRAME_UI_FONTS "Ahem.ttf");
    RAWFRAME_EXPECT(rig.ui->addFont(0xF1, std::as_bytes(std::span{kAhem.data(), kAhem.size()})).has_value());
    Node label{.text = 0xA1, .textValue = 3, .font = 0xF1, .textSize = 10, .textColor = 0xFFFFFFFF};
    rig.put(rig.player, kHudId, label);
    world::World second{rig.schema};
    const world::EntityHandle kSecond = *second.create();
    RAWFRAME_EXPECT(second.insertErased(kSecond, *rig.schema->find(kHudId), &label).has_value());
    const std::array<UiView, 2> kViews = {
        UiView{.world = &rig.world, .player = rig.player, .width = 320, .height = 360},
        UiView{.world = &second, .player = kSecond, .x = 320, .width = 320, .height = 360}};
    RAWFRAME_EXPECT(rig.ui->update(kViews, 640, 360, 1).has_value());
    RAWFRAME_EXPECT((askedBy == std::vector<std::size_t>{0, 1}));
    const ui::DrawList& kDrawn = rig.ui->drawn();
    RAWFRAME_EXPECT(kDrawn.glyphRuns.size() == 2 && kDrawn.glyphs.size() == 9);
    // A change of locale words both views again, each in its player's.
    askedBy.clear();
    rig.ui->reword();
    RAWFRAME_EXPECT(rig.ui->update(kViews, 640, 360, 1).has_value());
    RAWFRAME_EXPECT((askedBy == std::vector<std::size_t>{0, 1}) && rig.ui->drawn().glyphs.size() == 9);
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
                               .words = [](std::size_t, std::uint64_t, std::int64_t) -> std::optional<std::string> {
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
                               .words = [](std::size_t, std::uint64_t, std::int64_t) -> std::optional<std::string> {
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

RAWFRAME_TEST(TabMovesTheKeyboardBetweenAViewsFields) {
    Rig rig;
    rig.ui = *WorldUi::create({.nodes = {kHudId, kMeterId, kRowId}, .parents = {std::nullopt, 0, 0}, .fonts = {0xF1}});
    const std::string kAhem = test::readFile(RAWFRAME_UI_FONTS "Ahem.ttf");
    RAWFRAME_EXPECT(rig.ui->addFont(0xF1, std::as_bytes(std::span{kAhem.data(), kAhem.size()})).has_value());
    // In the panel, side by side: the row's field first (105, 55), then
    // the meter's (215, 55), whatever order they were made in.
    const auto kField = [](std::int64_t press, std::int32_t order) {
        return Node{.widthOffset = 100,
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

RAWFRAME_TEST(NavigationMovesFocusAndActivatesWhatItHolds) {
    Rig rig;
    rig.ui = *WorldUi::create({.nodes = {kHudId, kMeterId, kRowId}, .parents = {std::nullopt, 0, 0}, .fonts = {0xF1}});
    const std::string kAhem = test::readFile(RAWFRAME_UI_FONTS "Ahem.ttf");
    RAWFRAME_EXPECT(rig.ui->addFont(0xF1, std::as_bytes(std::span{kAhem.data(), kAhem.size()})).has_value());
    // In the panel, side by side: a node that takes presses (105, 55),
    // then a field (215, 55).
    rig.put(rig.player, kHudId, panel());
    RAWFRAME_EXPECT(rig.frame() && rig.ui->reachable() == 0);
    // Navigation reaches each as it is made (D476).
    rig.put(rig.player, kMeterId, Node{.widthOffset = 100, .heightOffset = 20, .fill = 0x40C040FF, .press = 1});
    RAWFRAME_EXPECT(rig.frame() && rig.ui->reachable() == 1);
    rig.put(rig.player,
            kRowId,
            Node{.widthOffset = 100,
                 .heightOffset = 20,
                 .order = 1,
                 .font = 0xF1,
                 .textSize = 10,
                 .textColor = 0xFFFFFFFF,
                 .press = 2,
                 .edit = 1});
    RAWFRAME_EXPECT(rig.frame() && rig.ui->reachable() == 2);
    const auto kRinged = [&rig] {
        return rig.frame() ? rig.ui->drawn().shadows.size() : std::size_t{99};
    };
    // Nothing holds focus: nothing moves, nothing is activated.
    RAWFRAME_EXPECT(!rig.ui->navigating() && !rig.ui->navigate(view::NavigationMove::Right) &&
                    !rig.ui->activate().has_value() && kRinged() == 0);
    // Entered, the first in reading order, ringed; activated, its code.
    RAWFRAME_EXPECT(rig.ui->enterNavigation() && rig.ui->navigating() && kRinged() == 1);
    const auto kPressed = rig.ui->activate();
    RAWFRAME_EXPECT(kPressed.has_value() && *kPressed == 1 && rig.ui->statistics().activated == 1);
    // Nothing above it; the field to its right, which takes the keyboard.
    RAWFRAME_EXPECT(!rig.ui->navigate(view::NavigationMove::Up) && rig.ui->navigate(view::NavigationMove::Right));
    RAWFRAME_EXPECT(!rig.ui->activate().has_value() && kRinged() == 1 && rig.ui->caret().has_value() &&
                    (*rig.ui->caret())[0] == 215);
    rig.ui->type(view::Typing{.kind = view::Typing::Kind::Text, .text = "x"});
    // Enter gives its text, and focus stays on it without the keyboard.
    rig.ui->type(view::Typing{.kind = view::Typing::Kind::Key, .key = view::TypingKey::Submit});
    const auto kGiven = rig.ui->takeSubmitted();
    RAWFRAME_EXPECT(kGiven.size() == 1 && kGiven[0].press == 2 && kGiven[0].text == "x");
    RAWFRAME_EXPECT(rig.ui->navigating() && kRinged() == 1 && !rig.ui->caret().has_value());
    // Escape too: the keyboard given back, focus kept.
    RAWFRAME_EXPECT(!rig.ui->activate().has_value() && rig.ui->caret().has_value());
    rig.ui->type(view::Typing{.kind = view::Typing::Kind::Key, .key = view::TypingKey::Dismiss});
    RAWFRAME_EXPECT(rig.ui->navigating() && kRinged() == 1 && !rig.ui->caret().has_value());
    // Next goes round to the first; nothing left of it.
    RAWFRAME_EXPECT(rig.ui->navigate(view::NavigationMove::Next) && !rig.ui->navigate(view::NavigationMove::Left));
    RAWFRAME_EXPECT(rig.ui->navigate(view::NavigationMove::Previous) && rig.ui->navigate(view::NavigationMove::Left));
    RAWFRAME_EXPECT(rig.ui->statistics().navigated == 5);
    // Dismissed with no keyboard held, focus goes and so does the ring.
    rig.ui->dismiss();
    RAWFRAME_EXPECT(!rig.ui->navigating() && kRinged() == 0);
    // A press elsewhere ends navigation too.
    RAWFRAME_EXPECT(rig.ui->enterNavigation());
    rig.ui->pressAt(600, 300);
    RAWFRAME_EXPECT(!rig.ui->navigating());
}

RAWFRAME_TEST(AStyledNodeLooksAsItsClassSaysInEachState) {
    Rig rig;
    const auto kStyles = ui::readStyles(R"({
  "kind": "ui.styles",
  "formatVersion": 1,
  "styles": [
    {
      "styleId": "0000000000000011",
      "name": "button",
      "base": {
        "fill": "#ff0000ff"
      },
      "focused": {
        "outerShadow": {
          "color": "#ffffffff",
          "spread": 2
        }
      },
      "hovered": {
        "fill": "#00ff00ff"
      },
      "pressed": {
        "fill": "#0000ffff"
      }
    }
  ]
}
)");
    RAWFRAME_EXPECT(kStyles.has_value());
    if (!kStyles.has_value()) {
        return;
    }
    rig.ui =
        *WorldUi::create({.nodes = {kHudId, kMeterId, kRowId}, .parents = {std::nullopt, 0, 0}, .styles = *kStyles});
    // A button at the view's top left, (100, 50), 100 by 20.
    Node button{.widthOffset = 100, .heightOffset = 20, .press = 1, .hit = 1, .style = 0x11};
    rig.put(rig.player, kHudId, button);
    // The button's box's fill as drawn: red, green, blue, nought to one.
    const auto kFill = [&rig]() -> std::array<float, 3> {
        if (!rig.frame()) {
            return {-1, -1, -1};
        }
        for (const ui::Box& kBox : rig.ui->drawn().boxes) {
            if (at(kBox, 100, 50, 100, 20)) {
                return {kBox.fill[0], kBox.fill[1], kBox.fill[2]};
            }
        }
        return {-1, -1, -1};
    };
    const auto kIs = [](std::array<float, 3> fill, float red, float green, float blue) {
        return std::abs(fill[0] - red) < 0.02F && std::abs(fill[1] - green) < 0.02F && std::abs(fill[2] - blue) < 0.02F;
    };
    RAWFRAME_EXPECT(kIs(kFill(), 1, 0, 0));
    // Hovered, pressed (which wins), let go, and left.
    rig.ui->hoverAt(std::array<float, 2>{150, 60});
    RAWFRAME_EXPECT(kIs(kFill(), 0, 1, 0));
    rig.ui->pressAt(150, 60);
    RAWFRAME_EXPECT(kIs(kFill(), 0, 0, 1));
    rig.ui->release();
    RAWFRAME_EXPECT(kIs(kFill(), 0, 1, 0));
    rig.ui->hoverAt(std::nullopt);
    RAWFRAME_EXPECT(kIs(kFill(), 1, 0, 0));
    // Focused, the class's shadow, not the engine's ring.
    RAWFRAME_EXPECT(rig.ui->enterNavigation() && rig.frame() && rig.ui->drawn().shadows.size() == 1);
    rig.ui->dismiss();
    RAWFRAME_EXPECT(rig.frame() && rig.ui->drawn().shadows.empty());
    // Its own fill wins in every state.
    button.fill = 0xFFFF00FF;
    rig.put(rig.player, kHudId, button);
    rig.ui->hoverAt(std::array<float, 2>{150, 60});
    RAWFRAME_EXPECT(kIs(kFill(), 1, 1, 0));
    // A class the game's styles lack: counted, the node unstyled, with no
    // fill of its own.
    button.fill = 0;
    button.style = 0x12;
    rig.put(rig.player, kHudId, button);
    const std::array<float, 3> kUnstyled = kFill();
    RAWFRAME_EXPECT(!kIs(kUnstyled, 1, 0, 0) && !kIs(kUnstyled, 0, 1, 0) && rig.ui->statistics().stylesUnknown == 1);
}

RAWFRAME_TEST(ANodeThatScrollsMovesItsChildrenUnderTheWheel) {
    // The panel a column that scrolls, 30 pixels inside its padding; five
    // rows of 20 in it, 10 apart, reach 140.
    Rig rig;
    Node scrolling = panel();
    scrolling.direction = 2;
    scrolling.scroll = 2;
    rig.put(rig.player, kHudId, scrolling);
    for (int each = 0; each < 5; ++each) {
        rig.put(*rig.world.create(), kRowId, Node{.heightOffset = 20, .shrink = 0, .fill = 0xC04040FF});
    }
    RAWFRAME_EXPECT(rig.frame(1, 1.0));
    const auto kFirstRow = [&rig]() {
        const ui::DrawList& kDrawn = rig.ui->drawn();
        return kDrawn.boxes.size() > 1 ? kDrawn.boxes[1].rect.y : -1.0F;
    };
    RAWFRAME_EXPECT(kFirstRow() == 55);
    // It takes no press: one on it still reaches the game.
    RAWFRAME_EXPECT(!rig.ui->press(150, 70).has_value());
    // Over the panel, a turn toward the user moves the rows up, eased.
    rig.ui->wheelAt(150, 70, 0, -1);
    for (double seconds = 1.05; seconds < 2; seconds += 0.05) {
        RAWFRAME_EXPECT(rig.frame(1, seconds));
    }
    RAWFRAME_EXPECT(rig.ui->statistics().wheeled == 1);
    RAWFRAME_EXPECT(kFirstRow() < 55);
    // Off it, nothing scrolls; a scroll past the constants is left out.
    rig.ui->wheelAt(150, 200, 0, -1);
    RAWFRAME_EXPECT(rig.ui->statistics().wheeled == 1);
    scrolling.scroll = 4;
    rig.put(rig.player, kHudId, scrolling);
    RAWFRAME_EXPECT(rig.frame(1, 3.0));
    RAWFRAME_EXPECT(rig.ui->statistics().leftOut > 0);
}

RAWFRAME_TEST(AViewIsLaidOutInsideWhatThePlatformLeavesClear) {
    // A phone's notch and home indicator (D589): a view on the window's
    // edges is drawn in by what covers them; an edge between two players'
    // views stays; with no window told, nothing changes.
    const view::ViewInsets kSafe{.top = 59, .right = 0, .bottom = 34, .left = 0};
    const view::ViewSize kWindow{.width = 402, .height = 874};
    const UiView kWhole = insideSafeArea(UiView{.width = 402, .height = 874}, kWindow, kSafe);
    RAWFRAME_EXPECT(kWhole.x == 0 && kWhole.y == 59 && kWhole.width == 402 && kWhole.height == 874 - 59 - 34);
    const UiView kUpper = insideSafeArea(UiView{.width = 402, .height = 437}, kWindow, kSafe);
    RAWFRAME_EXPECT(kUpper.y == 59 && kUpper.height == 437 - 59);
    const UiView kLower = insideSafeArea(UiView{.y = 437, .width = 402, .height = 437}, kWindow, kSafe);
    RAWFRAME_EXPECT(kLower.y == 437 && kLower.height == 437 - 34);
    // A landscape phone's notch on the left.
    const UiView kSide = insideSafeArea(
        UiView{.width = 874, .height = 402}, {.width = 874, .height = 402}, {.right = 0, .bottom = 21, .left = 59});
    RAWFRAME_EXPECT(kSide.x == 59 && kSide.width == 874 - 59 && kSide.height == 402 - 21);
    const UiView kUntold = insideSafeArea(UiView{.width = 640, .height = 360}, {}, kSafe);
    RAWFRAME_EXPECT(kUntold.y == 0 && kUntold.width == 640 && kUntold.height == 360);
}
