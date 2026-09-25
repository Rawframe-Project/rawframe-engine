// A game's Mod API surface (SPEC-0042, D177): its policy, namespace and
// version, the mods a curated game approves, and its data points; and each
// way a description gets them wrong; and a mod's description cooked with its
// scenes (D178).

#include "rawframe/test/test.h"
#include "rawframe/world_kest/cooked_mod.h"
#include "rawframe/world_kest/errors.h"
#include "rawframe/world_kest/game.h"
#include "rawframe/world_kest/mod.h"

#include <string>

using namespace rawframe;
using world_kest::ModPolicy;
using world_kest::parseGame;

namespace {

const std::string kBase = "program raid.kest\ncomponent 2b7d4e61-0c93-4a58-b1f2-7e9a3c5d8f04 raid.enemy Enemy\n"
                          "component 5e0a7c31-9d24-4b8f-a6e1-3c7b9f2d0e84 raid.rules Rules\n";

bool refused(const std::string& text) {
    const auto kParsed = parseGame(kBase + text);
    return !kParsed.has_value() && kParsed.error().domain() == world_kest::kWorldKestDomain;
}

} // namespace

RAWFRAME_TEST(AGameDeclaresWhoMayModItAndWhere) {
    const auto kGame = parseGame(kBase + "mods curated\nmodapi raid 3\napprove acme/more-enemies\n"
                                         "extension enemies data raid.enemy multi\n"
                                         "extension rules data raid.rules exclusive required\n");
    RAWFRAME_EXPECT(kGame.has_value());
    if (!kGame.has_value()) {
        return;
    }
    const world_kest::GameModApi& kMods = kGame->mods;
    RAWFRAME_EXPECT(kMods.policy == ModPolicy::Curated && kMods.modNamespace == "raid" && kMods.version == 3);
    RAWFRAME_EXPECT(kMods.approved == std::vector<std::string>{"acme/more-enemies"} && kMods.points.size() == 2);
    RAWFRAME_EXPECT(kMods.points[0].name == "enemies" && kMods.points[0].accepts == "raid.enemy" &&
                    !kMods.points[0].exclusive && !kMods.points[0].required);
    RAWFRAME_EXPECT(kMods.points[1].exclusive && kMods.points[1].required);
    // Without a mods line, a game is closed and has no surface.
    const auto kClosed = parseGame(kBase);
    RAWFRAME_EXPECT(kClosed.has_value() && kClosed->mods.policy == ModPolicy::Closed && kClosed->mods.points.empty());
}

RAWFRAME_TEST(AModApiDeclaredWrongIsRefused) {
    // Open or with points, but no Mod API; a policy twice or unknown.
    RAWFRAME_EXPECT(refused("mods open\n"));
    RAWFRAME_EXPECT(refused("extension enemies data raid.enemy multi\n"));
    RAWFRAME_EXPECT(refused("mods open\nmods closed\nmodapi raid 1\n"));
    RAWFRAME_EXPECT(refused("mods sometimes\nmodapi raid 1\n"));
    // A namespace or version outside the grammar, or a second Mod API.
    RAWFRAME_EXPECT(refused("modapi Raid 1\n") && refused("modapi raid 0\n") && refused("modapi raid x\n"));
    RAWFRAME_EXPECT(refused("modapi raid 1\nmodapi raid 2\n"));
    // Approvals: only curated, each a subject once.
    RAWFRAME_EXPECT(refused("mods open\nmodapi raid 1\napprove acme/more\n"));
    RAWFRAME_EXPECT(refused("mods curated\nmodapi raid 1\napprove acme\n"));
    RAWFRAME_EXPECT(refused("mods curated\nmodapi raid 1\napprove acme/more\napprove acme/more\n"));
    // Points: a kind that runs code, twice, an unknown component, a bad
    // occupancy or name.
    RAWFRAME_EXPECT(refused("modapi raid 1\nextension waves event raid.enemy multi\n"));
    RAWFRAME_EXPECT(refused("modapi raid 1\nextension enemies data raid.enemy multi\n"
                            "extension enemies data raid.rules multi\n"));
    RAWFRAME_EXPECT(refused("modapi raid 1\nextension enemies data raid.boss multi\n"));
    RAWFRAME_EXPECT(refused("modapi raid 1\nextension enemies data raid.enemy some\n"));
    RAWFRAME_EXPECT(refused("modapi raid 1\nextension Enemies data raid.enemy multi\n"));
    RAWFRAME_EXPECT(refused("modapi raid 1\nextension enemies data raid.enemy multi optional\n"));
}

RAWFRAME_TEST(AModDescribesItsTargetRangeAndContributions) {
    const auto kMod = world_kest::parseMod("# More enemies.\ntarget acme/raid\nmodapi >=2 <4\n"
                                           "contribute enemies enemies.scene\ncontribute enemies bosses.scene\n");
    RAWFRAME_EXPECT(kMod.has_value());
    if (!kMod.has_value()) {
        return;
    }
    RAWFRAME_EXPECT(kMod->target == "acme/raid" && kMod->modApi.size() == 2 && kMod->contributions.size() == 2 &&
                    kMod->contributions[1].scene == "bosses.scene");
    RAWFRAME_EXPECT(!world_kest::accepts(kMod->modApi, 1) && world_kest::accepts(kMod->modApi, 2) &&
                    world_kest::accepts(kMod->modApi, 3) && !world_kest::accepts(kMod->modApi, 4));
    const auto kPinned = world_kest::parseMod("target acme/raid\nmodapi 3\n");
    RAWFRAME_EXPECT(kPinned.has_value() && world_kest::accepts(kPinned->modApi, 3) &&
                    !world_kest::accepts(kPinned->modApi, 2));
    // Refused: no target or range, twice, outside the grammar, a range
    // nothing satisfies, a contribution twice, an unknown line.
    for (const std::string_view kText :
         {"modapi 1\n",
          "target acme/raid\n",
          "target acme/raid\ntarget acme/raid\nmodapi 1\n",
          "target acme\nmodapi 1\n",
          "target acme/raid\nmodapi ~1\n",
          "target acme/raid\nmodapi 0\n",
          "target acme/raid\nmodapi >3 <3\n",
          "target acme/raid\nmodapi >=1 <5 <6\n",
          "target acme/raid\nmodapi 1\ncontribute enemies a.scene\ncontribute enemies a.scene\n",
          "target acme/raid\nmodapi 1\nreplace rules\n"}) {
        RAWFRAME_EXPECT(!world_kest::parseMod(kText).has_value());
    }
}

RAWFRAME_TEST(ACookedModReadsAsWritten) {
    const base::Bits128 kWave = base::parseBits128Hex("000000000000000000000000000000b2").value;
    const base::Bits128 kMore = base::parseBits128Hex("000000000000000000000000000000b3").value;
    const world_kest::CookedMod kMod{
        .text = "target acme/raid\nmodapi 3\n",
        .scenes = {{.path = "wave.scene", .scene = kWave}, {.path = "more.scene", .scene = kMore}}};
    const auto kWritten = world_kest::writeCookedMod(kMod);
    RAWFRAME_EXPECT(kWritten.has_value());
    if (!kWritten.has_value()) {
        return;
    }
    // Scenes in path order, and the same bytes again.
    const auto kRead = world_kest::readCookedMod(*kWritten);
    RAWFRAME_EXPECT(kRead.has_value() && kRead->text == kMod.text && kRead->scenes.size() == 2 &&
                    kRead->scenes[0].path == "more.scene" && kRead->scene("wave.scene") != nullptr &&
                    kRead->scene("wave.scene")->scene == kWave);
    RAWFRAME_EXPECT(kRead.has_value() && world_kest::writeCookedMod(*kRead).value_or("") == *kWritten);
    // A scene named twice or of no identity is not written; a record of
    // another kind, a member too many, or a scene out of order is not read.
    RAWFRAME_EXPECT(
        !world_kest::writeCookedMod({.scenes = {{.path = "a", .scene = kWave}, {.path = "a", .scene = kMore}}})
             .has_value());
    RAWFRAME_EXPECT(!world_kest::writeCookedMod({.scenes = {{.path = "a", .scene = {}}}}).has_value());
    std::string other = *kWritten;
    other.replace(other.find("mod.description"), 15, "game.descriptio");
    RAWFRAME_EXPECT(!world_kest::readCookedMod(other).has_value());
    std::string swapped = *kWritten;
    swapped.replace(swapped.find("more.scene"), 10, "zzzz.scene");
    RAWFRAME_EXPECT(!world_kest::readCookedMod(swapped).has_value());
    RAWFRAME_EXPECT(!world_kest::readCookedMod("{}").has_value());
}
