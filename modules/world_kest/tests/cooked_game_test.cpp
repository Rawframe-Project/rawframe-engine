// A cooked game description: its text, the documents it names, and its
// programs as files of Kest sources, written in one form and read back
// exactly; anything else refused.

#include "rawframe/test/test.h"
#include "rawframe/world_kest/cooked_game.h"
#include "rawframe/world_kest/errors.h"

#include <string>
#include <vector>

using namespace rawframe;
using namespace rawframe::world_kest;

namespace {

bool refused(const auto& outcome) {
    return !outcome.has_value() && outcome.error().domain() == kWorldKestDomain &&
           outcome.error().code() == code(WorldKestError::CookedGameInvalid);
}

CookedGame sample() {
    const base::Bits128 kSources = base::parseBits128Hex("f9f0181057571ecd398d86d2c34a641f").value;
    return CookedGame{
        .text = "program runners.kest\nmixer runners.mixer\nsound 950e09337d03de69 shot.sound\n",
        .files = {{.path = "shot.sound", .text = "{\"kind\": \"audio.sound\"}\n"},
                  {.path = "runners.mixer", .text = "{\"kind\": \"audio.mixer\"}\n"}},
        .programs = {{.path = "sample.kest", .sources = kSources, .entry = "sample.kest"},
                     {.path = "runners.kest", .sources = kSources, .entry = "runners.kest"}},
        .scenes = {{.path = "level.scene", .scene = base::parseBits128Hex("52771075251e7361deaecf4939c72e56").value}}};
}

} // namespace

RAWFRAME_TEST(ACookedGameRoundTripsInOneForm) {
    const auto kWritten = writeCookedGame(sample());
    RAWFRAME_EXPECT(kWritten.has_value());
    if (!kWritten.has_value()) {
        return;
    }
    const auto kRead = readCookedGame(*kWritten);
    RAWFRAME_EXPECT(kRead.has_value());
    if (!kRead.has_value()) {
        return;
    }
    RAWFRAME_EXPECT(kRead->text == sample().text && kRead->files.size() == 2 && kRead->programs.size() == 2 &&
                    kRead->scene("level.scene") != nullptr &&
                    kRead->scene("level.scene")->scene == sample().scenes[0].scene && kRead->scene("x") == nullptr);
    // Names answered from the record, in path order.
    RAWFRAME_EXPECT(kRead->files[0].path == "runners.mixer" && kRead->file("shot.sound") != nullptr &&
                    kRead->file("shot.sound")->text == sample().files[0].text && kRead->file("other") == nullptr);
    RAWFRAME_EXPECT(kRead->program("runners.kest") != nullptr &&
                    kRead->program("runners.kest")->entry == "runners.kest" &&
                    kRead->program("runners.kest")->sources == sample().programs[0].sources &&
                    kRead->program("controls.kest") == nullptr);
    const auto kAgain = writeCookedGame(*kRead);
    RAWFRAME_EXPECT(kAgain.has_value() && *kAgain == *kWritten);
}

RAWFRAME_TEST(ACookedGameIsRefusedInAnyOtherForm) {
    CookedGame twice = sample();
    twice.files.push_back(twice.files[0]);
    RAWFRAME_EXPECT(refused(writeCookedGame(twice)));
    CookedGame unnamed = sample();
    unnamed.files[0].path.clear();
    RAWFRAME_EXPECT(refused(writeCookedGame(unnamed)));
    CookedGame noEntry = sample();
    noEntry.programs[0].entry.clear();
    RAWFRAME_EXPECT(refused(writeCookedGame(noEntry)));
    CookedGame noSources = sample();
    noSources.programs[1].sources = {};
    RAWFRAME_EXPECT(refused(writeCookedGame(noSources)));
    CookedGame noScene = sample();
    noScene.scenes[0].scene = {};
    RAWFRAME_EXPECT(refused(writeCookedGame(noScene)));

    const std::string kGood = *writeCookedGame(sample());
    const std::string kTail =
        ",\"formatVersion\":2,\"kind\":\"game.description\",\"programs\":[],\"scenes\":[],\"text\":\"\"}";
    const std::string kProgramsHead = "{\"files\":[],\"formatVersion\":2,\"kind\":\"game.description\",\"programs\":[";
    const std::string kProgramsTail = "],\"scenes\":[],\"text\":\"\"}";
    const std::string kScenesHead = "{\"files\":[],\"formatVersion\":2,\"kind\":\"game.description\",\"programs\":[],"
                                    "\"scenes\":[";
    const std::string kScenesTail = "],\"text\":\"\"}";
    const std::vector<std::string> kBad = {
        kGood.substr(0, kGood.size() - 1),
        "{\"files\": []" + kTail,
        "{\"files\":[]" +
            std::string{
                ",\"formatVersion\":1,\"kind\":\"game.description\",\"programs\":[],\"scenes\":[],\"text\":\"\"}"},
        "{\"files\":[],\"formatVersion\":2,\"kind\":\"game.description\",\"programs\":[],\"text\":\"\"}",
        "{\"extra\":0,\"files\":[]" + kTail,
        "{\"files\":[{\"path\":\"b\",\"text\":\"\"},{\"path\":\"a\",\"text\":\"\"}]" + kTail,
        "{\"files\":[{\"path\":\"a\",\"text\":\"\",\"x\":\"\"}]" + kTail,
        kProgramsHead + "{\"entry\":\"a\",\"path\":\"a\",\"sources\":\"F9F0181057571ECD398D86D2C34A641F\"}" +
            kProgramsTail,
        kProgramsHead + "{\"entry\":\"a\",\"path\":\"a\",\"sources\":\"00000000000000000000000000000000\"}" +
            kProgramsTail,
        kScenesHead + "{\"path\":\"a\",\"scene\":\"00000000000000000000000000000000\"}" + kScenesTail,
        kScenesHead +
            "{\"path\":\"b\",\"scene\":\"52771075251e7361deaecf4939c72e56\"},{\"path\":\"a\",\"scene\":"
            "\"52771075251e7361deaecf4939c72e56\"}" +
            kScenesTail,
        kScenesHead + "{\"path\":\"a\"}" + kScenesTail,
    };
    for (const std::string& bad : kBad) {
        RAWFRAME_EXPECT(refused(readCookedGame(bad)));
    }
    RAWFRAME_EXPECT(readCookedGame("{\"files\":[]" + kTail).has_value());
}
