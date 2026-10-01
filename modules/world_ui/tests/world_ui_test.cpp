// The UI as World data (D376): node components laid out in their player's
// view, nested by their components' parents on the same entity or on the
// player, siblings in order; a node changes, goes, and is left out as its
// component does; nothing unchanged is laid out again; a new World starts
// afresh; and values the tree cannot take, a gradient of no kind among
// them, are left out and counted.

#include "rawframe/test/test.h"
#include "rawframe/world_ui/errors.h"
#include "rawframe/world_ui/world_ui.h"

#include <array>
#include <cstring>
#include <memory>
#include <utility>

using namespace rawframe;
using namespace rawframe::world_ui;

namespace {

constexpr auto kHudId = schema::ComponentTypeId::fromText("1d3c5b7a-0e2f-4a61-8b93-c5d7e9f1a203");
constexpr auto kMeterId = schema::ComponentTypeId::fromText("2e4d6c8b-1f30-4b72-9ca4-d6e8f0a2b314");
constexpr auto kRowId = schema::ComponentTypeId::fromText("3f5e7d9c-2041-4c83-adb5-e7f9a1b3c425");

std::shared_ptr<const schema::SchemaRegistry> registry() {
    schema::RegistryBuilder builder;
    for (const auto& [kId, kName] :
         {std::pair{kHudId, "test.hud"}, std::pair{kMeterId, "test.meter"}, std::pair{kRowId, "test.row"}}) {
        builder.add(schema::ComponentDescriptor{
            .id = kId, .name = kName, .size = sizeof(Node), .alignment = alignof(Node), .plainData = true});
    }
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

    void put(world::EntityHandle entity, schema::ComponentTypeId id, Node node) {
        const schema::ComponentRuntimeId kComponent = *schema->find(id);
        if (void* held = world.getErased(entity, kComponent)) {
            std::memcpy(held, &node, sizeof(Node));
            return;
        }
        RAWFRAME_EXPECT(world.insertErased(entity, kComponent, &node).has_value());
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
