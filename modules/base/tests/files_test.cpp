// A file replaced whole: written where there was none, then replaced, with
// nothing left beside it; and refused where its directory is missing.

#include "rawframe/base/files.h"
#include "rawframe/test/scratch.h"
#include "rawframe/test/test.h"

#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

using namespace rawframe;

namespace {

std::string contentsOf(const std::filesystem::path& path) {
    std::ifstream file{path, std::ios::binary};
    return {std::istreambuf_iterator<char>{file}, std::istreambuf_iterator<char>{}};
}

std::vector<std::byte> bytesOf(std::string_view text) {
    std::vector<std::byte> bytes(text.size());
    for (std::size_t at = 0; at < text.size(); ++at) {
        bytes[at] = static_cast<std::byte>(text[at]);
    }
    return bytes;
}

} // namespace

RAWFRAME_TEST(AFileIsReplacedWhole) {
    const std::filesystem::path kDirectory = test::scratchDirectory("files");
    const std::filesystem::path kPath = kDirectory / "kept.json";
    std::filesystem::create_directories(kDirectory);
    RAWFRAME_EXPECT(base::replaceFile(kPath, bytesOf("first and longer")));
    RAWFRAME_EXPECT(contentsOf(kPath) == "first and longer");
    RAWFRAME_EXPECT(base::replaceFile(kPath, bytesOf("second")));
    RAWFRAME_EXPECT(contentsOf(kPath) == "second");
    RAWFRAME_EXPECT(!std::filesystem::exists(kDirectory / "kept.json.partial"));
    // Nothing at all is a file too.
    RAWFRAME_EXPECT(base::replaceFile(kPath, {}));
    RAWFRAME_EXPECT(std::filesystem::file_size(kPath) == 0);
    // A directory that is not there is not made.
    RAWFRAME_EXPECT(!base::replaceFile(kDirectory / "absent" / "kept.json", bytesOf("none")));
    RAWFRAME_EXPECT(!std::filesystem::exists(kDirectory / "absent"));
    std::filesystem::remove_all(kDirectory);
}

#if defined(__unix__) || defined(__APPLE__)
RAWFRAME_TEST(AFileIsNeverWrittenThroughALink) {
    // A directory from elsewhere, a game's, may hold links to files outside
    // it where a write would go (D847): the file they name is kept.
    const std::filesystem::path kDirectory = test::scratchDirectory("files_linked");
    std::filesystem::create_directories(kDirectory);
    const std::filesystem::path kElsewhere = kDirectory / "elsewhere.txt";
    RAWFRAME_EXPECT(base::writeWholeFile(kElsewhere, bytesOf("kept")));
    // Where the partial file goes: taken away, the file put whole.
    std::filesystem::create_symlink(kElsewhere, kDirectory / "level.scene.partial");
    RAWFRAME_EXPECT(base::replaceFile(kDirectory / "level.scene", bytesOf("written")));
    RAWFRAME_EXPECT(contentsOf(kDirectory / "level.scene") == "written" && contentsOf(kElsewhere) == "kept");
    // At the file itself: replaced, not followed.
    std::filesystem::create_symlink(kElsewhere, kDirectory / "other.scene");
    RAWFRAME_EXPECT(base::replaceFile(kDirectory / "other.scene", bytesOf("written")));
    RAWFRAME_EXPECT(!std::filesystem::is_symlink(kDirectory / "other.scene") && contentsOf(kElsewhere) == "kept");
    // Written whole in place: refused.
    std::filesystem::create_symlink(kElsewhere, kDirectory / "linked.scene");
    RAWFRAME_EXPECT(!base::writeWholeFile(kDirectory / "linked.scene", bytesOf("written")));
    RAWFRAME_EXPECT(contentsOf(kElsewhere) == "kept");
    std::filesystem::remove_all(kDirectory);
}
#endif
