// The embedded Kest library: every file of `std` and of the engine's modules,
// byte for byte as in the tree, and a sample game's program compiled from its
// own files with nothing else read.

#include "rawframe/kest/errors.h"
#include "rawframe/kest_library/library.h"
#include "rawframe/test/files.h"
#include "rawframe/test/test.h"

#include <algorithm>
#include <string>
#include <vector>

using namespace rawframe;

namespace {

/// Every `.kest` file under a directory, by its path relative to it.
std::vector<kest::SourceFile> kestFilesUnder(const std::string& directory) {
    std::vector<kest::SourceFile> made;
    for (const std::string& path : test::filesUnder(directory, ".kest")) {
        made.push_back(kest::SourceFile{.path = path, .text = test::readFile(directory + "/" + path)});
    }
    return made;
}

} // namespace

RAWFRAME_TEST(TheLibraryIsTheTreesFilesExactly) {
    std::vector<kest::SourceFile> expected;
    for (kest::SourceFile& file : kestFilesUnder(RAWFRAME_KEST_LIBRARY)) {
        expected.push_back({.path = std::string{kest_library::kStandardLibrary} + file.path, .text = file.text});
    }
    for (kest::SourceFile& file : kestFilesUnder(RAWFRAME_ENGINE_MODULES)) {
        expected.push_back({.path = std::string{kest_library::kEngineModules} + file.path, .text = file.text});
    }
    std::ranges::sort(expected, {}, &kest::SourceFile::path);
    std::vector<kest::SourceFile> embedded = kest_library::files();
    std::ranges::sort(embedded, {}, &kest::SourceFile::path);
    RAWFRAME_EXPECT(embedded.size() == expected.size() && embedded.size() > 10);
    for (std::size_t at = 0; at < std::min(embedded.size(), expected.size()); ++at) {
        RAWFRAME_EXPECT(embedded[at].path == expected[at].path && embedded[at].text == expected[at].text);
    }
    RAWFRAME_EXPECT(
        std::ranges::contains(embedded, std::string{"engine/modules/rawframe/world.kest"}, &kest::SourceFile::path));
}

RAWFRAME_TEST(AGamesProgramCompilesFromItsOwnFiles) {
    // Runners imports std, rawframe.world, .physics2d, .random,
    // .replication, .sound, and .input, and its own `controls`.
    const std::vector<kest::SourceFile> kGame = kestFilesUnder(std::string{RAWFRAME_SAMPLE_GAMES} + "runners");
    std::string report;
    const auto kProgram = kest_library::compile("runners.kest", kGame, {}, &report);
    RAWFRAME_EXPECT(kProgram.has_value());
    if (!kProgram.has_value()) {
        std::fputs(report.c_str(), stderr);
        return;
    }
    // The engine's modules were found: their doors are asked for.
    RAWFRAME_EXPECT(std::ranges::contains((*kProgram)->doorsRequested(), std::string{"World.create"}) &&
                    std::ranges::contains((*kProgram)->doorsRequested(), std::string{"Physics2D.castRayAt"}));
    const auto kSample = kest_library::compile("sample.kest", kGame, {});
    RAWFRAME_EXPECT(kSample.has_value() && std::ranges::contains((*kSample)->doorsRequested(), std::string{"Input.x"}));
}

RAWFRAME_TEST(AGamesFilesAreItsOwnAndPlain) {
    const std::vector<kest::SourceFile> kGame = kestFilesUnder(std::string{RAWFRAME_SAMPLE_GAMES} + "runners");
    const auto kRefused = [](const auto& outcome) {
        return !outcome.has_value() && outcome.error().code() == code(kest::KestError::DoesNotCompile);
    };
    RAWFRAME_EXPECT(kRefused(kest_library::compile("missing.kest", kGame, {})));
    for (const std::string_view kPath :
         {"../runners.kest", "/runners.kest", "./runners.kest", "a//b.kest", "kest.project", ""}) {
        std::vector<kest::SourceFile> game = kGame;
        game.push_back({.path = std::string{kPath}, .text = "module x\n"});
        RAWFRAME_EXPECT(kRefused(kest_library::compile("runners.kest", game, {})));
    }
    // A program importing what neither it nor the library holds does not
    // compile: nothing is looked for on a disk.
    const std::vector<kest::SourceFile> kLonely = {
        {.path = "lonely.kest", .text = "module lonely\n\nimport elsewhere\n\nfn f() {\n}\n"}};
    RAWFRAME_EXPECT(kRefused(kest_library::compile("lonely.kest", kLonely, {})));
}
