// The UI as World data (D376): node components laid out in their player's
// view, nested by their components' parents on the same entity or on the
// player, siblings in order; a node changes, goes, and is left out as its
// component does; nothing unchanged is laid out again; a new World starts
// afresh; values the tree cannot take, a gradient of no kind among them,
// are left out and counted; and a node shows its label's words in the
// game's font once it is read, sized by them (D386); a press lands on the
// node that takes it, with its code, or passes through (D421); a text
// field's keyboard, typing, and placeholder are fields_test.cpp's;
// navigation moves focus without a pointer (D430); a styled node looks as
// its class says in each state it is in (D431); and a node that scrolls
// moves its children under a wheel turned over it (D444).

#include "rawframe/test/files.h"
#include "rawframe/test/test.h"
#include "rawframe/ui/access.h"
#include "rawframe/ui/styles.h"
#include "rawframe/world_ui/errors.h"
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

RAWFRAME_TEST(NodesAreLaidOutInTheirPlayersView) {
    Rig rig;
    rig.put(rig.player, kHudId, panel());
    rig.put(rig.player, kMeterId, meter(100));
    // Two rows on other entities, in the panel on the player, by order.
    const world::EntityHandle kFirst = *rig.world.create();
    const world::EntityHandle kSecond = *rig.world.create();
    rig.put(kFirst, kRowId, ui::NodeFields{.widthOffset = 20, .shrink = 1, .fill = 0xC04040FF, .order = 2});
    rig.put(kSecond, kRowId, ui::NodeFields{.widthOffset = 30, .shrink = 1, .fill = 0x4040C0FF, .order = 1});
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
    ui::NodeFields bottom = panel();
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
    ui::NodeFields node = panel();
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
    ui::NodeFields bad = panel();
    bad.direction = 9;
    rig.put(rig.player, kHudId, bad);
    RAWFRAME_EXPECT(rig.frame() && rig.frame());
    // Counted each frame it stays so, and the meter in it has no parent.
    RAWFRAME_EXPECT(rig.ui->drawn().boxes.empty() && rig.ui->statistics().leftOut == 2);
    ui::NodeFields negative = panel();
    negative.radius = -1;
    rig.put(rig.player, kHudId, negative);
    RAWFRAME_EXPECT(rig.frame() && rig.ui->drawn().boxes.empty());
    rig.put(rig.player, kHudId, panel());
    RAWFRAME_EXPECT(rig.frame() && rig.ui->drawn().boxes.size() == 1);
    // A gradient of a kind past the set is refused; a linear one drawn.
    ui::NodeFields ramped = panel();
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
    rig.ui = *WorldUi::create({.nodes = {kHudId, kMeterId, kRowId},
                               .parents = {std::nullopt, 0, 0},
                               .fonts = {0xF2, 0xF1},
                               .words = [&asked](std::size_t, std::uint64_t label, std::array<std::int64_t, 2> values)
                                   -> std::optional<std::string> {
                                   ++asked;
                                   if (label != 0xA1) {
                                       return std::nullopt;
                                   }
                                   return std::string(static_cast<std::size_t>(values[0] + values[1]), 'X');
                               }});
    // Words in Ahem, whose glyphs are em boxes: three of them, 10 pixels.
    rig.put(rig.player,
            kHudId,
            ui::NodeFields{.text = 0xA1, .textValue = 3, .font = 0xF1, .textSize = 10, .textColor = 0xFFFFFFFF});
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
    rig.put(rig.player,
            kHudId,
            ui::NodeFields{.text = 0xA1, .textValue = 5, .font = 0xF1, .textSize = 10, .textColor = 0xFFFFFFFF});
    RAWFRAME_EXPECT(rig.frame() && rig.ui->drawn().glyphs.size() == 5);
    // And with its second value, its label's second argument (D647).
    rig.put(rig.player,
            kHudId,
            ui::NodeFields{
                .text = 0xA1, .textValue = 5, .textSecond = 2, .font = 0xF1, .textSize = 10, .textColor = 0xFFFFFFFF});
    RAWFRAME_EXPECT(rig.frame() && rig.ui->drawn().glyphs.size() == 7);
    rig.put(rig.player, kHudId, ui::NodeFields{.text = 0xA9, .textSize = 10, .textColor = 0xFFFFFFFF});
    RAWFRAME_EXPECT(rig.frame() && rig.ui->drawn().glyphs.empty() && rig.ui->statistics().textsUnknown == 1);
    rig.put(rig.player, kHudId, ui::NodeFields{.text = 0xA1, .textValue = 2, .textAlign = 3});
    RAWFRAME_EXPECT(rig.frame() && rig.ui->drawn().glyphs.empty());
    RAWFRAME_EXPECT(rig.ui->statistics().leftOut == 1);
    // Font nought is the first declared font read so far: here the one.
    rig.put(rig.player, kHudId, ui::NodeFields{.text = 0xA1, .textValue = 2, .textSize = 10, .textColor = 0xFFFFFFFF});
    RAWFRAME_EXPECT(rig.frame() && rig.ui->drawn().glyphs.size() == 2);
}

RAWFRAME_TEST(EachLocalPlayersWordsAreInTheirOwnLocale) {
    // Two local players, each in their own view of their own World, the
    // second asking for words twice as long (ADR-0050's per-player locale).
    Rig rig;
    std::vector<std::size_t> askedBy;
    rig.ui = *WorldUi::create({.nodes = {kHudId, kMeterId, kRowId},
                               .parents = {std::nullopt, 0, 0},
                               .fonts = {0xF1},
                               .words = [&askedBy](std::size_t player,
                                                   std::uint64_t,
                                                   std::array<std::int64_t, 2> values) -> std::optional<std::string> {
                                   askedBy.push_back(player);
                                   return std::string(static_cast<std::size_t>(values[0]) * (player + 1), 'X');
                               }});
    const std::string kAhem = test::readFile(RAWFRAME_UI_FONTS "Ahem.ttf");
    RAWFRAME_EXPECT(rig.ui->addFont(0xF1, std::as_bytes(std::span{kAhem.data(), kAhem.size()})).has_value());
    ui::NodeFields label{.text = 0xA1, .textValue = 3, .font = 0xF1, .textSize = 10, .textColor = 0xFFFFFFFF};
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
    ui::NodeFields pressing = meter(100);
    pressing.hit = 1;
    pressing.press = 42;
    rig.put(rig.player, kMeterId, pressing);
    RAWFRAME_EXPECT(rig.frame());
    RAWFRAME_EXPECT(rig.ui->press(110, 60) == std::optional<std::int64_t>{42});
    RAWFRAME_EXPECT(!rig.ui->press(350, 60).has_value());
    // A panel that blocks and says nothing; one that leaves its subtree out.
    ui::NodeFields blocking = panel();
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
    rig.put(
        rig.player, kMeterId, ui::NodeFields{.widthOffset = 100, .heightOffset = 20, .fill = 0x40C040FF, .press = 1});
    RAWFRAME_EXPECT(rig.frame() && rig.ui->reachable() == 1);
    rig.put(rig.player,
            kRowId,
            ui::NodeFields{.widthOffset = 100,
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
    ui::NodeFields button{.widthOffset = 100, .heightOffset = 20, .press = 1, .hit = 1, .style = 0x11};
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
    ui::NodeFields scrolling = panel();
    scrolling.direction = 2;
    scrolling.scroll = 2;
    rig.put(rig.player, kHudId, scrolling);
    for (int each = 0; each < 5; ++each) {
        rig.put(*rig.world.create(), kRowId, ui::NodeFields{.heightOffset = 20, .shrink = 0, .fill = 0xC04040FF});
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

RAWFRAME_TEST(ARangeIsPagedAndDraggedAndKeepsWhatThePlayerMovedItTo) {
    Rig rig;
    // A track 200 by 20 at the view's top left, 0 to 100 at 50, and its
    // thumb, 20 wide, at the value's share of it, as a present system
    // places one.
    ui::NodeFields track{.widthOffset = 200, .heightOffset = 20, .shrink = 0, .press = 9};
    track.range = 1;
    track.rangeMaximum = 100;
    track.rangeStep = 1;
    track.rangeValue = 50;
    ui::NodeFields thumb{.widthOffset = 20, .heightOffset = 20, .absolute = 1, .xScale = 0.5F, .anchorX = 0.5F};
    thumb.thumb = 1;
    rig.put(rig.player, kHudId, track);
    rig.put(rig.player, kMeterId, thumb);
    RAWFRAME_EXPECT(rig.frame());
    RAWFRAME_EXPECT(rig.ui->valueOf(0, 9) == 50.0F && !rig.ui->valueOf(0, 8).has_value() &&
                    !rig.ui->valueOf(1, 9).has_value());
    // A press on the track before the thumb pages back a tenth.
    rig.ui->hoverAt(std::array<float, 2>{110, 60});
    rig.ui->pressAt(110, 60);
    rig.ui->release();
    RAWFRAME_EXPECT(rig.frame(1, 1));
    RAWFRAME_EXPECT(rig.ui->valueOf(0, 9) == 40.0F);
    // The thumb placed at 40, from 172 to 192, grabbed at its middle and
    // dragged 36 to the right: 36 of the 180 it travels is 20 more.
    thumb.xScale = 0.4F;
    thumb.anchorX = 0.4F;
    rig.put(rig.player, kMeterId, thumb);
    RAWFRAME_EXPECT(rig.frame(1, 1.5));
    rig.ui->hoverAt(std::array<float, 2>{182, 60});
    rig.ui->pressAt(182, 60);
    rig.ui->hoverAt(std::array<float, 2>{200, 60});
    rig.ui->hoverAt(std::array<float, 2>{218, 60});
    rig.ui->release();
    RAWFRAME_EXPECT(rig.frame(1, 2));
    RAWFRAME_EXPECT(rig.ui->valueOf(0, 9) == 60.0F && rig.ui->statistics().ranged >= 4);
    // The node changed otherwise, the value the player moved stays; its own
    // value changed, the range takes it.
    track.fill = 0x303030FF;
    rig.put(rig.player, kHudId, track);
    RAWFRAME_EXPECT(rig.frame(1, 3));
    RAWFRAME_EXPECT(rig.ui->valueOf(0, 9) == 60.0F);
    track.rangeValue = 10;
    rig.put(rig.player, kHudId, track);
    RAWFRAME_EXPECT(rig.frame(1, 4));
    RAWFRAME_EXPECT(rig.ui->valueOf(0, 9) == 10.0F);
    // Reached by navigation, it takes the moves along it, a step each, and
    // leaves the others to focus.
    RAWFRAME_EXPECT(rig.ui->enterNavigation());
    RAWFRAME_EXPECT(rig.ui->navigate(view::NavigationMove::Right) && rig.ui->valueOf(0, 9) == 11.0F);
    RAWFRAME_EXPECT(rig.ui->navigate(view::NavigationMove::Left) && rig.ui->navigate(view::NavigationMove::Left) &&
                    rig.ui->valueOf(0, 9) == 9.0F);
    RAWFRAME_EXPECT(!rig.ui->navigate(view::NavigationMove::Up) && rig.ui->valueOf(0, 9) == 9.0F);
    RAWFRAME_EXPECT(rig.frame(1, 4.5));
    RAWFRAME_EXPECT(rig.ui->valueOf(0, 9) == 9.0F);
    // No longer a range, it has no value; a range past its axes is left out.
    track.range = 0;
    rig.put(rig.player, kHudId, track);
    RAWFRAME_EXPECT(rig.frame(1, 5));
    RAWFRAME_EXPECT(!rig.ui->valueOf(0, 9).has_value());
    const std::uint64_t kLeftOut = rig.ui->statistics().leftOut;
    track.range = 3;
    rig.put(rig.player, kHudId, track);
    RAWFRAME_EXPECT(rig.frame(1, 6));
    RAWFRAME_EXPECT(rig.ui->statistics().leftOut > kLeftOut);
}

RAWFRAME_TEST(ACheckableIsCheckedByPressesAndARadioUnchecksItsGroup) {
    Rig rig;
    // In the panel along the view's top: a check box, then two radio
    // buttons on other entities, each 20 by 20, 10 apart from 105.
    rig.put(rig.player, kHudId, panel());
    ui::NodeFields box{.widthOffset = 20, .heightOffset = 20, .shrink = 0, .press = 5};
    box.check = 1;
    rig.put(rig.player, kMeterId, box);
    const world::EntityHandle kFirst = *rig.world.create();
    const world::EntityHandle kSecond = *rig.world.create();
    ui::NodeFields radio{.widthOffset = 20, .heightOffset = 20, .shrink = 0, .order = 1, .press = 6};
    radio.check = 3;
    rig.put(kFirst, kRowId, radio);
    radio.order = 2;
    radio.press = 7;
    rig.put(kSecond, kRowId, radio);
    RAWFRAME_EXPECT(rig.frame());
    const auto kClick = [&rig](float x) {
        rig.ui->hoverAt(std::array<float, 2>{x, 65});
        rig.ui->pressAt(x, 65);
        rig.ui->release();
        return rig.frame();
    };
    RAWFRAME_EXPECT(rig.ui->valueOf(0, 5) == 0.0F && rig.ui->valueOf(0, 6) == 0.0F);
    // A click checks the box and another unchecks it.
    RAWFRAME_EXPECT(kClick(115) && rig.ui->valueOf(0, 5) == 1.0F);
    RAWFRAME_EXPECT(kClick(115) && rig.ui->valueOf(0, 5) == 0.0F);
    // A press let go elsewhere is no click.
    rig.ui->hoverAt(std::array<float, 2>{115, 65});
    rig.ui->pressAt(115, 65);
    rig.ui->hoverAt(std::array<float, 2>{300, 200});
    rig.ui->release();
    RAWFRAME_EXPECT(rig.frame() && rig.ui->valueOf(0, 5) == 0.0F);
    // A radio checked unchecks the one beside it; checked again, it stays.
    RAWFRAME_EXPECT(kClick(145) && rig.ui->valueOf(0, 6) == 1.0F && rig.ui->valueOf(0, 7) == 0.0F);
    RAWFRAME_EXPECT(kClick(175) && rig.ui->valueOf(0, 6) == 0.0F && rig.ui->valueOf(0, 7) == 1.0F);
    RAWFRAME_EXPECT(kClick(175) && rig.ui->valueOf(0, 7) == 1.0F);
    // Its own `checked` changed, the box takes it; the player's uncheck then
    // stays while it is unchanged.
    box.checked = 1;
    rig.put(rig.player, kMeterId, box);
    RAWFRAME_EXPECT(rig.frame() && rig.ui->valueOf(0, 5) == 1.0F);
    RAWFRAME_EXPECT(kClick(115) && rig.ui->valueOf(0, 5) == 0.0F);
    rig.put(rig.player, kMeterId, box);
    RAWFRAME_EXPECT(rig.frame() && rig.ui->valueOf(0, 5) == 0.0F);
    // Navigation's activation checks it too, and gives its press.
    RAWFRAME_EXPECT(rig.ui->enterNavigation() && rig.ui->activate() == std::optional<std::int64_t>{5});
    RAWFRAME_EXPECT(rig.ui->valueOf(0, 5) == 1.0F && rig.ui->statistics().toggled == 7);
}

RAWFRAME_TEST(APopupIsPlacedBesideItsParent) {
    Rig rig;
    rig.put(rig.player, kHudId, panel());
    // A tip 60 by 30 below the panel, 4 from it, centered on it.
    ui::NodeFields tip{.widthOffset = 60, .heightOffset = 30, .margin = 4, .fill = 0x101820FF, .layer = 3};
    tip.popup = 1;
    rig.put(rig.player, kMeterId, tip);
    RAWFRAME_EXPECT(rig.frame());
    const auto kBoxAt = [&rig](float x, float y, float width, float height) {
        for (const ui::Box& kBox : rig.ui->drawn().boxes) {
            if (at(kBox, x, y, width, height)) {
                return true;
            }
        }
        return false;
    };
    RAWFRAME_EXPECT(kBoxAt(220, 94, 60, 30));
    // Above it, where the window leaves room.
    tip.popup = 2;
    rig.put(rig.player, kMeterId, tip);
    RAWFRAME_EXPECT(rig.frame() && kBoxAt(220, 16, 60, 30));
    // No longer a popup, it is in the panel's row again, its margin inside
    // the panel's padding.
    tip.popup = 0;
    rig.put(rig.player, kMeterId, tip);
    RAWFRAME_EXPECT(rig.frame() && kBoxAt(109, 59, 60, 30));
    RAWFRAME_EXPECT(rig.ui->statistics().leftOut == 0);
}

RAWFRAME_TEST(ADisabledNodeTakesNothing) {
    Rig rig;
    // A check box and a slider in the panel, both disabled.
    rig.put(rig.player, kHudId, panel());
    ui::NodeFields box{.widthOffset = 20, .heightOffset = 20, .shrink = 0, .press = 5};
    box.check = 1;
    box.disabled = 1;
    rig.put(rig.player, kMeterId, box);
    const world::EntityHandle kRow = *rig.world.create();
    ui::NodeFields track{.widthOffset = 100, .heightOffset = 20, .shrink = 0, .order = 1, .press = 9};
    track.range = 1;
    track.rangeMaximum = 100;
    track.rangeStep = 1;
    track.rangeValue = 50;
    track.disabled = 1;
    rig.put(kRow, kRowId, track);
    RAWFRAME_EXPECT(rig.frame());
    // A press on either is the UI's but gives no press code, and changes
    // neither; navigation finds nothing to reach.
    RAWFRAME_EXPECT(rig.ui->press(115, 65) == std::optional<std::int64_t>{0} &&
                    rig.ui->press(150, 65) == std::optional<std::int64_t>{0});
    for (const float kX : {115.0F, 140.0F}) {
        rig.ui->hoverAt(std::array<float, 2>{kX, 65});
        rig.ui->pressAt(kX, 65);
        rig.ui->hoverAt(std::array<float, 2>{kX + 30, 65});
        rig.ui->release();
        RAWFRAME_EXPECT(rig.frame());
    }
    RAWFRAME_EXPECT(rig.ui->valueOf(0, 5) == 0.0F && rig.ui->valueOf(0, 9) == 50.0F);
    RAWFRAME_EXPECT(!rig.ui->enterNavigation());
    RAWFRAME_EXPECT(rig.ui->statistics().toggled == 0 && rig.ui->statistics().ranged == 0);
    // Enabled again, a click checks the box.
    box.disabled = 0;
    rig.put(rig.player, kMeterId, box);
    RAWFRAME_EXPECT(rig.frame());
    rig.ui->hoverAt(std::array<float, 2>{115, 65});
    rig.ui->pressAt(115, 65);
    rig.ui->release();
    RAWFRAME_EXPECT(rig.frame() && rig.ui->valueOf(0, 5) == 1.0F &&
                    rig.ui->press(115, 65) == std::optional<std::int64_t>{5});
}
