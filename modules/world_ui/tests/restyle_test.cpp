// The game's styles made again as an author edits them (D634): a node takes
// its class's new look, a class the sheet no longer has leaves its node
// unstyled, and one coming back styles it again.

#include "rawframe/test/test.h"
#include "rawframe/ui/styles.h"
#include "rawframe/world_ui/world_ui.h"

#include <array>
#include <cmath>
#include <cstring>
#include <memory>
#include <string>

using namespace rawframe;
using namespace rawframe::world_ui;

namespace {

constexpr auto kButtonId = schema::ComponentTypeId::fromText("8e0d2c4b-7596-41d8-b20a-3c4ef6a8b97a");

/// A sheet of one class, 0x11, whose base fill is `fill`; none with no
/// fill.
ui::StyleSheet sheet(std::string_view fill) {
    if (fill.empty()) {
        return {};
    }
    const std::string kText = R"({
  "kind": "ui.styles",
  "formatVersion": 1,
  "styles": [
    {
      "styleId": "0000000000000011",
      "name": "button",
      "base": {
        "fill": ")" + std::string{fill} +
                              R"("
      }
    }
  ]
}
)";
    auto read = ui::readStyles(kText);
    RAWFRAME_EXPECT(read.has_value());
    return read.has_value() ? *std::move(read) : ui::StyleSheet{};
}

} // namespace

RAWFRAME_TEST(EditedStylesStyleTheUiAnew) {
    schema::RegistryBuilder builder;
    builder.add(schema::ComponentDescriptor{.id = kButtonId,
                                            .name = "test.button",
                                            .size = sizeof(ui::NodeFields),
                                            .alignment = alignof(ui::NodeFields),
                                            .plainData = true});
    const std::shared_ptr<const schema::SchemaRegistry> kSchema = *builder.freeze();
    world::World world{kSchema};
    const world::EntityHandle kPlayer = *world.create();
    ui::NodeFields button{.widthOffset = 100, .heightOffset = 20, .style = 0x11};
    RAWFRAME_EXPECT(world.insertErased(kPlayer, *kSchema->find(kButtonId), &button).has_value());
    auto ui = WorldUi::create({.nodes = {kButtonId}, .parents = {std::nullopt}, .styles = sheet("#ff0000ff")});
    RAWFRAME_EXPECT(ui.has_value());
    if (!ui.has_value()) {
        return;
    }
    // The button's fill as drawn, red, green, and blue; none for no box.
    const auto kFill = [&]() -> std::array<float, 3> {
        const std::array<UiView, 1> kViews = {UiView{.world = &world, .player = kPlayer, .width = 400, .height = 300}};
        if (!(*ui)->update(kViews, 400, 300, 1).has_value()) {
            return {-1, -1, -1};
        }
        for (const ui::Box& kBox : (*ui)->drawn().boxes) {
            if (kBox.rect.width == 100 && kBox.rect.height == 20) {
                return {kBox.fill[0], kBox.fill[1], kBox.fill[2]};
            }
        }
        return {0, 0, 0};
    };
    const auto kIs = [](std::array<float, 3> fill, float red, float green, float blue) {
        return std::abs(fill[0] - red) < 0.02F && std::abs(fill[1] - green) < 0.02F && std::abs(fill[2] - blue) < 0.02F;
    };
    RAWFRAME_EXPECT(kIs(kFill(), 1, 0, 0));
    // Its class edited: blue.
    RAWFRAME_EXPECT((*ui)->restyle(sheet("#0000ffff")).has_value());
    RAWFRAME_EXPECT(kIs(kFill(), 0, 0, 1));
    // Its class gone: unstyled, no fill of its own.
    RAWFRAME_EXPECT((*ui)->restyle(sheet("")).has_value());
    RAWFRAME_EXPECT(!kIs(kFill(), 0, 0, 1) && !kIs(kFill(), 1, 0, 0));
    // Back, green.
    RAWFRAME_EXPECT((*ui)->restyle(sheet("#00ff00ff")).has_value());
    RAWFRAME_EXPECT(kIs(kFill(), 0, 1, 0) && (*ui)->statistics().restyled == 3);
}
