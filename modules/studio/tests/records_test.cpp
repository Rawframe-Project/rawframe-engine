// Studio's records (D435, D439): a component found by what an author types,
// a field's text made a value of its kind or refused, an outcome read, and
// a minted identity in the form the session reads.

#include "../src/play.h"
#include "../src/records.h"
#include "rawframe/test/test.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <set>
#include <string>
#include <thread>

using namespace rawframe::studio;
namespace document = rawframe::document;

namespace {

Catalog sample() {
    Catalog catalog;
    catalog.operations = {"scene.add_component", "scene.set_field"};
    catalog.components = {{"a", "rawframe.physics2d.body"},
                          {"b", "rawframe.physics2d.pose"},
                          {"c", "runners.tile"},
                          {"d", "runners.target"},
                          {"e", "game.tile"}};
    return catalog;
}

} // namespace

RAWFRAME_TEST(AComponentIsFoundByItsNameItsLastPartOrAStartOnlyItHas) {
    const Catalog kCatalog = sample();
    std::string why;
    RAWFRAME_EXPECT(componentNamed(kCatalog, "runners.tile", why)->id == "c");
    RAWFRAME_EXPECT(componentNamed(kCatalog, "body", why)->id == "a");
    RAWFRAME_EXPECT(componentNamed(kCatalog, "runners.ta", why)->id == "d");
    // Two whose last part is `tile`; the whole name tells them apart.
    RAWFRAME_EXPECT(!componentNamed(kCatalog, "tile", why).has_value());
    RAWFRAME_EXPECT(why == "2 components match tile");
    RAWFRAME_EXPECT(componentNamed(kCatalog, "game.tile", why)->id == "e");
    RAWFRAME_EXPECT(!componentNamed(kCatalog, "rawframe", why).has_value());
    RAWFRAME_EXPECT(!componentNamed(kCatalog, "wheel", why).has_value());
    RAWFRAME_EXPECT(why == "no component matches wheel");
    RAWFRAME_EXPECT(!componentNamed(kCatalog, "", why).has_value());
    RAWFRAME_EXPECT(kCatalog.offers("scene.set_field") && !kCatalog.offers("scene.destroy_entity"));
}

RAWFRAME_TEST(AReferenceNamesOneEntityByNameOrIdentity) {
    const std::vector<std::string> kIds = {"c5400cf8-4b07", "c5401111-0000", "b4560e55-e4e2", "9a000000-0000"};
    const std::vector<std::string> kNames = {"ground", "ground left", "gate", ""};
    std::string why;
    // A whole name, even where it starts another.
    RAWFRAME_EXPECT(entityNamed(kIds, kNames, "ground", why) == "c5400cf8-4b07");
    RAWFRAME_EXPECT(entityNamed(kIds, kNames, "b456", why) == "b4560e55-e4e2");
    RAWFRAME_EXPECT(entityNamed(kIds, kNames, "9a00", why) == "9a000000-0000");
    RAWFRAME_EXPECT(entityNamed(kIds, kNames, "ga", why) == "b4560e55-e4e2");
    // An identity's start of four or more alone, and only of one.
    RAWFRAME_EXPECT(!entityNamed(kIds, kNames, "c54", why).has_value());
    RAWFRAME_EXPECT(!entityNamed(kIds, kNames, "c540", why).has_value());
    RAWFRAME_EXPECT(why == "2 entities match c540");
    RAWFRAME_EXPECT(!entityNamed(kIds, kNames, "gro", why).has_value());
    RAWFRAME_EXPECT(why == "2 entities match gro");
    RAWFRAME_EXPECT(!entityNamed(kIds, kNames, "wall", why).has_value());
    RAWFRAME_EXPECT(why == "no entity matches wall");
    RAWFRAME_EXPECT(!entityNamed(kIds, kNames, "", why).has_value());
}

RAWFRAME_TEST(AnInstanceNamesOneSceneByPathOrFile) {
    const std::vector<std::string> kScenes = {"level.scene", "parts/crate.scene", "parts/cart.scene", "lobby.scene"};
    std::string why;
    RAWFRAME_EXPECT(sceneNamed(kScenes, "parts/crate.scene", why) == 1U);
    RAWFRAME_EXPECT(sceneNamed(kScenes, "crate.scene", why) == 1U);
    RAWFRAME_EXPECT(sceneNamed(kScenes, "crate", why) == 1U);
    RAWFRAME_EXPECT(sceneNamed(kScenes, "lev", why) == 0U);
    RAWFRAME_EXPECT(sceneNamed(kScenes, "parts/cart", why) == 2U);
    // A start several scenes share is refused, as is one none has.
    RAWFRAME_EXPECT(!sceneNamed(kScenes, "parts", why).has_value());
    RAWFRAME_EXPECT(why == "2 scenes match parts");
    RAWFRAME_EXPECT(!sceneNamed(kScenes, "l", why).has_value());
    RAWFRAME_EXPECT(!sceneNamed(kScenes, "wall", why).has_value());
    RAWFRAME_EXPECT(why == "no scene matches wall");
    RAWFRAME_EXPECT(!sceneNamed(kScenes, "", why).has_value());
}

RAWFRAME_TEST(ADiagnosticIsReadAndOpenedAtItsLine) {
    const auto kRead = diagnosticOf(
        R"({"kind":"authoring.reply","id":1,"error":{"code":"internal","message":"the Kest program does not compile",)"
        R"("details":{"diagnostic":"game/a b/controls.kest:20:7: expected a declaration [K0202]"}}})");
    RAWFRAME_EXPECT(kRead.has_value() && kRead->file == "game/a b/controls.kest" && kRead->line == 20 &&
                    kRead->column == 7 && kRead->message == "expected a declaration [K0202]");
    // A refusal naming no place, or one not in the shape, gives none.
    RAWFRAME_EXPECT(!diagnosticOf(R"({"error":{"message":"no","details":{}}})").has_value());
    RAWFRAME_EXPECT(!diagnosticOf(R"({"error":{"details":{"diagnostic":"game/a.kest:x:1: no"}}})").has_value());
    RAWFRAME_EXPECT(!diagnosticOf(R"({"error":{"details":{"diagnostic":"game/a.kest:0:1: no"}}})").has_value());
    RAWFRAME_EXPECT(!diagnosticOf(R"({"error":{"details":{"diagnostic":"the first line"}}})").has_value());

    const std::vector<std::string> kCode = editorCommand("code  --goto {file}:{line}:{column}", "/g/a b.kest", 20, 7);
    RAWFRAME_EXPECT((kCode == std::vector<std::string>{"code", "--goto", "/g/a b.kest:20:7"}));
    const std::vector<std::string> kVim = editorCommand("vim +{line} {file}", "/g/a.kest", 3, 1);
    RAWFRAME_EXPECT((kVim == std::vector<std::string>{"vim", "+3", "/g/a.kest"}));
    RAWFRAME_EXPECT(editorCommand("", "/g/a.kest", 3, 1).empty());
}

RAWFRAME_TEST(AnEditorIsFoundOnThePathAsAShellFindsIt) {
    std::error_code error;
    const std::filesystem::path kRoot = std::filesystem::temp_directory_path() / ("studio-path-" + mintedIdentity());
    std::filesystem::create_directories(kRoot / "one");
    std::filesystem::create_directories(kRoot / "two");
    std::ofstream{kRoot / "two" / "edit"} << "#!/bin/sh\n";
    const std::string kPath =
        (kRoot / "none").string() + ":" + (kRoot / "one").string() + "::" + (kRoot / "two").string();
    RAWFRAME_EXPECT(programOnPath("edit", kPath) == kRoot / "two" / "edit");
    RAWFRAME_EXPECT(!programOnPath("missing", kPath).has_value());
    // A program naming a directory is itself.
    RAWFRAME_EXPECT(programOnPath("./edit", "") == std::filesystem::path{"./edit"});
    std::filesystem::remove_all(kRoot, error);
}

RAWFRAME_TEST(TypedTextIsAValueOfItsFieldsKindOrNone) {
    RAWFRAME_EXPECT(document::writeCompact(*typedValue("real", "2.5")) == R"({"real":2.5})");
    RAWFRAME_EXPECT(!typedValue("real", "x").has_value());
    RAWFRAME_EXPECT(!typedValue("real", "\"2\"").has_value());
    RAWFRAME_EXPECT(document::writeCompact(*typedValue("unsigned", "7")) == R"({"unsigned":"7"})");
    RAWFRAME_EXPECT(document::writeCompact(*typedValue("truth", "true")) == R"({"truth":true})");
    RAWFRAME_EXPECT(!typedValue("truth", "yes").has_value());
    RAWFRAME_EXPECT(!typedValue("reference", "x").has_value());
}

RAWFRAME_TEST(AnOutcomeSaysWhetherItsSlotWasDoneAndWhatCanBeUndone) {
    const Outcome kDone =
        outcomeOf(R"({"kind":"authoring.reply","id":2,"answer":{"undoable":3,"redoable":1,"results":[{"deltas":1}]}})");
    RAWFRAME_EXPECT(kDone.done && kDone.undoable == 3 && kDone.redoable == 1);
    const Outcome kRefused = outcomeOf(
        R"({"kind":"authoring.reply","id":3,"answer":{"undoable":0,"redoable":0,"results":[{"error":{"message":"there is nothing to undo"}}]}})");
    RAWFRAME_EXPECT(!kRefused.done && kRefused.message == "there is nothing to undo");
    const Outcome kInvalid = outcomeOf(R"({"kind":"authoring.reply","id":4,"error":{"message":"bad inputs"}})");
    RAWFRAME_EXPECT(!kInvalid.done && kInvalid.message == "bad inputs");
    RAWFRAME_EXPECT(!outcomeOf("not json").done);
}

RAWFRAME_TEST(AMintedIdentityIsAVersionFourUuidAndNotRepeated) {
    std::set<std::string> minted;
    for (int each = 0; each < 64; ++each) {
        const std::string kIdentity = mintedIdentity();
        RAWFRAME_EXPECT(kIdentity.size() == 36);
        RAWFRAME_EXPECT(kIdentity[8] == '-' && kIdentity[13] == '-' && kIdentity[18] == '-' && kIdentity[23] == '-');
        RAWFRAME_EXPECT(kIdentity[14] == '4');
        RAWFRAME_EXPECT(std::string{"89ab"}.find(kIdentity[19]) != std::string::npos);
        minted.insert(kIdentity);
    }
    RAWFRAME_EXPECT(minted.size() == 64);
}

RAWFRAME_TEST(AViewIsMadeWholeFromThePartTypedAndShownAsText) {
    // None yet: the typed part over a view from above the origin.
    const auto kFirst = viewWith(std::nullopt, "eye", "3 1 3");
    RAWFRAME_EXPECT(document::writeCompact(*kFirst) == R"({"eye":[3,1,3],"target":[0,0,0],"fieldOfView":60})");
    const auto kSecond = viewWith(kFirst, "fieldOfView", "70");
    RAWFRAME_EXPECT(document::writeCompact(*kSecond) == R"({"eye":[3,1,3],"target":[0,0,0],"fieldOfView":70})");
    RAWFRAME_EXPECT(viewWith(kSecond, "target", "0, 1.5,0").has_value());
    RAWFRAME_EXPECT(!viewWith(kSecond, "eye", "1 2").has_value());
    RAWFRAME_EXPECT(!viewWith(kSecond, "eye", "1 2 x").has_value());
    RAWFRAME_EXPECT(!viewWith(kSecond, "fieldOfView", "60 70").has_value());
    RAWFRAME_EXPECT(!viewWith(kSecond, "roll", "1").has_value());
    RAWFRAME_EXPECT(viewText(kSecond, "eye") == "3 1 3" && viewText(kSecond, "fieldOfView") == "70");
    RAWFRAME_EXPECT(viewText(std::nullopt, "eye").empty());
    RAWFRAME_EXPECT(viewText(Value{}, "eye").empty());
}

RAWFRAME_TEST(APreviewIsAskedForAndItsAnswerRead) {
    const Preview kPreview{"127.0.0.1:4433", "/tmp/pin", "/tmp/token"};
    RAWFRAME_EXPECT(
        document::writeCompact(previewRecord(3, "gate.scene", &kPreview)) ==
        R"({"kind":"authoring.preview","id":3,"scene":"gate.scene","preview":{"endpoint":"127.0.0.1:4433","pinFile":"/tmp/pin","tokenFile":"/tmp/token"}})");
    RAWFRAME_EXPECT(document::writeCompact(previewRecord(4, "gate.scene", nullptr)) ==
                    R"({"kind":"authoring.preview","id":4,"scene":"gate.scene","preview":null})");
    const Answered kLive =
        answeredOf(R"({"kind":"authoring.reply","id":3,"answer":{"kind":"authoring.preview","previewing":true}})");
    RAWFRAME_EXPECT(kLive.done && kLive.previewing && !kLive.view.has_value());
    const Answered kViewed = answeredOf(
        R"({"kind":"authoring.reply","id":5,"answer":{"kind":"authoring.view","view":{"eye":[1,2,3]},"previewing":false}})");
    RAWFRAME_EXPECT(kViewed.done && !kViewed.previewing && viewText(kViewed.view, "eye") == "1 2 3");
    const Answered kDenied = answeredOf(
        R"({"kind":"authoring.reply","id":6,"error":{"code":"capability_denied","message":"no view grant"}})");
    RAWFRAME_EXPECT(!kDenied.done && kDenied.message == "no view grant");
}

RAWFRAME_TEST(AComponentsFieldsAreTheCatalogsWithTheScenesValuesThenTheUnknown) {
    Catalog::Component body{.id = "a",
                            .name = "rawframe.physics2d.body",
                            .fields = {{"motion", "unsigned"}, {"width", "real"}, {"height", "real"}}};
    const auto kRead = document::parse(
        R"([{"name":"height","value":{"real":0.5}},{"name":"width","value":{"real":40}},{"name":"legacy","value":{"signed":"3"}}])");
    const std::vector<FieldShown> kShown = fieldsShown(&body, &*kRead);
    RAWFRAME_EXPECT(kShown.size() == 4);
    RAWFRAME_EXPECT(kShown[0].name == "motion" && kShown[0].kind == "unsigned" && !kShown[0].text.has_value());
    RAWFRAME_EXPECT(kShown[1].name == "width" && kShown[1].text == "40");
    RAWFRAME_EXPECT(kShown[2].name == "height" && kShown[2].kind == "real" && kShown[2].text == "0.5");
    // Set, though the catalog does not know it: last, its kind its value's.
    RAWFRAME_EXPECT(kShown[3].name == "legacy" && kShown[3].kind == "signed" && kShown[3].text == "3");
    // No type in the catalog: what the scene sets alone.
    RAWFRAME_EXPECT(fieldsShown(nullptr, &*kRead).size() == 3);
    RAWFRAME_EXPECT(fieldsShown(&body, nullptr).size() == 3);
}

RAWFRAME_TEST(APlayedProgramsSettingsKeepWhatTheyAreGivenAndAddWhatTheyLack) {
    const std::string kGiven =
        "# a client of the plaza\nbots.player = true\nrender.device  =  any\ncontent.root = /c\n";
    const std::string kMade = settingsOf(kGiven,
                                         {{"render.device", "vulkan"}, {"bots.player", "false"}, {"bots.count", "1"}},
                                         {{"tooling.grants", "view"}});
    RAWFRAME_EXPECT(kMade.starts_with(kGiven));
    // A default the given set is left out; one they lack is added.
    RAWFRAME_EXPECT(kMade.find("render.device = vulkan") == std::string::npos);
    RAWFRAME_EXPECT(kMade.find("bots.player = false") == std::string::npos);
    RAWFRAME_EXPECT(kMade.find("bots.count = 1\n") != std::string::npos);
    // Studio's own are always said.
    RAWFRAME_EXPECT(kMade.ends_with("tooling.grants = view\n"));
    // A key that only begins like one is not it.
    RAWFRAME_EXPECT(settingsOf("bots.count_extra = 2\n", {{"bots.count", "1"}}, {}).find("bots.count = 1") !=
                    std::string::npos);
}

RAWFRAME_TEST(AGameEndingAsItStartsIsLaunchedAgainAFewTimes) {
    std::error_code error;
    const std::filesystem::path kRoot = std::filesystem::temp_directory_path() / ("studio-play-" + mintedIdentity());
    // A server that ends at once, as one whose port another process took.
    auto play = Play::start(
        PlaySettings{.server = "/bin/false", .client = "/bin/false", .game = "g.game", .directory = kRoot / "play"});
    RAWFRAME_EXPECT(play.has_value());
    for (int tries = 0; play.has_value() && tries < 2000 && !(play->launches() == 5 && play->ended()); ++tries) {
        RAWFRAME_EXPECT(play->advance().has_value());
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    RAWFRAME_EXPECT(play.has_value() && play->launches() == 5 && play->ended() && !play->running());
    std::filesystem::remove_all(kRoot, error);
}

RAWFRAME_TEST(APlayedGamesFilesAreItsOwnersAlone) {
    std::error_code error;
    const std::filesystem::path kRoot = std::filesystem::temp_directory_path() / ("studio-play-" + mintedIdentity());
    const std::filesystem::path kDirectory = kRoot / "play";
    std::filesystem::create_directories(kDirectory);
    // A link where the token goes, to a file of someone else's.
    const std::filesystem::path kElsewhere = kRoot / "elsewhere";
    {
        std::ofstream{kElsewhere} << "kept";
    }
    std::filesystem::create_symlink(kElsewhere, kDirectory / "token", error);
    // Programs that are not there still find the files written first.
    static_cast<void>(Play::start(
        PlaySettings{.server = "/nonexistent", .client = "/nonexistent", .game = "g.game", .directory = kDirectory}));
    std::string kept;
    std::getline(std::ifstream{kElsewhere}, kept);
    RAWFRAME_EXPECT(kept == "kept");
    RAWFRAME_EXPECT(std::filesystem::is_regular_file(std::filesystem::symlink_status(kDirectory / "token")));
    RAWFRAME_EXPECT(std::filesystem::status(kDirectory).permissions() == std::filesystem::perms::owner_all);
    // A play directory that is a link is no directory of its own.
    std::filesystem::create_directory_symlink(kDirectory, kRoot / "linked", error);
    RAWFRAME_EXPECT(!Play::start(PlaySettings{.server = "/nonexistent",
                                              .client = "/nonexistent",
                                              .game = "g.game",
                                              .directory = kRoot / "linked"})
                         .has_value());
    std::filesystem::remove_all(kRoot, error);
}
