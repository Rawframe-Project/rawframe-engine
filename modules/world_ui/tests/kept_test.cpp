// The player's settings kept between runs: a kept slider and check
// box start as kept, the player's changes to them are noted and counted,
// a node not kept is never noted, and every local player's starts as the
// first's; the `ui.kept` document reads back what is written and is
// refused where it is wrong; and the file is replaced whole.

#include "../src/keeper.h"
#include "rawframe/test/scratch.h"
#include "rawframe/test/test.h"
#include "rawframe/world_ui/world_ui.h"

#include <array>
#include <cstring>
#include <filesystem>
#include <limits>
#include <memory>
#include <optional>
#include <string>

using namespace rawframe;
using namespace rawframe::world_ui;

namespace {

constexpr auto kTrackId = schema::ComponentTypeId::fromText("5b7a9f1e-4263-4ea5-8fd7-091bc3d5e647");
constexpr auto kBoxId = schema::ComponentTypeId::fromText("6c8b0a2f-5374-4fb6-90e8-1a2cd4e6f758");
constexpr auto kPlainId = schema::ComponentTypeId::fromText("7d9c1b3a-6485-40c7-a1f9-2b3de5f7a869");

std::shared_ptr<const schema::SchemaRegistry> registry() {
    schema::RegistryBuilder builder;
    for (const auto& [kId, kName] :
         {std::pair{kTrackId, "test.track"}, std::pair{kBoxId, "test.box"}, std::pair{kPlainId, "test.plain"}}) {
        builder.add(schema::ComponentDescriptor{.id = kId,
                                                .name = kName,
                                                .size = sizeof(ui::NodeFields),
                                                .alignment = alignof(ui::NodeFields),
                                                .plainData = true});
    }
    return *builder.freeze();
}

/// Two local players, each a World of their own: a slider 200 wide from
/// 0 to 100 at 50, kept, at their view's top left; a check box under it,
/// kept; and a check box beside them that is not.
struct Rig {
    explicit Rig(std::vector<std::optional<float>> kept)
        : ui(*WorldUi::create({.nodes = {kTrackId, kBoxId, kPlainId},
                               .parents = {std::nullopt, std::nullopt, std::nullopt},
                               .kept = std::move(kept)})) {
        for (std::size_t at = 0; at < worlds.size(); ++at) {
            players[at] = *worlds[at].create();
            ui::NodeFields track{.widthOffset = 200, .heightOffset = 20, .shrink = 0, .press = 9};
            track.range = 1;
            track.rangeMaximum = 100;
            track.rangeStep = 1;
            track.rangeValue = 50;
            track.kept = 1;
            put(at, kTrackId, track);
            ui::NodeFields box{.widthOffset = 20, .heightOffset = 20, .shrink = 0, .press = 5};
            box.check = 1;
            box.kept = 1;
            put(at, kBoxId, box);
            box.press = 6;
            box.kept = 0;
            put(at, kPlainId, box);
        }
    }

    void put(std::size_t player, schema::ComponentTypeId id, ui::NodeFields value) {
        const schema::ComponentRuntimeId kComponent = *schema->find(id);
        if (void* held = worlds[player].getErased(players[player], kComponent)) {
            std::memcpy(held, &value, sizeof value);
            return;
        }
        RAWFRAME_EXPECT(worlds[player].insertErased(players[player], kComponent, &value).has_value());
    }

    /// One frame, the players' views side by side, each 400 by 300.
    bool frame() {
        const std::array<UiView, 2> kViews = {
            UiView{.world = worlds.data(), .player = players[0], .width = 400, .height = 300},
            UiView{.world = &worlds[1], .player = players[1], .x = 400, .width = 400, .height = 300}};
        return ui->update(kViews, 800, 300, 1).has_value();
    }

    /// The first player's view clicked at `x`, `y`.
    bool click(float x, float y) {
        ui->hoverAt(std::array<float, 2>{x, y});
        ui->pressAt(x, y);
        ui->release();
        return frame();
    }

    std::shared_ptr<const schema::SchemaRegistry> schema = registry();
    std::array<world::World, 2> worlds{world::World{schema}, world::World{schema}};
    std::array<world::EntityHandle, 2> players;
    std::unique_ptr<WorldUi> ui;
};

} // namespace

RAWFRAME_TEST(KeptNodesStartAsKeptAndTheirChangesAreNoted) {
    // Nothing kept yet: each starts as its node says, which is no change.
    {
        Rig rig{{}};
        RAWFRAME_EXPECT(rig.frame());
        RAWFRAME_EXPECT(rig.ui->valueOf(0, 9) == 50.0F && rig.ui->valueOf(0, 5) == 0.0F);
        RAWFRAME_EXPECT(rig.ui->kept()[0] == 50.0F && rig.ui->kept()[1] == 0.0F && !rig.ui->kept()[2].has_value());
        RAWFRAME_EXPECT(rig.ui->statistics().kept == 0 && rig.ui->statistics().keptGiven == 0);
        // The box checked and the slider paged back are noted; the box not
        // kept is not.
        RAWFRAME_EXPECT(rig.click(10, 30) && rig.click(10, 10));
        RAWFRAME_EXPECT(rig.ui->kept()[0] == 40.0F && rig.ui->kept()[1] == 1.0F && rig.ui->statistics().kept == 2);
        RAWFRAME_EXPECT(rig.click(10, 50) && rig.ui->valueOf(0, 6) == 1.0F);
        RAWFRAME_EXPECT(!rig.ui->kept()[2].has_value() && rig.ui->statistics().kept == 2);
        // The second player's view is not the first's: its slider paged is
        // not noted.
        RAWFRAME_EXPECT(rig.click(410, 10) && rig.ui->valueOf(1, 9) == 40.0F);
        RAWFRAME_EXPECT(rig.ui->kept()[0] == 40.0F && rig.ui->statistics().kept == 2);
    }
    // Kept from before: both players' start as kept, not as their nodes
    // say, and a value past the range is held inside it.
    Rig rig{{40.0F, 1.0F, 1.0F}};
    RAWFRAME_EXPECT(rig.frame());
    RAWFRAME_EXPECT(rig.ui->valueOf(0, 9) == 40.0F && rig.ui->valueOf(0, 5) == 1.0F);
    RAWFRAME_EXPECT(rig.ui->valueOf(1, 9) == 40.0F && rig.ui->valueOf(1, 5) == 1.0F);
    RAWFRAME_EXPECT(rig.ui->valueOf(0, 6) == 0.0F && rig.ui->statistics().keptGiven == 4);
    RAWFRAME_EXPECT(rig.ui->statistics().kept == 0);
    Rig past{{500.0F, std::nullopt, std::nullopt}};
    RAWFRAME_EXPECT(past.frame() && past.ui->valueOf(0, 9) == 100.0F && past.ui->kept()[0] == 100.0F);
    // Kept values for other nodes than the game's are refused.
    RAWFRAME_EXPECT(!WorldUi::create({.nodes = {kTrackId}, .parents = {std::nullopt}, .kept = {1.0F, 2.0F}}));
}

RAWFRAME_TEST(TheKeptDocumentReadsBackWhatIsWritten) {
    const KeptValues kValues{{"tycoon.top", 1.0F}, {"tycoon.zoom", 62.5F}};
    const std::string kText = writeKept(kValues);
    RAWFRAME_EXPECT(kText ==
                    "{\n  \"kind\": \"ui.kept\",\n  \"formatVersion\": 1,\n  \"values\": {\n    \"tycoon.top\": "
                    "1,\n    \"tycoon.zoom\": 62.5\n  }\n}\n");
    RAWFRAME_EXPECT(readKept(kText) == kValues);
    RAWFRAME_EXPECT(readKept(writeKept({})) == KeptValues{});
    // Its whitespace as a player may leave it.
    const KeptValues kNegative{{"a", -3.0F}};
    RAWFRAME_EXPECT(readKept(R"({"kind":"ui.kept","formatVersion":1,"values":{"a":-3}})") == kNegative);
    // Refused: another version or kind, a value not a number or not finite
    // as a float, an unknown member, a value under no name.
    RAWFRAME_EXPECT(!readKept(R"({"kind":"ui.kept","formatVersion":2,"values":{}})"));
    RAWFRAME_EXPECT(!readKept(R"({"kind":"ui.styles","formatVersion":1,"values":{}})"));
    RAWFRAME_EXPECT(!readKept(R"({"kind":"ui.kept","formatVersion":1,"values":{"a":true}})"));
    RAWFRAME_EXPECT(!readKept(R"({"kind":"ui.kept","formatVersion":1,"values":{"a":1e300}})"));
    RAWFRAME_EXPECT(!readKept(R"({"kind":"ui.kept","formatVersion":1,"values":{},"more":1})"));
    RAWFRAME_EXPECT(!readKept(R"({"kind":"ui.kept","formatVersion":1,"values":{"":1}})"));
    RAWFRAME_EXPECT(!readKept("not a document"));
}

RAWFRAME_TEST(AKeeperWritesTheFileWholeAndReadsItBack) {
    const std::filesystem::path kDirectory = test::scratchDirectory("kept");
    const std::filesystem::path kPath = kDirectory / "settings" / "kept.json";
    std::filesystem::remove_all(kDirectory);
    {
        Keeper keeper{kPath};
        // No file yet is nothing kept.
        RAWFRAME_EXPECT(keeper.read() == KeptValues{});
        // Written here, as no executor takes it, its directory made.
        keeper.keep({{"test.track", 40.0F}}, nullptr, {});
        RAWFRAME_EXPECT(!keeper.writing() && keeper.written() == 1 && keeper.failed() == 0);
        RAWFRAME_EXPECT(keeper.read() == (KeptValues{{"test.track", 40.0F}}));
        keeper.keep({{"test.track", 60.0F}, {"test.box", 1.0F}}, nullptr, {});
        RAWFRAME_EXPECT(keeper.read() == KeptValues({{"test.box", 1.0F}, {"test.track", 60.0F}}));
        RAWFRAME_EXPECT(!std::filesystem::exists(kPath.string() + ".partial"));
    }
    // A file that does not read is refused, and written over.
    {
        std::FILE* file = std::fopen(kPath.string().c_str(), "wb");
        std::fputs("{\"kind\":", file);
        std::fclose(file);
        Keeper keeper{kPath};
        RAWFRAME_EXPECT(!keeper.read().has_value());
        keeper.keep({{"test.box", 0.0F}}, nullptr, {});
        RAWFRAME_EXPECT(keeper.read() == (KeptValues{{"test.box", 0.0F}}));
    }
    std::filesystem::remove_all(kDirectory);
}
