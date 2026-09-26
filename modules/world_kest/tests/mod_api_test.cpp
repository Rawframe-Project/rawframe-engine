// A game's Mod API surface (SPEC-0042, D177): its policy, namespace and
// version, the mods a curated game approves, and its data points; and each
// way a description gets them wrong; and a mod's description cooked with its
// scenes (D178); and SPEC-0042's validation of a Composition's mods (D179).

#include "rawframe/test/mutations.h"
#include "rawframe/test/test.h"
#include "rawframe/world_kest/cooked_mod.h"
#include "rawframe/world_kest/errors.h"
#include "rawframe/world_kest/game.h"
#include "rawframe/world_kest/game_files.h"
#include "rawframe/world_kest/mod.h"

#include <array>
#include <cstdint>
#include <cstdio>
#include <span>
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
        .scenes = {{.path = "wave.scene", .scene = kWave}, {.path = "more.scene", .scene = kMore}},
        .programs = {{.path = "horde.kest", .sources = kMore, .entry = "horde.kest"}}};
    const auto kWritten = world_kest::writeCookedMod(kMod);
    RAWFRAME_EXPECT(kWritten.has_value());
    if (!kWritten.has_value()) {
        return;
    }
    // Scenes in path order, and the same bytes again.
    const auto kRead = world_kest::readCookedMod(*kWritten);
    RAWFRAME_EXPECT(kRead.has_value() && kRead->text == kMod.text && kRead->scenes.size() == 2 &&
                    kRead->scenes[0].path == "more.scene" && kRead->scene("wave.scene") != nullptr &&
                    kRead->scene("wave.scene")->scene == kWave && kRead->programs.size() == 1 &&
                    kRead->programs[0].sources == kMore && kRead->programs[0].entry == "horde.kest");
    RAWFRAME_EXPECT(kRead.has_value() && world_kest::writeCookedMod(*kRead).value_or("") == *kWritten);
    // A scene named twice or of no identity is not written; a record of
    // another kind, a member too many, or a scene out of order is not read.
    RAWFRAME_EXPECT(
        !world_kest::writeCookedMod({.scenes = {{.path = "a", .scene = kWave}, {.path = "a", .scene = kMore}}})
             .has_value());
    RAWFRAME_EXPECT(!world_kest::writeCookedMod({.scenes = {{.path = "a", .scene = {}}}}).has_value());
    world_kest::CookedMod twoPrograms = kMod;
    twoPrograms.programs.push_back({.path = "other.kest", .sources = kWave, .entry = "other.kest"});
    RAWFRAME_EXPECT(!world_kest::writeCookedMod(twoPrograms).has_value());
    std::string other = *kWritten;
    other.replace(other.find("mod.description"), 15, "game.descriptio");
    RAWFRAME_EXPECT(!world_kest::readCookedMod(other).has_value());
    std::string swapped = *kWritten;
    swapped.replace(swapped.find("more.scene"), 10, "zzzz.scene");
    RAWFRAME_EXPECT(!world_kest::readCookedMod(swapped).has_value());
    RAWFRAME_EXPECT(!world_kest::readCookedMod("{}").has_value());
}

namespace {

world_kest::ComposedMod modOf(std::string subject, std::string_view text) {
    return world_kest::ComposedMod{.subject = std::move(subject), .description = *world_kest::parseMod(text)};
}

/// Whether `mods` are refused for `game` as ModRefused, naming `part` in
/// its context.
bool refusedWith(const world_kest::GameDescription& game,
                 const std::vector<world_kest::ComposedMod>& mods,
                 std::string_view part,
                 const std::vector<std::string>& held = {"raid.rules"}) {
    const auto kChecked = world_kest::checkMods(game, "acme/raid", mods, held);
    if (kChecked.has_value() || kChecked.error().code() != code(world_kest::WorldKestError::ModRefused)) {
        return false;
    }
    for (const result::ContextField& field : kChecked.error().context()) {
        if (field.value.find(part) != std::string_view::npos) {
            return true;
        }
    }
    return false;
}

} // namespace

RAWFRAME_TEST(AGameTakesOnlyTheModsItsPolicyAllows) {
    const auto kGame = parseGame(kBase + "mods curated\nmodapi raid 3\napprove fan/horde\napprove fan/bosses\n"
                                         "extension enemies data raid.enemy multi\n"
                                         "extension rules data raid.rules exclusive required\n");
    RAWFRAME_EXPECT(kGame.has_value());
    if (!kGame.has_value()) {
        return;
    }
    const std::string kHorde = "target acme/raid\nmodapi >=2 <4\ncontribute enemies wave.scene\n";
    const std::vector<world_kest::ComposedMod> kTaken = {
        modOf("fan/bosses", "target acme/raid\nmodapi 3\ncontribute enemies bosses.scene\n"),
        modOf("fan/horde", kHorde)};
    // Approved, targeting it, in range, on its points: taken, and none too.
    const std::vector<std::string> kGameHolds = {"raid.rules"};
    RAWFRAME_EXPECT(world_kest::checkMods(*kGame, "acme/raid", kTaken, kGameHolds).has_value());
    RAWFRAME_EXPECT(world_kest::checkMods(*kGame, "acme/raid", {}, kGameHolds).has_value());
    // Not approved; another game; out of range; an unknown point.
    RAWFRAME_EXPECT(refusedWith(*kGame, {modOf("fan/other", kHorde)}, "fan/other"));
    RAWFRAME_EXPECT(refusedWith(*kGame, {modOf("fan/horde", "target acme/siege\nmodapi 3\n")}, "acme/siege"));
    RAWFRAME_EXPECT(refusedWith(*kGame, {modOf("fan/horde", "target acme/raid\nmodapi >=4\n")}, "fan/horde"));
    RAWFRAME_EXPECT(
        refusedWith(*kGame, {modOf("fan/horde", "target acme/raid\nmodapi 3\ncontribute bosses b.scene\n")}, "bosses"));
    // Two claimants of the exclusive point, each named, whatever order.
    const std::vector<world_kest::ComposedMod> kClaimed = {
        modOf("fan/bosses", "target acme/raid\nmodapi 3\ncontribute rules hard.scene\n"),
        modOf("fan/horde", "target acme/raid\nmodapi 3\ncontribute rules easy.scene\n")};
    RAWFRAME_EXPECT(refusedWith(*kGame, kClaimed, "fan/bosses:hard.scene fan/horde:easy.scene"));
    // A required point the game's own scenes do not fill needs a mod.
    RAWFRAME_EXPECT(refusedWith(*kGame, {}, "rules", {}));
    RAWFRAME_EXPECT(world_kest::checkMods(*kGame, "acme/raid", std::span{kClaimed}.first(1), {}).has_value());
    // A closed game takes none.
    RAWFRAME_EXPECT(refusedWith(*parseGame(kBase), {modOf("fan/horde", kHorde)}, "fan/horde"));
}

RAWFRAME_TEST(AGameFillsItsOwnRequiredPointsOrIsNotRead) {
    const std::string kGame = kBase + "scene rules.scene\nmods open\nmodapi raid 1\n"
                                      "extension rules data raid.rules exclusive required\n";
    // Scenes in their canonical form, as hall.scene is written.
    const std::string kEmpty = "{\n  \"kind\": \"rawframe.scene\",\n  \"formatVersion\": 1,\n  \"schema\": {},\n"
                               "  \"entities\": []\n}\n";
    const std::string kRules = "{\n  \"kind\": \"rawframe.scene\",\n  \"formatVersion\": 1,\n  \"schema\": {\n"
                               "    \"raid.rules\": \"b1e3cd3c575b7a33\"\n  },\n  \"entities\": [\n    {\n"
                               "      \"id\": \"909c0889-a695-4893-a218-b36b9eb97339\",\n      \"components\": {\n"
                               "        \"raid.rules\": {}\n      }\n    }\n  ]\n}\n";
    const auto kRead = [&kGame](const std::string& scene) {
        return world_kest::GameFiles::fromHeld(
            "raid.game", {{"raid.game", kGame}, {"rules.scene", scene}, {"raid.kest", ""}}, nullptr);
    };
    const auto kUnfilled = kRead(kEmpty);
    RAWFRAME_EXPECT(!kUnfilled.has_value() && kUnfilled.error().code() == code(world_kest::WorldKestError::ModRefused));
    const auto kFilled = kRead(kRules);
    RAWFRAME_EXPECT(kFilled.has_value() && kFilled->modScenes().empty());
}

RAWFRAME_TEST(AnEventPointTakesHandlersAfterASystem) {
    const std::string kSystems = "system raid.fight simulation fight write raid.enemy\n";
    const auto kGame = parseGame(kBase + kSystems +
                                 "mods open\nmodapi raid 1\nextension spawned data raid.enemy multi\n"
                                 "extension hits event raid.enemy after raid.fight write raid.rules multi\n"
                                 "extension rule event raid.rules after raid.fight exclusive\n");
    RAWFRAME_EXPECT(kGame.has_value());
    if (!kGame.has_value()) {
        return;
    }
    using Kind = world_kest::GameExtensionPoint::Kind;
    const auto& kPoints = kGame->mods.points;
    RAWFRAME_EXPECT(kPoints.size() == 3 && kPoints[0].kind == Kind::Data && kPoints[1].kind == Kind::Event &&
                    kPoints[1].accepts == "raid.enemy" && kPoints[1].after == "raid.fight" &&
                    kPoints[1].writes == std::vector<std::string>{"raid.rules"} && kPoints[2].writes.empty() &&
                    kPoints[2].exclusive);
    // After no system or one it does not declare, writing what it reads or
    // twice, an unknown component, another clause, and kinds not built are
    // refused.
    for (const std::string_view kLine :
         {"extension hits event raid.enemy multi\n",
          "extension hits event raid.enemy after raid.rest multi\n",
          "extension hits event raid.enemy after raid.fight write raid.enemy multi\n",
          "extension hits event raid.enemy after raid.fight write raid.rules write raid.rules multi\n",
          "extension hits event raid.enemy after raid.fight write raid.boss multi\n",
          "extension hits event raid.enemy after raid.fight read raid.rules multi\n",
          "extension hits service raid.enemy multi\n",
          "extension\n"}) {
        RAWFRAME_EXPECT(refused(kSystems + "modapi raid 1\n" + std::string{kLine}));
    }

    // A mod handles an event with a function of its one program.
    const auto kMod =
        world_kest::parseMod("target acme/raid\nmodapi 1\nprogram horde.kest\nhandle hits bounty\nhandle hits tally\n");
    RAWFRAME_EXPECT(kMod.has_value() && kMod->program == "horde.kest" && kMod->handlers.size() == 2 &&
                    kMod->handlers[1].function == "tally");
    for (const std::string_view kText :
         {"target acme/raid\nmodapi 1\nhandle hits bounty\n",
          "target acme/raid\nmodapi 1\nprogram horde.kest\n",
          "target acme/raid\nmodapi 1\nprogram a.kest\nprogram b.kest\nhandle hits bounty\n",
          "target acme/raid\nmodapi 1\nprogram a.kest\nhandle hits bounty\nhandle hits bounty\n",
          "target acme/raid\nmodapi 1\nprogram a.kest\nhandle hits\n"}) {
        RAWFRAME_EXPECT(!world_kest::parseMod(kText).has_value());
    }

    // Handlers go to event points and values to data points; an exclusive
    // event with two handlers names both.
    const std::vector<world_kest::ComposedMod> kHandled = {
        modOf("fan/horde", "target acme/raid\nmodapi 1\nprogram horde.kest\nhandle hits bounty\n")};
    RAWFRAME_EXPECT(world_kest::checkMods(*kGame, "acme/raid", kHandled, {}).has_value());
    RAWFRAME_EXPECT(
        refusedWith(*kGame,
                    {modOf("fan/horde", "target acme/raid\nmodapi 1\nprogram h.kest\nhandle spawned bounty\n")},
                    "spawned"));
    RAWFRAME_EXPECT(
        refusedWith(*kGame, {modOf("fan/horde", "target acme/raid\nmodapi 1\ncontribute hits wave.scene\n")}, "hits"));
    RAWFRAME_EXPECT(refusedWith(*kGame,
                                {modOf("fan/horde", "target acme/raid\nmodapi 1\nprogram h.kest\nhandle rule a\n"),
                                 modOf("fan/more", "target acme/raid\nmodapi 1\nprogram m.kest\nhandle rule b\n")},
                                "fan/horde:a fan/more:b"));
}

RAWFRAME_TEST(HostileModDescriptionsAreReadOrRefusedNeverHalfRead) {
    // A mod description is an untrusted author's text (SPEC-0042, D183):
    // whatever parses keeps every rule a description has, and a game checks
    // it without harm.
    const std::string kSeed = "# A mod.\ntarget acme/raid\nmodapi >=2 <4\ncontribute spawned wave.scene\n"
                              "program horde.kest\nhandle hits bounty\n";
    constexpr std::string_view kInserted = " \t\r\n#<>=/.-_aZ9\xC3\x80";
    constexpr std::array<std::string_view, 8> kLines = {"handle hits tally\n",
                                                        "program other.kest\n",
                                                        "modapi <3\n",
                                                        "target acme/siege\n",
                                                        "contribute rule r.scene\n",
                                                        "handle rule a\n",
                                                        "contribute spawned wave.scene\n",
                                                        "replace rules\n"};
    const auto kGame = parseGame(kBase + "system raid.fight simulation fight write raid.enemy\nmods open\n"
                                         "modapi raid 3\nextension spawned data raid.enemy multi\n"
                                         "extension hits event raid.enemy after raid.fight write raid.rules multi\n"
                                         "extension rule event raid.rules after raid.fight exclusive required\n");
    RAWFRAME_EXPECT(kGame.has_value());
    if (!kGame.has_value()) {
        return;
    }
    test::Mutations mutations;
    int accepted = 0;
    int taken = 0;
    for (int round = 0; round < 20'000; ++round) {
        const std::string kText = mutations.mutate(kSeed, kInserted, kLines);
        const auto kMod = world_kest::parseMod(kText);
        if (!kMod.has_value()) {
            RAWFRAME_EXPECT(kMod.error().domain() == world_kest::kWorldKestDomain);
            continue;
        }
        ++accepted;
        // Some version satisfies the range: one beside a bound, if any.
        bool satisfiable = false;
        for (const world_kest::ModApiBound& bound : kMod->modApi) {
            for (const std::uint32_t kVersion : {bound.version - 1, bound.version, bound.version + 1}) {
                satisfiable = satisfiable || (kVersion != 0 && world_kest::accepts(kMod->modApi, kVersion));
            }
        }
        RAWFRAME_EXPECT(!kMod->target.empty() && !kMod->modApi.empty() && kMod->modApi.size() <= 2 && satisfiable &&
                        kMod->handlers.empty() == kMod->program.empty());
        const std::array<world_kest::ComposedMod, 1> kMods = {
            world_kest::ComposedMod{.subject = "fan/horde", .description = *kMod}};
        const auto kChecked = world_kest::checkMods(*kGame, "acme/raid", kMods, {});
        RAWFRAME_EXPECT(kChecked.has_value() ||
                        kChecked.error().code() == code(world_kest::WorldKestError::ModRefused));
        taken += kChecked.has_value() ? 1 : 0;
    }
    // Some mutations land in names and ranges and stay descriptions; some
    // of those the game still takes.
    std::printf("  %d of 20000 read, %d taken\n", accepted, taken);
    RAWFRAME_EXPECT(accepted > 500 && taken > 50);
}

RAWFRAME_TEST(HostileCookedModsReadOnlyAsTheyWrite) {
    // A cooked mod comes from a Build its publisher signed, not the game's:
    // whatever the reader accepts writes back to the very same bytes.
    const world_kest::CookedMod kMod{
        .text = "target acme/raid\nmodapi 3\nprogram horde.kest\nhandle hits bounty\n",
        .scenes = {{.path = "wave.scene", .scene = base::parseBits128Hex("000000000000000000000000000000b2").value}},
        .programs = {{.path = "horde.kest",
                      .sources = base::parseBits128Hex("000000000000000000000000000000b3").value,
                      .entry = "horde.kest"}}};
    const auto kSeed = world_kest::writeCookedMod(kMod);
    RAWFRAME_EXPECT(kSeed.has_value());
    if (!kSeed.has_value()) {
        return;
    }
    constexpr std::string_view kInserted = "{}[]\",:\\0123456789abcdef";
    test::Mutations mutations;
    int accepted = 0;
    for (int round = 0; round < 20'000; ++round) {
        const std::string kText = mutations.mutate(*kSeed, kInserted);
        const auto kRead = world_kest::readCookedMod(kText);
        if (!kRead.has_value()) {
            continue;
        }
        ++accepted;
        RAWFRAME_EXPECT(world_kest::writeCookedMod(*kRead).value_or("") == kText);
    }
    std::printf("  %d of 20000 read\n", accepted);
    RAWFRAME_EXPECT(accepted > 0);
}
