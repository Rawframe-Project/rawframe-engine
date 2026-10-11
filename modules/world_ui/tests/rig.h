#pragma once

// What the UI's tests share (D376): a registry of a HUD, a meter, a row,
// and typed words; a World with a player whose view the UI lays out; and
// the HUD's panel and meter.

#include "rawframe/test/test.h"
#include "rawframe/world_ui/world_ui.h"

#include <array>
#include <cstring>
#include <memory>
#include <optional>
#include <utility>

namespace rawframe::world_ui::test_rig {

inline constexpr auto kHudId = schema::ComponentTypeId::fromText("1d3c5b7a-0e2f-4a61-8b93-c5d7e9f1a203");
inline constexpr auto kMeterId = schema::ComponentTypeId::fromText("2e4d6c8b-1f30-4b72-9ca4-d6e8f0a2b314");
inline constexpr auto kRowId = schema::ComponentTypeId::fromText("3f5e7d9c-2041-4c83-adb5-e7f9a1b3c425");
inline constexpr auto kWordsId = schema::ComponentTypeId::fromText("4a6f8e0d-3152-4d94-bec6-f80ab2c4d536");

inline std::shared_ptr<const schema::SchemaRegistry> registry() {
    schema::RegistryBuilder builder;
    for (const auto& [kId, kName] :
         {std::pair{kHudId, "test.hud"}, std::pair{kMeterId, "test.meter"}, std::pair{kRowId, "test.row"}}) {
        builder.add(schema::ComponentDescriptor{.id = kId,
                                                .name = kName,
                                                .size = sizeof(ui::NodeFields),
                                                .alignment = alignof(ui::NodeFields),
                                                .plainData = true});
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

inline ui::NodeFields panel() {
    return ui::NodeFields{
        .widthOffset = 300, .heightOffset = 40, .gap = 10, .shrink = 1, .padding = 5, .fill = 0x202020FF, .radius = 6};
}

inline ui::NodeFields meter(float width) {
    return ui::NodeFields{.widthOffset = width, .shrink = 1, .fill = 0x40C040FF};
}

inline bool at(const ui::Box& box, float x, float y, float width, float height) {
    return box.rect.x == x && box.rect.y == y && box.rect.width == width && box.rect.height == height;
}

} // namespace rawframe::world_ui::test_rig
