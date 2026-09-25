// A game's Kest files as one resource: written in one form only, read back
// exactly, and refused in any other.

#include "rawframe/kest_library/errors.h"
#include "rawframe/kest_library/library.h"
#include "rawframe/test/test.h"

#include <string>
#include <vector>

using namespace rawframe;

namespace {

bool refused(const auto& outcome) {
    return !outcome.has_value() && outcome.error().domain() == kest_library::kKestLibraryDomain &&
           outcome.error().code() == code(kest_library::KestLibraryError::SourcesInvalid);
}

const std::vector<kest::SourceFile> kGame = {
    {.path = "shots.kest", .text = "module shots\n\nimport rawframe.world\n\nfn fire(count: i32) {\n}\n"},
    {.path = "rules/score.kest", .text = "module score\n\n// \"quoted\", a tab\t, and \\ too.\nfn add() {\n}\n"},
};

} // namespace

RAWFRAME_TEST(AGamesSourcesRoundTripInOneForm) {
    const auto kWritten = kest_library::writeGameSources(kGame);
    RAWFRAME_EXPECT(kWritten.has_value());
    if (!kWritten.has_value()) {
        return;
    }
    const auto kRead = kest_library::readGameSources(*kWritten);
    RAWFRAME_EXPECT(kRead.has_value() && kRead->size() == 2);
    if (!kRead.has_value() || kRead->size() != 2) {
        return;
    }
    // In path order, whatever order they were given in.
    RAWFRAME_EXPECT((*kRead)[0].path == "rules/score.kest" && (*kRead)[0].text == kGame[1].text &&
                    (*kRead)[1].path == "shots.kest" && (*kRead)[1].text == kGame[0].text);
    const auto kAgain = kest_library::writeGameSources(*kRead);
    RAWFRAME_EXPECT(kAgain.has_value() && *kAgain == *kWritten);
    // What was read compiles.
    RAWFRAME_EXPECT(kest_library::compile("shots.kest", *kRead, {}).has_value());
}

RAWFRAME_TEST(AnythingElseIsRefused) {
    std::vector<kest::SourceFile> twice = kGame;
    twice.push_back(kGame[0]);
    RAWFRAME_EXPECT(refused(kest_library::writeGameSources(twice)));
    for (const std::string_view kPath : {"../x.kest", "/x.kest", "./x.kest", "x/", "kest.project", ""}) {
        std::vector<kest::SourceFile> game = kGame;
        game.push_back({.path = std::string{kPath}, .text = ""});
        RAWFRAME_EXPECT(refused(kest_library::writeGameSources(game)));
    }
    std::vector<kest::SourceFile> many(kest_library::kMaximumGameFiles + 1);
    for (std::size_t at = 0; at < many.size(); ++at) {
        many[at].path = "f" + std::to_string(at) + ".kest";
    }
    RAWFRAME_EXPECT(refused(kest_library::writeGameSources(many)));
    many.pop_back();
    RAWFRAME_EXPECT(kest_library::writeGameSources(many).has_value());

    const std::string kGood = *kest_library::writeGameSources(kGame);
    const std::vector<std::string> kBad = {
        // Not canonical: a space.
        "{\"files\": [],\"formatVersion\":1,\"kind\":\"kest.sources\"}",
        // Another kind, another version, a member more.
        "{\"files\":[],\"formatVersion\":1,\"kind\":\"kest.other\"}",
        "{\"files\":[],\"formatVersion\":2,\"kind\":\"kest.sources\"}",
        "{\"extra\":1,\"files\":[],\"formatVersion\":1,\"kind\":\"kest.sources\"}",
        // Files out of order, twice, or with a member more.
        "{\"files\":[{\"path\":\"b.kest\",\"text\":\"\"},{\"path\":\"a.kest\",\"text\":\"\"}],\"formatVersion\":1,"
        "\"kind\":\"kest.sources\"}",
        "{\"files\":[{\"path\":\"a.kest\",\"text\":\"\"},{\"path\":\"a.kest\",\"text\":\"\"}],\"formatVersion\":1,"
        "\"kind\":\"kest.sources\"}",
        "{\"files\":[{\"mode\":1,\"path\":\"a.kest\",\"text\":\"\"}],\"formatVersion\":1,\"kind\":\"kest.sources\"}",
        "{\"files\":[{\"path\":\"../a.kest\",\"text\":\"\"}],\"formatVersion\":1,\"kind\":\"kest.sources\"}",
        // Cut short.
        kGood.substr(0, kGood.size() - 1),
    };
    for (const std::string& bad : kBad) {
        RAWFRAME_EXPECT(refused(kest_library::readGameSources(bad)));
    }
    RAWFRAME_EXPECT(
        kest_library::readGameSources("{\"files\":[],\"formatVersion\":1,\"kind\":\"kest.sources\"}").has_value());
}
