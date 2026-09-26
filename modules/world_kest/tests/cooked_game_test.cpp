// A cooked game description: its text, the documents it names, its
// programs as files of Kest sources, and its scenes, meshes, animators'
// graphs, and text documents as resources, written in one form and read
// back exactly; anything else, mutated text included, refused.

#include "rawframe/test/mutations.h"
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
        .scenes = {{.path = "level.scene", .scene = base::parseBits128Hex("52771075251e7361deaecf4939c72e56").value}},
        .meshes = {{.path = "hall.gltf", .mesh = base::parseBits128Hex("0c6a5d1e2f3b4a5968778695a4b3c2d1").value}},
        .animators = {{.path = "walker.rfanim",
                       .graph = base::parseBits128Hex("7d4f2a90c1b3e5d6a8f0e2c4b6d8fa1c").value}},
        .texts = {{.path = "hud.strings", .document = base::parseBits128Hex("749e2ba7d0067a4db2348b183fc4d55f").value},
                  {.path = "hud.tr.translations",
                   .document = base::parseBits128Hex("0123456789abcdef0123456789abcdef").value}}};
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
    RAWFRAME_EXPECT(kRead->mesh("hall.gltf") != nullptr && kRead->mesh("hall.gltf")->mesh == sample().meshes[0].mesh &&
                    kRead->mesh("level.scene") == nullptr);
    RAWFRAME_EXPECT(kRead->animator("walker.rfanim") != nullptr &&
                    kRead->animator("walker.rfanim")->graph == sample().animators[0].graph &&
                    kRead->animator("hall.gltf") == nullptr);
    RAWFRAME_EXPECT(kRead->texts.size() == 2 && kRead->textDocument("hud.strings") != nullptr &&
                    kRead->textDocument("hud.strings")->document == sample().texts[0].document &&
                    kRead->textDocument("level.scene") == nullptr);
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
    CookedGame noMesh = sample();
    noMesh.meshes[0].mesh = {};
    RAWFRAME_EXPECT(refused(writeCookedGame(noMesh)));
    CookedGame noGraph = sample();
    noGraph.animators[0].graph = {};
    RAWFRAME_EXPECT(refused(writeCookedGame(noGraph)));
    CookedGame noText = sample();
    noText.texts[1].document = {};
    RAWFRAME_EXPECT(refused(writeCookedGame(noText)));

    const std::string kGood = *writeCookedGame(sample());
    const std::string kTail = ",\"formatVersion\":5,\"kind\":\"game.description\",\"meshes\":[],\"programs\":[],"
                              "\"scenes\":[],\"text\":\"\",\"texts\":[]}";
    const std::string kProgramsHead =
        "{\"animators\":[],\"files\":[],\"formatVersion\":5,\"kind\":\"game.description\",\"meshes\":[],\"programs\":[";
    const std::string kProgramsTail = "],\"scenes\":[],\"text\":\"\",\"texts\":[]}";
    const std::string kScenesHead = "{\"animators\":[],\"files\":[],\"formatVersion\":5,\"kind\":\"game.description\","
                                    "\"meshes\":[],\"programs\":[],"
                                    "\"scenes\":[";
    const std::string kScenesTail = "],\"text\":\"\",\"texts\":[]}";
    const std::vector<std::string> kBad = {
        kGood.substr(0, kGood.size() - 1),
        "{\"animators\":[],\"files\": []" + kTail,
        "{\"animators\":[],\"files\":[]" +
            std::string{",\"formatVersion\":3,\"kind\":\"game.description\",\"meshes\":[],\"programs\":[]"
                        ",\"scenes\":[],\"text\":\"\",\"texts\":[]}"},
        "{\"animators\":[],\"files\":[],\"formatVersion\":5,\"kind\":\"game.description\",\"meshes\":[],\"programs\":[]"
        ",\"text\":\"\",\"texts\":[]}",
        "{\"animators\":[],\"extra\":0,\"files\":[]" + kTail,
        "{\"animators\":[],\"files\":[{\"path\":\"b\",\"text\":\"\",\"texts\":[]},{\"path\":\"a\",\"text\":\"\","
        "\"texts\":[]}]" +
            kTail,
        "{\"animators\":[],\"files\":[{\"path\":\"a\",\"text\":\"\",\"x\":\"\"}]" + kTail,
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
        "{\"animators\":[],\"files\":[],\"formatVersion\":5,\"kind\":\"game.description\",\"programs\":[],\"scenes\":[]"
        ",\"text\":\"\",\"texts\":[]}",
        "{\"animators\":[],\"files\":[],\"formatVersion\":5,\"kind\":\"game.description\",\"meshes\":[{\"mesh\":"
        "\"00000000000000000000000000000000\",\"path\":\"a\"}],\"programs\":[],\"scenes\":[],\"text\":\"\",\"texts\":[]"
        "}",
        // Format 3 had no animators.
        "{\"files\":[]" + std::string{",\"formatVersion\":5,\"kind\":\"game.description\",\"meshes\":[],\"programs\":[]"
                                      ",\"scenes\":[],\"text\":\"\",\"texts\":[]}"},
        "{\"animators\":[{\"graph\":\"00000000000000000000000000000000\",\"path\":\"a\"}]" +
            std::string{",\"files\":[]"} + kTail,
        // Format 4 had no texts.
        "{\"animators\":[],\"files\":[],\"formatVersion\":4,\"kind\":\"game.description\",\"meshes\":[],"
        "\"programs\":[],\"scenes\":[],\"text\":\"\"}",
        "{\"animators\":[],\"files\":[],\"formatVersion\":5,\"kind\":\"game.description\",\"meshes\":[],"
        "\"programs\":[],\"scenes\":[],\"text\":\"\",\"texts\":[{\"document\":\"00000000000000000000000000000000\","
        "\"path\":\"a\"}]}",
    };
    for (const std::string& bad : kBad) {
        RAWFRAME_EXPECT(refused(readCookedGame(bad)));
    }
    RAWFRAME_EXPECT(readCookedGame("{\"animators\":[],\"files\":[]" + kTail).has_value());
}

RAWFRAME_TEST(HostileCookedGamesReadOnlyAsTheyWrite) {
    // A client reads its game from content it fetched.
    const auto kWritten = writeCookedGame(sample());
    RAWFRAME_EXPECT(kWritten.has_value());
    if (!kWritten.has_value()) {
        return;
    }
    test::Mutations mutations;
    std::size_t read = 0;
    for (int round = 0; round < 20'000; ++round) {
        const std::string kDamaged = mutations.mutate(*kWritten, "\"{}[],:\\ 0a\n\x00\xff");
        const auto kRead = readCookedGame(kDamaged);
        if (!kRead.has_value()) {
            RAWFRAME_EXPECT(refused(kRead));
            continue;
        }
        ++read;
        const auto kAgain = writeCookedGame(*kRead);
        RAWFRAME_EXPECT(kAgain.has_value() && *kAgain == kDamaged);
    }
    RAWFRAME_EXPECT(read > 0);
}
